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

namespace pdw
{
namespace ini
{
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
		DWORD& attributes, std::string& error)
	{
		contents.clear();
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
		std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
		if (!input)
		{
			error = "The existing INI file could not be opened for preservation.";
			return false;
		}
		const std::streamoff length = input.tellg();
		if (length < 0 || static_cast<unsigned long long>(length) > MAX_INI_BYTES)
		{
			error = "The existing INI file is too large to merge safely.";
			return false;
		}
		contents.assign(static_cast<std::size_t>(length), '\0');
		input.seekg(0, std::ios::beg);
		if (length > 0) input.read(&contents[0], length);
		if (!input && length > 0)
		{
			error = "The existing INI file could not be read completely.";
			return false;
		}
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

bool WriteMergedSettingsFile(const std::string& path,
	const std::string& generated, std::string& error)
{
	error.clear();
	if (path.empty() || generated.empty() || generated.size() > MAX_INI_BYTES)
	{
		error = "The generated INI settings are empty or too large.";
		return false;
	}

	char fullPath[MAX_PATH] = {};
	const DWORD fullLength = GetFullPathNameA(path.c_str(),
		static_cast<DWORD>(_countof(fullPath)), fullPath, NULL);
	if (!fullLength)
	{
		error = WindowsError("Resolving INI path", GetLastError());
		return false;
	}
	if (fullLength >= _countof(fullPath))
	{
		error = "The INI path is too long for this PDW build.";
		return false;
	}
	std::string existing;
	bool exists = false;
	DWORD attributes = INVALID_FILE_ATTRIBUTES;
	if (!ReadExistingFile(fullPath, existing, exists, attributes, error)) return false;
	const std::string merged = MergeKnownSettings(existing, generated);
	if (merged.empty())
	{
		error = "The merged INI settings would be empty.";
		return false;
	}

	std::string directory(fullPath);
	const std::size_t slash = directory.find_last_of("\\/");
	if (slash == std::string::npos) directory = ".";
	else if (slash == 2 && directory.size() >= 3 && directory[1] == ':')
		directory = directory.substr(0, 3);
	else directory = directory.substr(0, slash);
	char temporary[MAX_PATH] = {};
	if (!GetTempFileNameA(directory.c_str(), "PDW", 0, temporary))
	{
		error = WindowsError("Creating INI temporary name", GetLastError());
		return false;
	}

	HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		error = WindowsError("Opening INI temporary file", GetLastError());
		DeleteFileA(temporary);
		return false;
	}
	const bool wrote = WriteAll(file, merged, error);
	CloseHandle(file);
	if (!wrote)
	{
		DeleteFileA(temporary);
		return false;
	}

	const bool wasReadOnly = exists && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
	if (wasReadOnly && !SetFileAttributesA(fullPath, attributes & ~FILE_ATTRIBUTE_READONLY))
	{
		error = WindowsError("Clearing read-only INI attribute", GetLastError());
		DeleteFileA(temporary);
		return false;
	}

	BOOL replaced = exists ? ReplaceFileA(fullPath, temporary, NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) :
		MoveFileExA(temporary, fullPath, MOVEFILE_WRITE_THROUGH);
	if (!replaced)
	{
		const DWORD code = GetLastError();
		DeleteFileA(temporary);
		if (wasReadOnly) SetFileAttributesA(fullPath, attributes);
		error = WindowsError("Replacing INI file", code);
		return false;
	}
	if (exists) SetFileAttributesA(fullPath, attributes & ~FILE_ATTRIBUTE_READONLY);
	return true;
}

} // namespace ini
} // namespace pdw
