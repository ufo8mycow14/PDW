#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ini_preservation_core.h"
#include "local_audio_profile_core.h"

namespace pdw
{
namespace ini
{
#ifdef PDW_INI_PRESERVATION_TEST_HOOKS
namespace
{
	BeforeMissingIniCommitTestHook g_beforeMissingIniCommitTestHook = NULL;
	BeforeExistingIniCommitTestHook g_beforeExistingIniCommitTestHook = NULL;
}

void SetBeforeMissingIniCommitTestHook(BeforeMissingIniCommitTestHook hook)
{
	g_beforeMissingIniCommitTestHook = hook;
}

void SetBeforeExistingIniCommitTestHook(BeforeExistingIniCommitTestHook hook)
{
	g_beforeExistingIniCommitTestHook = hook;
}
#endif

namespace
{
	const std::size_t MAX_INI_BYTES = 16u * 1024u * 1024u;

	struct Definition
	{
		std::string name;
		std::string sectionKey;
		std::vector<std::string> keyOrder;
		std::map<std::string, std::string> linesByKey;
	};

	struct Chunk
	{
		bool hasSection;
		std::string header;
		std::string sectionKey;
		std::vector<std::string> body;
		Chunk() : hasSection(false) {}
	};

	std::string Trim(const std::string& value)
	{
		std::size_t first = 0;
		while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
		std::size_t last = value.size();
		while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
		return value.substr(first, last - first);
	}

	std::string LowerAscii(const std::string& value)
	{
		std::string result(value);
		for (std::string::iterator character = result.begin(); character != result.end(); ++character)
			*character = static_cast<char>(std::tolower(static_cast<unsigned char>(*character)));
		return result;
	}

	bool ParseSection(const std::string& line, std::string& name)
	{
		const std::string trimmed = Trim(line);
		if (trimmed.size() < 3 || trimmed[0] != '[') return false;
		const std::size_t closing = trimmed.find(']');
		if (closing == std::string::npos) return false;
		const std::string remainder = Trim(trimmed.substr(closing + 1));
		if (!remainder.empty() && remainder[0] != ';' && remainder[0] != '#') return false;
		name = Trim(trimmed.substr(1, closing - 1));
		return !name.empty();
	}

	bool ParseKey(const std::string& line, std::string& key)
	{
		const std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') return false;
		const std::size_t separator = trimmed.find('=');
		if (separator == std::string::npos) return false;
		key = Trim(trimmed.substr(0, separator));
		return !key.empty();
	}

	std::vector<std::string> SplitLines(const std::string& text, bool& trailingNewline)
	{
		std::vector<std::string> lines;
		trailingNewline = !text.empty() && text[text.size() - 1] == '\n';
		std::size_t start = 0;
		while (start < text.size())
		{
			const std::size_t newline = text.find('\n', start);
			const std::size_t end = newline == std::string::npos ? text.size() : newline;
			std::string line = text.substr(start, end - start);
			if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
			lines.push_back(line);
			if (newline == std::string::npos) break;
			start = newline + 1;
		}
		return lines;
	}

	std::string DetectNewline(const std::string& text)
	{
		const std::size_t newline = text.find('\n');
		if (newline != std::string::npos && newline > 0 && text[newline - 1] == '\r') return "\r\n";
		return "\n";
	}

	std::vector<Definition> ParseDefinitions(const std::vector<std::string>& lines)
	{
		std::vector<Definition> definitions;
		std::map<std::string, std::size_t> indexes;
		Definition* current = NULL;
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			std::string section;
			if (ParseSection(lines[index], section))
			{
				const std::string sectionKey = LowerAscii(section);
				std::map<std::string, std::size_t>::const_iterator found = indexes.find(sectionKey);
				if (found == indexes.end())
				{
					Definition definition;
					definition.name = section;
					definition.sectionKey = sectionKey;
					definitions.push_back(definition);
					indexes[sectionKey] = definitions.size() - 1;
					current = &definitions.back();
				}
				else current = &definitions[found->second];
				continue;
			}
			if (!current) continue;
			std::string key;
			if (!ParseKey(lines[index], key)) continue;
			const std::string normalized = LowerAscii(key);
			if (current->linesByKey.find(normalized) == current->linesByKey.end())
				current->keyOrder.push_back(normalized);
			current->linesByKey[normalized] = lines[index];
		}
		return definitions;
	}

	std::vector<Chunk> ParseChunks(const std::vector<std::string>& lines)
	{
		std::vector<Chunk> chunks(1);
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			std::string section;
			if (ParseSection(lines[index], section))
			{
				Chunk chunk;
				chunk.hasSection = true;
				chunk.header = lines[index];
				chunk.sectionKey = LowerAscii(section);
				chunks.push_back(chunk);
			}
			else chunks.back().body.push_back(lines[index]);
		}
		return chunks;
	}

	bool DecimalSuffix(const std::string& value, std::size_t first)
	{
		if (first >= value.size()) return false;
		for (std::size_t index = first; index < value.size(); ++index)
			if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
		return true;
	}

	bool RemoveWhenMissing(const std::string& sectionKey, const std::string& key)
	{
		// These are intentionally optional/dynamic values owned by PDW. Keeping a
		// missing old value would undo a user's reset-to-default or retain removed
		// upload paths. All other unknown keys remain untouched.
		if (sectionKey == "pdw" && key == "logfilepath") return true;
		if (sectionKey == "ftp" && key.size() > 4 && key.compare(0, 4, "file") == 0)
			return DecimalSuffix(key, 4);
		return false;
	}

	std::string JoinLines(const std::vector<std::string>& lines, const std::string& newline,
		bool trailingNewline, const std::string& prefix)
	{
		std::ostringstream output;
		output << prefix;
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			if (index) output << newline;
			output << lines[index];
		}
		if (trailingNewline && !lines.empty()) output << newline;
		return output.str();
	}

	std::string WindowsError(const char* operation, DWORD code)
	{
		std::ostringstream error;
		error << operation << " failed with Windows error " << static_cast<unsigned long>(code) << ".";
		return error.str();
	}

	bool ReadExistingFile(const std::string& path, std::string& contents, bool& exists,
		DWORD& attributes, DWORD& volumeSerial, DWORD& fileIndexHigh,
		DWORD& fileIndexLow, std::string& error)
	{
		contents.clear();
		volumeSerial = fileIndexHigh = fileIndexLow = 0;
		attributes = GetFileAttributesA(path.c_str());
		exists = attributes != INVALID_FILE_ATTRIBUTES;
		if (!exists)
		{
			const DWORD code = GetLastError();
			if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
			error = WindowsError("Reading INI attributes", code);
			return false;
		}
		if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			error = "The INI path refers to a directory.";
			return false;
		}
		HANDLE input = CreateFileA(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | READ_CONTROL,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (input == INVALID_HANDLE_VALUE)
		{
			error = WindowsError("Pinning the existing INI for preservation", GetLastError());
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		LARGE_INTEGER length = {};
		if (!GetFileInformationByHandle(input, &information) ||
			!GetFileSizeEx(input, &length) || length.QuadPart < 0 ||
			static_cast<unsigned long long>(length.QuadPart) > MAX_INI_BYTES)
		{
			CloseHandle(input);
			error = "The existing INI file is too large to merge safely.";
			return false;
		}
		attributes = information.dwFileAttributes;
		volumeSerial = information.dwVolumeSerialNumber;
		fileIndexHigh = information.nFileIndexHigh;
		fileIndexLow = information.nFileIndexLow;
		contents.assign(static_cast<std::size_t>(length.QuadPart), '\0');
		std::size_t total = 0;
		while (total < contents.size())
		{
			const DWORD requested = static_cast<DWORD>((std::min)(contents.size() - total,
				static_cast<std::size_t>(0x7fffffff)));
			DWORD read = 0;
			if (!ReadFile(input, &contents[total], requested, &read, NULL) || !read)
			{
				CloseHandle(input);
				error = WindowsError("Reading the pinned existing INI", GetLastError());
				return false;
			}
			total += read;
		}
		CloseHandle(input);
		return true;
	}

	bool WriteAll(HANDLE file, const std::string& contents, std::string& error)
	{
		std::size_t writtenTotal = 0;
		while (writtenTotal < contents.size())
		{
			const DWORD remaining = static_cast<DWORD>((std::min)(contents.size() - writtenTotal,
				static_cast<std::size_t>(0x7fffffff)));
			DWORD written = 0;
			if (!WriteFile(file, contents.data() + writtenTotal, remaining, &written, NULL) || !written)
			{
				error = WindowsError("Writing merged INI", GetLastError());
				return false;
			}
			writtenTotal += written;
		}
		if (!FlushFileBuffers(file))
		{
			error = WindowsError("Flushing merged INI", GetLastError());
			return false;
		}
		return true;
	}
}

bool CreateSensitiveSettingsTemporaryFile(const std::string& referencePath,
	const char* prefix, std::string& temporaryPath, void*& nativeHandle,
	std::string& error)
{
	return pdw::audio_profile::CreateSecureTemporarySettingsFile(referencePath,
		prefix, temporaryPath, nativeHandle, error);
}

bool MarkSensitiveSettingsTemporaryFileForDeletion(void* nativeHandle,
	const std::string& path, std::string& error)
{
	return pdw::audio_profile::MarkSensitiveTemporaryFileForDeletion(nativeHandle,
		path, error);
}

std::string MergeKnownSettings(const std::string& existingInput,
	const std::string& generatedInput)
{
	std::string existing(existingInput);
	std::string generated(generatedInput);
	std::string prefix;
	if (existing.size() >= 3 && static_cast<unsigned char>(existing[0]) == 0xef &&
		static_cast<unsigned char>(existing[1]) == 0xbb &&
		static_cast<unsigned char>(existing[2]) == 0xbf)
	{
		prefix.assign(existing, 0, 3);
		existing.erase(0, 3);
	}
	else if (generated.size() >= 3 && static_cast<unsigned char>(generated[0]) == 0xef &&
		static_cast<unsigned char>(generated[1]) == 0xbb &&
		static_cast<unsigned char>(generated[2]) == 0xbf)
	{
		prefix.assign(generated, 0, 3);
		generated.erase(0, 3);
	}

	bool existingTrailing = false;
	bool generatedTrailing = false;
	const std::vector<std::string> existingLines = SplitLines(existing, existingTrailing);
	const std::vector<std::string> generatedLines = SplitLines(generated, generatedTrailing);
	const std::vector<Definition> definitions = ParseDefinitions(generatedLines);
	std::vector<Chunk> chunks = ParseChunks(existingLines);

	std::map<std::string, const Definition*> definitionsBySection;
	for (std::size_t index = 0; index < definitions.size(); ++index)
		definitionsBySection[definitions[index].sectionKey] = &definitions[index];

	std::map<std::string, std::size_t> lastChunk;
	for (std::size_t index = 0; index < chunks.size(); ++index)
		if (chunks[index].hasSection && definitionsBySection.count(chunks[index].sectionKey))
			lastChunk[chunks[index].sectionKey] = index;

	std::map<std::string, std::set<std::string> > writtenKeys;
	std::set<std::string> encounteredSections;
	std::vector<std::string> outputLines;
	for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex)
	{
		Chunk& chunk = chunks[chunkIndex];
		if (chunk.hasSection) outputLines.push_back(chunk.header);
		std::map<std::string, const Definition*>::const_iterator found =
			definitionsBySection.find(chunk.sectionKey);
		if (!chunk.hasSection || found == definitionsBySection.end())
		{
			outputLines.insert(outputLines.end(), chunk.body.begin(), chunk.body.end());
			continue;
		}

		encounteredSections.insert(chunk.sectionKey);
		const Definition& definition = *found->second;
		std::vector<std::string> body;
		for (std::size_t lineIndex = 0; lineIndex < chunk.body.size(); ++lineIndex)
		{
			std::string key;
			if (!ParseKey(chunk.body[lineIndex], key))
			{
				body.push_back(chunk.body[lineIndex]);
				continue;
			}
			const std::string normalized = LowerAscii(key);
			std::map<std::string, std::string>::const_iterator replacement =
				definition.linesByKey.find(normalized);
			if (replacement != definition.linesByKey.end())
			{
				if (writtenKeys[chunk.sectionKey].insert(normalized).second)
					body.push_back(replacement->second);
				continue;
			}
			if (RemoveWhenMissing(chunk.sectionKey, normalized)) continue;
			body.push_back(chunk.body[lineIndex]);
		}

		if (lastChunk[chunk.sectionKey] == chunkIndex)
		{
			std::vector<std::string> missing;
			for (std::size_t keyIndex = 0; keyIndex < definition.keyOrder.size(); ++keyIndex)
			{
				const std::string& key = definition.keyOrder[keyIndex];
				if (writtenKeys[chunk.sectionKey].insert(key).second)
					missing.push_back(definition.linesByKey.find(key)->second);
			}
			std::size_t insertion = body.size();
			while (insertion > 0 && Trim(body[insertion - 1]).empty()) --insertion;
			body.insert(body.begin() + insertion, missing.begin(), missing.end());
		}
		outputLines.insert(outputLines.end(), body.begin(), body.end());
	}

	for (std::size_t index = 0; index < definitions.size(); ++index)
	{
		const Definition& definition = definitions[index];
		if (encounteredSections.count(definition.sectionKey)) continue;
		if (!outputLines.empty() && !Trim(outputLines.back()).empty()) outputLines.push_back("");
		outputLines.push_back("[" + definition.name + "]");
		for (std::size_t keyIndex = 0; keyIndex < definition.keyOrder.size(); ++keyIndex)
			outputLines.push_back(definition.linesByKey.find(definition.keyOrder[keyIndex])->second);
	}

	const std::string newline = existing.empty() ? DetectNewline(generated) : DetectNewline(existing);
	const bool trailing = existing.empty() ? generatedTrailing : existingTrailing;
	return JoinLines(outputLines, newline, trailing, prefix);
}

pdw::audio_profile::SettingsTransactionOutcome WriteMergedSettingsFile(
	const std::string& path,
	const std::string& generated, std::string& error)
{
	error.clear();
	if (path.empty() || generated.empty() || generated.size() > MAX_INI_BYTES)
	{
		error = "The generated INI settings are empty or too large.";
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}

	char fullPath[MAX_PATH] = {};
	const DWORD fullLength = GetFullPathNameA(path.c_str(),
		static_cast<DWORD>(_countof(fullPath)), fullPath, NULL);
	if (!fullLength)
	{
		error = WindowsError("Resolving INI path", GetLastError());
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	if (fullLength >= _countof(fullPath))
	{
		error = "The INI path is too long for this PDW build.";
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	std::string existing;
	bool exists = false;
	DWORD attributes = INVALID_FILE_ATTRIBUTES;
	DWORD volumeSerial = 0;
	DWORD fileIndexHigh = 0;
	DWORD fileIndexLow = 0;
	if (!ReadExistingFile(fullPath, existing, exists, attributes, volumeSerial,
		fileIndexHigh, fileIndexLow, error)) return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	const std::string merged = MergeKnownSettings(existing, generated);
	if (merged.empty())
	{
		error = "The merged INI settings would be empty.";
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}

	if (exists)
	{
#ifdef PDW_INI_PRESERVATION_TEST_HOOKS
		if (g_beforeExistingIniCommitTestHook)
			g_beforeExistingIniCommitTestHook(fullPath);
#endif
		const pdw::audio_profile::SettingsTransactionOutcome outcome =
			pdw::audio_profile::CommitVerifiedFileTransactionForIdentity(fullPath,
				existing, merged, volumeSerial, fileIndexHigh, fileIndexLow, error);
		// The shared transaction clears a legacy read-only bit only through the
		// already verified file handle. A pathname swap can therefore never cause
		// PDW to mutate an operator's replacement file by name.
		return outcome;
	}

	std::string temporary;
	void* temporaryNativeHandle = INVALID_HANDLE_VALUE;
	if (!CreateSensitiveSettingsTemporaryFile(fullPath, "PDS", temporary,
		temporaryNativeHandle, error))
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	HANDLE file = static_cast<HANDLE>(temporaryNativeHandle);
	const bool wrote = WriteAll(file, merged, error);
	if (!wrote)
	{
		std::string removalError;
		if (!MarkSensitiveSettingsTemporaryFileForDeletion(file, temporary, removalError))
			error += " " + removalError;
		CloseHandle(file);
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}

	std::string staged;
	LARGE_INTEGER beginning = {};
	LARGE_INTEGER length = {};
	if (!SetFilePointerEx(file, beginning, NULL, FILE_BEGIN) ||
		!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
		static_cast<unsigned long long>(length.QuadPart) > MAX_INI_BYTES)
		error = WindowsError("Reading protected staged INI size", GetLastError());
	else
	{
		staged.assign(static_cast<std::size_t>(length.QuadPart), '\0');
		std::size_t total = 0;
		while (total < staged.size())
		{
			const DWORD requested = static_cast<DWORD>((std::min)(staged.size() - total,
				static_cast<std::size_t>(0x7fffffff)));
			DWORD read = 0;
			if (!ReadFile(file, &staged[total], requested, &read, NULL) || !read)
			{
				error = WindowsError("Reading protected staged INI", GetLastError());
				staged.clear();
				break;
			}
			total += read;
		}
	}
	if (staged != merged)
	{
		std::string removalError;
		if (!MarkSensitiveSettingsTemporaryFileForDeletion(file, temporary, removalError))
			error += " " + removalError;
		CloseHandle(file);
		if (error.empty()) error = "The staged INI did not verify byte-for-byte.";
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	BY_HANDLE_FILE_INFORMATION stagedInformation = {};
	if (!GetFileInformationByHandle(file, &stagedInformation))
	{
		error = WindowsError("Reading protected staged INI identity", GetLastError());
		std::string removalError;
		if (!MarkSensitiveSettingsTemporaryFileForDeletion(file, temporary, removalError))
			error += " " + removalError;
		CloseHandle(file);
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	CloseHandle(file);

#ifdef PDW_INI_PRESERVATION_TEST_HOOKS
	if (g_beforeMissingIniCommitTestHook)
		g_beforeMissingIniCommitTestHook(fullPath, temporary.c_str());
#endif
	BOOL replaced = MoveFileExA(temporary.c_str(), fullPath, MOVEFILE_WRITE_THROUGH);
	if (!replaced)
	{
		const DWORD code = GetLastError();
		error = WindowsError("Creating INI file", code);
		error += " The protected staged settings were retained at \"" + temporary +
			"\"; PDW did not delete a possible last complete settings candidate.";
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	HANDLE committed = CreateFileA(fullPath, GENERIC_READ | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (committed == INVALID_HANDLE_VALUE)
	{
		error = WindowsError("Verifying the newly created INI", GetLastError());
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	BY_HANDLE_FILE_INFORMATION committedInformation = {};
	std::string committedBytes;
	LARGE_INTEGER committedBeginning = {};
	LARGE_INTEGER committedLength = {};
	bool committedVerified = GetFileInformationByHandle(committed, &committedInformation) != FALSE &&
		SetFilePointerEx(committed, committedBeginning, NULL, FILE_BEGIN) != FALSE &&
		GetFileSizeEx(committed, &committedLength) != FALSE &&
		committedLength.QuadPart >= 0 &&
		static_cast<unsigned long long>(committedLength.QuadPart) <= MAX_INI_BYTES;
	if (committedVerified)
	{
		committedBytes.assign(static_cast<std::size_t>(committedLength.QuadPart), '\0');
		std::size_t total = 0;
		while (total < committedBytes.size())
		{
			const DWORD requested = static_cast<DWORD>((std::min)(committedBytes.size() - total,
				static_cast<std::size_t>(0x7fffffff)));
			DWORD read = 0;
			if (!ReadFile(committed, &committedBytes[total], requested, &read, NULL) || !read)
			{
				committedVerified = false;
				break;
			}
			total += read;
		}
	}
	committedVerified = committedVerified && committedBytes == merged &&
		committedInformation.dwVolumeSerialNumber == stagedInformation.dwVolumeSerialNumber &&
		committedInformation.nFileIndexHigh == stagedInformation.nFileIndexHigh &&
		committedInformation.nFileIndexLow == stagedInformation.nFileIndexLow;
	CloseHandle(committed);
	if (!committedVerified)
	{
		error = "The newly created INI did not match the protected staged-file identity; "
			"PDW left the current pathname occupant untouched.";
		return pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	return pdw::audio_profile::SETTINGS_TRANSACTION_COMMITTED;
}

} // namespace ini
} // namespace pdw
