#ifndef STRICT
#define STRICT 1
#endif

#include "publishing_job_store.h"

#include <windows.h>

#include <climits>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace pdw
{
namespace publishing
{
namespace
{
	const char JOB_MAGIC_V1[] = "PDW-PUBLISH-JOB 1";
	const char JOB_MAGIC_V2[] = "PDW-PUBLISH-JOB 2";
	const std::size_t MAX_ID_BYTES = 128;

	void SetWindowsError(const char* operation, std::string& error)
	{
		char message[160];
		_snprintf_s(message, sizeof(message), _TRUNCATE, "%s (Windows error %lu).",
			operation, static_cast<unsigned long>(GetLastError()));
		error = message;
	}

	bool ParseUnsigned(const std::string& text, unsigned int& value)
	{
		if (text.empty()) return false;
		unsigned int parsed = 0;
		for (std::string::const_iterator character = text.begin(); character != text.end(); ++character)
		{
			if (*character < '0' || *character > '9') return false;
			const unsigned int digit = static_cast<unsigned int>(*character - '0');
			if (parsed > (UINT_MAX - digit) / 10u) return false;
			parsed = parsed * 10u + digit;
		}
		value = parsed;
		return true;
	}

	bool ReadLine(const std::string& text, std::size_t& position, std::string& line)
	{
		if (position > text.size()) return false;
		const std::size_t end = text.find('\n', position);
		if (end == std::string::npos) return false;
		line.assign(text, position, end - position);
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		position = end + 1;
		return true;
	}

	bool ReadHeaderValue(const std::string& text, std::size_t& position,
		const char* key, std::string& value)
	{
		std::string line;
		if (!ReadLine(text, position, line)) return false;
		const std::string prefix = std::string(key) + "=";
		if (line.compare(0, prefix.size(), prefix) != 0) return false;
		value = line.substr(prefix.size());
		return true;
	}

	void AppendUtf8(std::string& output, unsigned int codePoint)
	{
		if (codePoint <= 0x7f)
			output.push_back(static_cast<char>(codePoint));
		else if (codePoint <= 0x7ff)
		{
			output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		}
		else
		{
			output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		}
	}

	int HexValue(char value)
	{
		if (value >= '0' && value <= '9') return value - '0';
		if (value >= 'a' && value <= 'f') return value - 'a' + 10;
		if (value >= 'A' && value <= 'F') return value - 'A' + 10;
		return -1;
	}

	class JsonReader
	{
	public:
		explicit JsonReader(const std::string& text) : text_(text), position_(0) {}

		bool Consume(const char* literal)
		{
			const std::size_t length = strlen(literal);
			if (text_.compare(position_, length, literal) != 0) return false;
			position_ += length;
			return true;
		}

		bool String(std::string& value)
		{
			value.clear();
			if (position_ >= text_.size() || text_[position_++] != '"') return false;
			while (position_ < text_.size())
			{
				const unsigned char current = static_cast<unsigned char>(text_[position_++]);
				if (current == '"') return true;
				if (current < 0x20) return false;
				if (current != '\\')
				{
					value.push_back(static_cast<char>(current));
					continue;
				}
				if (position_ >= text_.size()) return false;
				const char escaped = text_[position_++];
				switch (escaped)
				{
					case '"': value.push_back('"'); break;
					case '\\': value.push_back('\\'); break;
					case '/': value.push_back('/'); break;
					case 'b': value.push_back('\b'); break;
					case 'f': value.push_back('\f'); break;
					case 'n': value.push_back('\n'); break;
					case 'r': value.push_back('\r'); break;
					case 't': value.push_back('\t'); break;
					case 'u':
					{
						if (position_ + 4 > text_.size()) return false;
						unsigned int codePoint = 0;
						for (int index = 0; index < 4; ++index)
						{
							const int digit = HexValue(text_[position_++]);
							if (digit < 0) return false;
							codePoint = (codePoint << 4) | static_cast<unsigned int>(digit);
						}
						if (codePoint >= 0xd800 && codePoint <= 0xdfff) return false;
						AppendUtf8(value, codePoint);
						break;
					}
					default: return false;
				}
			}
			return false;
		}

		bool Boolean(bool& value)
		{
			if (Consume("true")) { value = true; return true; }
			if (Consume("false")) { value = false; return true; }
			return false;
		}

		bool NullableInteger(int& value)
		{
			if (Consume("null")) { value = -1; return true; }
			if (position_ >= text_.size()) return false;
			bool negative = false;
			if (text_[position_] == '-') { negative = true; ++position_; }
			if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') return false;
			const unsigned long long limit = static_cast<unsigned long long>(INT_MAX) +
				(negative ? 1u : 0u);
			unsigned long long parsed = 0;
			while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
			{
				const unsigned int digit = static_cast<unsigned int>(text_[position_++] - '0');
				if (parsed > (limit - digit) / 10u) return false;
				parsed = parsed * 10u + digit;
			}
			if (negative && parsed == static_cast<unsigned long long>(INT_MAX) + 1u)
				value = INT_MIN;
			else if (negative)
				value = -static_cast<int>(parsed);
			else value = static_cast<int>(parsed);
			return true;
		}

		bool AtEnd() const { return position_ == text_.size(); }

	private:
		const std::string& text_;
		std::size_t position_;
	};

	bool ParseEventPayload(const std::string& payload, PublishEvent& event)
	{
		JsonReader input(payload);
		return input.Consume("{\"id\":") && input.String(event.id) &&
			input.Consume(",\"timestamp\":") && input.String(event.timestamp) &&
			input.Consume(",\"source\":") && input.String(event.source) &&
			input.Consume(",\"address\":") && input.String(event.address) &&
			input.Consume(",\"time\":") && input.String(event.time) &&
			input.Consume(",\"date\":") && input.String(event.date) &&
			input.Consume(",\"mode\":") && input.String(event.mode) &&
			input.Consume(",\"message_type\":") && input.String(event.messageType) &&
			input.Consume(",\"bitrate\":") && input.String(event.bitrate) &&
			input.Consume(",\"message\":") && input.String(event.message) &&
			input.Consume(",\"filter_label\":") && input.String(event.filterLabel) &&
			input.Consume(",\"filter_matched\":") && input.Boolean(event.filterMatched) &&
			input.Consume(",\"monitor_only\":") && input.Boolean(event.monitorOnly) &&
			input.Consume(",\"filtered\":") && input.Boolean(event.filtered) &&
			input.Consume(",\"rejected\":") && input.Boolean(event.rejected) &&
			input.Consume(",\"blocked_duplicate\":") && input.Boolean(event.blockedDuplicate) &&
			input.Consume(",\"group_call\":") && input.Boolean(event.groupCall) &&
			input.Consume(",\"group_final\":") && input.Boolean(event.groupFinal) &&
			input.Consume(",\"fragmented\":") && input.Boolean(event.fragmented) &&
			input.Consume(",\"assembled\":") && input.Boolean(event.assembled) &&
			input.Consume(",\"filter_index\":") && input.NullableInteger(event.filterIndex) &&
			input.Consume(",\"group_bit\":") && input.NullableInteger(event.groupBit) &&
			input.Consume(",\"cycle\":") && input.NullableInteger(event.cycle) &&
			input.Consume(",\"frame\":") && input.NullableInteger(event.frame) &&
			input.Consume("}") && input.AtEnd();
	}

	bool ValidateRecord(const PublishJobRecord& record, bool allowMissingStaticPath,
		std::string& error)
	{
		if (!IsSafePublishJobId(record.event.id))
		{
			error = "Publish-job event ID is empty or unsafe.";
			return false;
		}
		if ((record.targets & ~static_cast<unsigned int>(PUBLISH_JOB_TARGET_ALL)) != 0 ||
			(record.completed & ~record.targets) != 0 ||
			(record.failed & ~record.targets) != 0 ||
			(record.completed & record.failed) != 0)
		{
			error = "Publish-job target state is invalid.";
			return false;
		}
		if (record.staticAttempts > PUBLISH_JOB_MAX_ATTEMPTS ||
			record.webhookAttempts > PUBLISH_JOB_MAX_ATTEMPTS ||
			(!(record.targets & PUBLISH_JOB_TARGET_STATIC) && record.staticAttempts != 0) ||
			(!(record.targets & PUBLISH_JOB_TARGET_WEBHOOK) && record.webhookAttempts != 0))
		{
			error = "Publish-job per-target attempt state is invalid.";
			return false;
		}
		const bool staticFailed = (record.failed & PUBLISH_JOB_TARGET_STATIC) != 0;
		const bool webhookFailed = (record.failed & PUBLISH_JOB_TARGET_WEBHOOK) != 0;
		const bool staticIncomplete = (record.targets & PUBLISH_JOB_TARGET_STATIC) != 0 &&
			(record.completed & PUBLISH_JOB_TARGET_STATIC) == 0;
		const bool webhookIncomplete = (record.targets & PUBLISH_JOB_TARGET_WEBHOOK) != 0 &&
			(record.completed & PUBLISH_JOB_TARGET_WEBHOOK) == 0;
		if (staticFailed != (staticIncomplete && record.staticAttempts == PUBLISH_JOB_MAX_ATTEMPTS) ||
			webhookFailed != (webhookIncomplete && record.webhookAttempts == PUBLISH_JOB_MAX_ATTEMPTS))
		{
			error = "Publish-job failed-target state does not match its attempt counters.";
			return false;
		}
		bool invalidStaticPath = record.staticOutputPath.size() > PUBLISH_JOB_MAX_STATIC_PATH_BYTES ||
			record.staticOutputPath.find('\0') != std::string::npos ||
			record.staticOutputPath.find('\r') != std::string::npos ||
			record.staticOutputPath.find('\n') != std::string::npos;
		if (!record.staticOutputPath.empty())
		{
			const bool driveAbsolute = record.staticOutputPath.size() >= 3 &&
				record.staticOutputPath[1] == ':' &&
				(record.staticOutputPath[2] == '\\' || record.staticOutputPath[2] == '/');
			const bool uncAbsolute = record.staticOutputPath.size() >= 2 &&
				record.staticOutputPath[0] == '\\' && record.staticOutputPath[1] == '\\';
			invalidStaticPath = invalidStaticPath || (!driveAbsolute && !uncAbsolute) ||
				(record.targets & PUBLISH_JOB_TARGET_STATIC) == 0;
		}
		if (invalidStaticPath ||
			(!allowMissingStaticPath && (record.targets & PUBLISH_JOB_TARGET_STATIC) != 0 &&
				record.staticOutputPath.empty()))
		{
			error = "Publish-job static output path is missing or invalid.";
			return false;
		}
		if (record.payload.empty() || record.payload.size() >= PUBLISH_JOB_MAX_FILE_BYTES)
		{
			error = "Publish-job payload is empty or too large.";
			return false;
		}
		PublishEvent parsed;
		if (!ParseEventPayload(record.payload, parsed) || parsed.id != record.event.id ||
			BuildJsonObject(record.event) != record.payload)
		{
			error = "Publish-job payload is malformed or does not match its event.";
			return false;
		}
		return true;
	}

	bool ReadBoundedFile(const std::string& path, std::string& contents, std::string& error)
	{
		WIN32_FILE_ATTRIBUTE_DATA attributes = {};
		if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attributes))
		{
			SetWindowsError("Publish-job file could not be inspected", error);
			return false;
		}
		if (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			error = "Publish-job path is a directory.";
			return false;
		}
		ULARGE_INTEGER size;
		size.HighPart = attributes.nFileSizeHigh;
		size.LowPart = attributes.nFileSizeLow;
		if (size.QuadPart == 0 || size.QuadPart > PUBLISH_JOB_MAX_FILE_BYTES)
		{
			error = "Publish-job file is empty or exceeds the size limit.";
			return false;
		}
		std::ifstream input(path.c_str(), std::ios::binary);
		if (!input)
		{
			error = "Publish-job file could not be opened.";
			return false;
		}
		contents.assign(static_cast<std::size_t>(size.QuadPart), '\0');
		input.read(&contents[0], static_cast<std::streamsize>(contents.size()));
		if (!input || static_cast<std::size_t>(input.gcount()) != contents.size())
		{
			error = "Publish-job file could not be read completely.";
			contents.clear();
			return false;
		}
		return true;
	}

	bool WriteAll(HANDLE file, const std::string& contents)
	{
		std::size_t offset = 0;
		while (offset < contents.size())
		{
			const std::size_t remaining = contents.size() - offset;
			const DWORD requested = remaining > static_cast<std::size_t>(MAXDWORD) ?
				MAXDWORD : static_cast<DWORD>(remaining);
			DWORD written = 0;
			if (!WriteFile(file, contents.data() + offset, requested, &written, NULL) ||
				written == 0) return false;
			offset += written;
		}
		return true;
	}

	bool SetFileLength(HANDLE file, LONGLONG length)
	{
		LARGE_INTEGER position;
		position.QuadPart = length;
		return SetFilePointerEx(file, position, NULL, FILE_BEGIN) != FALSE &&
			SetEndOfFile(file) != FALSE;
	}

	bool FindCompleteHistoryLength(HANDLE file, LONGLONG length, LONGLONG& completeLength)
	{
		completeLength = 0;
		if (length <= 0) return true;
		const DWORD blockSize = 4096;
		std::vector<char> block(blockSize);
		LONGLONG scanEnd = length;
		while (scanEnd > 0)
		{
			const LONGLONG scanStart = scanEnd > blockSize ? scanEnd - blockSize : 0;
			const DWORD requested = static_cast<DWORD>(scanEnd - scanStart);
			LARGE_INTEGER position;
			position.QuadPart = scanStart;
			if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN)) return false;
			DWORD read = 0;
			if (!ReadFile(file, &block[0], requested, &read, NULL) || read != requested)
				return false;
			for (DWORD index = read; index > 0; --index)
			{
				if (block[index - 1] == '\n')
				{
					completeLength = scanStart + index;
					return true;
				}
			}
			scanEnd = scanStart;
		}
		return true;
	}
}

bool PendingPublishJobIds::Add(const std::string& id)
{
	return ids_.insert(id).second;
}

void PendingPublishJobIds::Remove(const std::string& id)
{
	ids_.erase(id);
}

void PendingPublishJobIds::Clear()
{
	ids_.clear();
}

std::size_t PendingPublishJobIds::Size() const
{
	return ids_.size();
}

bool IsSafePublishJobId(const std::string& id)
{
	if (id.empty() || id.size() > MAX_ID_BYTES) return false;
	for (std::string::const_iterator character = id.begin(); character != id.end(); ++character)
	{
		const unsigned char value = static_cast<unsigned char>(*character);
		if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || value == '-' || value == '_')) return false;
	}
	return true;
}

bool ParseCanonicalPublishEventJson(const std::string& payload,
	PublishEvent& event, std::string& error)
{
	event = PublishEvent();
	error.clear();
	if (payload.empty() || payload.size() >= PUBLISH_JOB_MAX_FILE_BYTES)
	{
		error = "Publishing event JSON is empty or too large.";
		return false;
	}
	if (!ParseEventPayload(payload, event) || !IsSafePublishJobId(event.id))
	{
		error = "Publishing event JSON is malformed.";
		return false;
	}
	if (BuildJsonObject(event) != payload)
	{
		error = "Publishing event JSON is not in canonical form.";
		return false;
	}
	return true;
}

bool LoadPublishHistoryJsonLines(const std::string& path, std::size_t maxEvents,
	std::vector<PublishEvent>& events, std::size_t& rejectedLines, std::string& error)
{
	events.clear();
	rejectedLines = 0;
	error.clear();
	if (!maxEvents) return true;

	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attributes))
	{
		const DWORD code = GetLastError();
		if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
		SetWindowsError("Publishing history could not be inspected", error);
		return false;
	}
	if (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		error = "Publishing history path is a directory.";
		return false;
	}
	ULARGE_INTEGER size;
	size.HighPart = attributes.nFileSizeHigh;
	size.LowPart = attributes.nFileSizeLow;
	if (!size.QuadPart) return true;

	const ULONGLONG bytesToRead = size.QuadPart < PUBLISH_HISTORY_MAX_READ_BYTES ?
		size.QuadPart : static_cast<ULONGLONG>(PUBLISH_HISTORY_MAX_READ_BYTES);
	const ULONGLONG offset = size.QuadPart - bytesToRead;
	if (offset > static_cast<ULONGLONG>((std::numeric_limits<std::streamoff>::max)()))
	{
		error = "Publishing history is too large to seek safely.";
		return false;
	}
	std::ifstream input(path.c_str(), std::ios::binary);
	if (!input)
	{
		error = "Publishing history could not be opened.";
		return false;
	}
	if (offset)
	{
		input.seekg(static_cast<std::streamoff>(offset - 1u), std::ios::beg);
		char previous = 0;
		input.get(previous);
		if (!input)
		{
			error = "Publishing history could not be positioned.";
			return false;
		}
		if (previous != '\n')
		{
			std::string partial;
			std::getline(input, partial);
		}
	}

	std::string line;
	while (std::getline(input, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		PublishEvent event;
		std::string parseError;
		if (!ParseCanonicalPublishEventJson(line, event, parseError))
		{
			++rejectedLines;
			continue;
		}
		for (std::vector<PublishEvent>::iterator existing = events.begin();
			existing != events.end(); ++existing)
		{
			if (existing->id == event.id)
			{
				events.erase(existing);
				break;
			}
		}
		events.insert(events.begin(), event);
		if (events.size() > maxEvents) events.resize(maxEvents);
	}
	if (!input.eof())
	{
		error = "Publishing history could not be read completely.";
		events.clear();
		return false;
	}
	return true;
}

bool PublishJobAttemptLimitReached(unsigned int attempts)
{
	return attempts >= PUBLISH_JOB_MAX_ATTEMPTS;
}

std::string PublishJobFileName(const std::string& id)
{
	return id + ".pdwjob";
}

std::string PublishJobBaseName(const std::string& path)
{
	const std::size_t separator = path.find_last_of("\\/");
	return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool SerializePublishJob(const PublishJobRecord& record, std::string& serialized,
	std::string& error)
{
	serialized.clear();
	error.clear();
	if (!ValidateRecord(record, false, error)) return false;
	std::ostringstream header;
	header << JOB_MAGIC_V2 << '\n'
		<< "id=" << record.event.id << '\n'
		<< "targets=" << record.targets << '\n'
		<< "completed=" << record.completed << '\n'
		<< "failed=" << record.failed << '\n'
		<< "static-attempts=" << record.staticAttempts << '\n'
		<< "webhook-attempts=" << record.webhookAttempts << '\n'
		<< "static-output-path-bytes=" << record.staticOutputPath.size() << '\n'
		<< "payload-bytes=" << record.payload.size() << "\n\n";
	serialized = header.str();
	serialized += record.staticOutputPath;
	serialized += record.payload;
	if (serialized.size() > PUBLISH_JOB_MAX_FILE_BYTES)
	{
		serialized.clear();
		error = "Publish-job record exceeds the size limit.";
		return false;
	}
	return true;
}

bool ParsePublishJob(const std::string& serialized, PublishJobRecord& record,
	std::string& error)
{
	record = PublishJobRecord();
	error.clear();
	if (serialized.empty() || serialized.size() > PUBLISH_JOB_MAX_FILE_BYTES)
	{
		error = "Publish-job record is empty or exceeds the size limit.";
		return false;
	}
	std::size_t position = 0;
	std::string line;
	if (!ReadLine(serialized, position, line) ||
		(line != JOB_MAGIC_V1 && line != JOB_MAGIC_V2))
	{
		error = "Publish-job record has an unsupported format or version.";
		return false;
	}
	const bool version1 = line == JOB_MAGIC_V1;
	std::string id, targets, completed, failed, staticAttempts, webhookAttempts;
	std::string staticPathBytes, attempts, payloadBytes;
	bool headerValid = ReadHeaderValue(serialized, position, "id", id) &&
		ReadHeaderValue(serialized, position, "targets", targets) &&
		ReadHeaderValue(serialized, position, "completed", completed);
	if (version1)
	{
		headerValid = headerValid &&
			ReadHeaderValue(serialized, position, "attempts", attempts);
	}
	else
	{
		headerValid = headerValid &&
			ReadHeaderValue(serialized, position, "failed", failed) &&
			ReadHeaderValue(serialized, position, "static-attempts", staticAttempts) &&
			ReadHeaderValue(serialized, position, "webhook-attempts", webhookAttempts) &&
			ReadHeaderValue(serialized, position, "static-output-path-bytes", staticPathBytes);
	}
	headerValid = headerValid &&
		ReadHeaderValue(serialized, position, "payload-bytes", payloadBytes) &&
		ReadLine(serialized, position, line) && line.empty();
	if (!headerValid)
	{
		error = "Publish-job header is malformed.";
		return false;
	}
	unsigned int payloadLength = 0;
	if (!IsSafePublishJobId(id) || !ParseUnsigned(targets, record.targets) ||
		!ParseUnsigned(completed, record.completed) ||
		!ParseUnsigned(payloadBytes, payloadLength))
	{
		error = "Publish-job metadata is invalid.";
		return false;
	}
	if (version1)
	{
		if (!ParseUnsigned(attempts, record.attempts) ||
			payloadLength != serialized.size() - position ||
			record.attempts > PUBLISH_JOB_MAX_ATTEMPTS)
		{
			error = "Publish-job version-1 metadata is invalid.";
			return false;
		}
		record.staticAttempts = (record.targets & PUBLISH_JOB_TARGET_STATIC) ? record.attempts : 0;
		record.webhookAttempts = (record.targets & PUBLISH_JOB_TARGET_WEBHOOK) ? record.attempts : 0;
		if (record.attempts == PUBLISH_JOB_MAX_ATTEMPTS)
			record.failed = record.targets & ~record.completed;
	}
	else
	{
		unsigned int staticPathLength = 0;
		if (!ParseUnsigned(failed, record.failed) ||
			!ParseUnsigned(staticAttempts, record.staticAttempts) ||
			!ParseUnsigned(webhookAttempts, record.webhookAttempts) ||
			!ParseUnsigned(staticPathBytes, staticPathLength) ||
			staticPathLength > PUBLISH_JOB_MAX_STATIC_PATH_BYTES ||
			staticPathLength > serialized.size() - position ||
			payloadLength != serialized.size() - position - staticPathLength)
		{
			error = "Publish-job version-2 metadata is invalid.";
			return false;
		}
		record.staticOutputPath.assign(serialized, position, staticPathLength);
		position += staticPathLength;
		record.attempts = record.staticAttempts > record.webhookAttempts ?
			record.staticAttempts : record.webhookAttempts;
	}
	record.payload.assign(serialized, position, payloadLength);
	if (!ParseEventPayload(record.payload, record.event) || record.event.id != id)
	{
		error = "Publish-job payload is malformed or its event ID changed.";
		return false;
	}
	if (!ValidateRecord(record, version1, error)) return false;
	record.legacyPayloadOnly = false;
	return true;
}

bool PublishJobRecoveryCandidateAdvances(const PublishJobRecord& finalRecord,
	const PublishJobRecord& candidate)
{
	if (candidate.event.id != finalRecord.event.id ||
		candidate.payload != finalRecord.payload ||
		candidate.targets != finalRecord.targets)
		return false;
	if (!finalRecord.staticOutputPath.empty() &&
		candidate.staticOutputPath != finalRecord.staticOutputPath)
		return false;
	if ((candidate.completed & finalRecord.completed) != finalRecord.completed ||
		(candidate.failed & finalRecord.failed) != finalRecord.failed ||
		candidate.staticAttempts < finalRecord.staticAttempts ||
		candidate.webhookAttempts < finalRecord.webhookAttempts)
		return false;
	return candidate.completed != finalRecord.completed ||
		candidate.failed != finalRecord.failed ||
		candidate.staticAttempts != finalRecord.staticAttempts ||
		candidate.webhookAttempts != finalRecord.webhookAttempts;
}

bool LoadPublishJobFile(const std::string& path, bool allowLegacyPayload,
	PublishJobRecord& record, std::string& error)
{
	std::string contents;
	if (!ReadBoundedFile(path, contents, error)) return false;
	const std::string magicPrefix = "PDW-PUBLISH-JOB ";
	if (contents.compare(0, magicPrefix.size(), magicPrefix) == 0)
		return ParsePublishJob(contents, record, error);
	if (!allowLegacyPayload)
	{
		error = "Publish-job file is not a supported versioned record.";
		return false;
	}
	record = PublishJobRecord();
	record.payload = contents;
	if (!ParseEventPayload(record.payload, record.event) || !IsSafePublishJobId(record.event.id))
	{
		error = "Legacy publish-job payload is malformed.";
		return false;
	}
	record.legacyPayloadOnly = true;
	return true;
}

bool SavePublishJobFileAtomic(const std::string& path,
	const PublishJobRecord& record, std::string& error)
{
	std::string serialized;
	if (!SerializePublishJob(record, serialized, error)) return false;
	const std::string temporary = path + ".tmp";
	DeleteFileA(temporary.c_str());
	HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		SetWindowsError("Publish-job temporary file could not be created", error);
		return false;
	}
	DWORD written = 0;
	const DWORD bytesToWrite = static_cast<DWORD>(serialized.size());
	const bool wrote = WriteFile(file, serialized.data(), bytesToWrite,
		&written, NULL) != FALSE && written == bytesToWrite && FlushFileBuffers(file) != FALSE;
	if (!CloseHandle(file) || !wrote)
	{
		if (wrote) SetWindowsError("Publish-job temporary file could not be closed", error);
		else SetWindowsError("Publish-job temporary file could not be written", error);
		DeleteFileA(temporary.c_str());
		return false;
	}
	if (!MoveFileExA(temporary.c_str(), path.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		SetWindowsError("Publish-job file could not be replaced atomically", error);
		DeleteFileA(temporary.c_str());
		return false;
	}
	return true;
}

bool DeletePublishJobFile(const std::string& path, std::string& error)
{
	error.clear();
	if (path.empty()) return true;
	if (DeleteFileA(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
	SetWindowsError("Publish-job file could not be deleted", error);
	return false;
}

bool MovePublishJobFile(const std::string& source, const std::string& destination,
	std::string& error)
{
	error.clear();
	if (source.empty() || destination.empty())
	{
		error = "Publish-job move path is empty.";
		return false;
	}
	if (MoveFileExA(source.c_str(), destination.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
	SetWindowsError("Publish-job file could not be moved", error);
	return false;
}

bool PromotePublishJobTempFile(const std::string& temporary,
	const std::string& finalPath, std::string& error)
{
	error.clear();
	if (temporary.empty() || finalPath.empty())
	{
		error = "Publish-job promotion path is empty.";
		return false;
	}
	const DWORD finalAttributes = GetFileAttributesA(finalPath.c_str());
	if (finalAttributes == INVALID_FILE_ATTRIBUTES)
	{
		const DWORD code = GetLastError();
		if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
		{
			SetLastError(code);
			SetWindowsError("Publish-job final file could not be inspected", error);
			return false;
		}
		if (MoveFileExA(temporary.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
		SetWindowsError("Publish-job temporary file could not be promoted", error);
		return false;
	}

	PublishJobRecord finalRecord;
	PublishJobRecord candidate;
	if (!LoadPublishJobFile(finalPath, false, finalRecord, error) ||
		!LoadPublishJobFile(temporary, false, candidate, error)) return false;
	if (!PublishJobRecoveryCandidateAdvances(finalRecord, candidate))
	{
		error = "Publish-job temporary file is equal, conflicting, or not a monotonic advancement.";
		return false;
	}
	if (MoveFileExA(temporary.c_str(), finalPath.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
	SetWindowsError("Publish-job advancing temporary file could not replace the final file", error);
	return false;
}

bool WritePublishStaticFileAtomic(const std::string& path,
	const std::string& contents, std::string& error)
{
	error.clear();
	if (path.empty())
	{
		error = "Publishing static-file path is empty.";
		return false;
	}
	const std::string temporary = path + ".tmp";
	DeleteFileA(temporary.c_str());
	HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		SetWindowsError("Publishing static temporary file could not be created", error);
		return false;
	}
	bool ok = WriteAll(file, contents) && FlushFileBuffers(file) != FALSE;
	DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
	if (!CloseHandle(file))
	{
		if (ok) failure = GetLastError();
		ok = false;
	}
	if (!ok)
	{
		DeleteFileA(temporary.c_str());
		SetLastError(failure);
		SetWindowsError("Publishing static temporary file could not be written durably", error);
		return false;
	}
	if (!MoveFileExA(temporary.c_str(), path.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		SetWindowsError("Publishing static file could not be replaced atomically", error);
		DeleteFileA(temporary.c_str());
		return false;
	}
	return true;
}

bool AppendPublishHistoryJsonLineDurable(const std::string& path,
	const std::string& canonicalJsonLine, std::string& error)
{
	error.clear();
	PublishEvent event;
	if (path.empty() || canonicalJsonLine.find('\n') != std::string::npos ||
		canonicalJsonLine.find('\r') != std::string::npos ||
		!ParseCanonicalPublishEventJson(canonicalJsonLine, event, error))
	{
		if (error.empty()) error = "Publishing history path or JSON line is invalid.";
		return false;
	}
	const std::string contents = canonicalJsonLine + "\n";
	HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		SetWindowsError("Publishing history file could not be opened", error);
		return false;
	}
	LARGE_INTEGER size;
	bool ok = GetFileSizeEx(file, &size) != FALSE;
	LONGLONG stableLength = 0;
	bool stableKnown = false;
	if (ok)
	{
		ok = FindCompleteHistoryLength(file, size.QuadPart, stableLength);
		stableKnown = ok;
	}
	if (ok && stableLength != size.QuadPart)
		ok = SetFileLength(file, stableLength) && FlushFileBuffers(file) != FALSE;
	if (ok)
	{
		LARGE_INTEGER position;
		position.QuadPart = stableLength;
		ok = SetFilePointerEx(file, position, NULL, FILE_BEGIN) != FALSE;
	}
	if (ok) ok = WriteAll(file, contents) && FlushFileBuffers(file) != FALSE;
	DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
	if (!ok && stableKnown)
	{
		SetFileLength(file, stableLength);
		FlushFileBuffers(file);
	}
	if (!CloseHandle(file))
	{
		if (ok) failure = GetLastError();
		ok = false;
	}
	if (!ok)
	{
		SetLastError(failure);
		SetWindowsError("Publishing history line could not be appended durably", error);
	}
	return ok;
}

} // namespace publishing
} // namespace pdw
