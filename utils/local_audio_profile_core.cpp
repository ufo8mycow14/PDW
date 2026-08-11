#ifndef STRICT
#define STRICT 1
#endif

#include "local_audio_profile_core.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace pdw
{
namespace audio_profile
{

const char* const ADELAIDE_FLEX_PRESET_ID = "sdrsharp-vbcable-adelaide-flex-v1";
const char* const ADELAIDE_FLEX_PRESET_NAME = "SDR# + VB-Audio Cable (Adelaide FLEX)";
const char* const VB_CABLE_CAPTURE_NAME = "CABLE Output (VB-Audio Virtual Cable)";

const std::size_t MAX_PROFILE_ID_UTF8_BYTES = 63;
const std::size_t MAX_PROFILE_NAME_UTF8_BYTES = 127;
const std::size_t MAX_ENDPOINT_ID_UTF8_BYTES = 511;
const std::size_t MAX_ENDPOINT_NAME_UTF8_BYTES = 255;

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
namespace
{
	BeforeReplaceTestHook g_beforeReplaceTestHook = NULL;
	BeforeSecureTempWriteTestHook g_beforeSecureTempWriteTestHook = NULL;
	BeforeVerifiedDeleteTestHook g_beforeVerifiedDeleteTestHook = NULL;
	AfterTargetGuardTestHook g_afterTargetGuardTestHook = NULL;
	BeforeStagedCommitTestHook g_beforeStagedCommitTestHook = NULL;
	ReplaceFileTestHook g_replaceFileTestHook = NULL;
	PathFailureTestHook g_readFileFailureTestHook = NULL;
	PathFailureTestHook g_deleteFileFailureTestHook = NULL;
	std::string g_lastConcurrentRecoveryPath;
	std::string g_lastSecondaryRecoveryPath;
}

void SetBeforeReplaceTestHook(BeforeReplaceTestHook hook)
{
	g_beforeReplaceTestHook = hook;
}

void SetBeforeSecureTempWriteTestHook(BeforeSecureTempWriteTestHook hook)
{
	g_beforeSecureTempWriteTestHook = hook;
}

void SetBeforeVerifiedDeleteTestHook(BeforeVerifiedDeleteTestHook hook)
{
	g_beforeVerifiedDeleteTestHook = hook;
}

void SetAfterTargetGuardTestHook(AfterTargetGuardTestHook hook)
{
	g_afterTargetGuardTestHook = hook;
}

void SetBeforeStagedCommitTestHook(BeforeStagedCommitTestHook hook)
{
	g_beforeStagedCommitTestHook = hook;
}

void SetReplaceFileTestHook(ReplaceFileTestHook hook)
{
	g_replaceFileTestHook = hook;
}

void SetReadFileFailureTestHook(PathFailureTestHook hook)
{
	g_readFileFailureTestHook = hook;
}

void SetDeleteFileFailureTestHook(PathFailureTestHook hook)
{
	g_deleteFileFailureTestHook = hook;
}

const char* LastConcurrentRecoveryPathForTest()
{
	return g_lastConcurrentRecoveryPath.c_str();
}

const char* LastSecondaryRecoveryPathForTest()
{
	return g_lastSecondaryRecoveryPath.c_str();
}

#endif

namespace
{
	const std::size_t MAX_INI_BYTES = 16u * 1024u * 1024u;
	const char* const UTF8_HEX_PREFIX = "utf8-hex:";

	struct IniSetting
	{
		IniSetting(const char* sectionValue, const char* keyValue, const std::string& valueValue)
			: section(sectionValue), key(keyValue), value(valueValue) {}
		std::string section;
		std::string key;
		std::string value;
	};

	struct TextLine
	{
		std::string text;
		std::string ending;
	};

	struct SectionOccurrence
	{
		std::string sectionKey;
		std::size_t header;
		std::size_t end;
	};

	std::string TrimAscii(const std::string& value)
	{
		std::size_t first = 0;
		while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
			++first;
		std::size_t last = value.size();
		while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
			--last;
		return value.substr(first, last - first);
	}

	std::string LowerAscii(const std::string& value)
	{
		std::string lowered(value);
		for (std::string::iterator character = lowered.begin(); character != lowered.end(); ++character)
		{
			const unsigned char byte = static_cast<unsigned char>(*character);
			if (byte >= 'A' && byte <= 'Z') *character = static_cast<char>(byte - 'A' + 'a');
		}
		return lowered;
	}

	bool ParseSection(const std::string& line, std::string& section)
	{
		const std::string trimmed = TrimAscii(line);
		if (trimmed.size() < 3 || trimmed[0] != '[') return false;
		const std::size_t closing = trimmed.find(']');
		if (closing == std::string::npos) return false;
		const std::string remainder = TrimAscii(trimmed.substr(closing + 1));
		if (!remainder.empty() && remainder[0] != ';' && remainder[0] != '#') return false;
		section = TrimAscii(trimmed.substr(1, closing - 1));
		return !section.empty();
	}

	bool ParseKeyValue(const std::string& line, std::string& key, std::string& value)
	{
		const std::string trimmed = TrimAscii(line);
		if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') return false;
		const std::size_t separator = trimmed.find('=');
		if (separator == std::string::npos) return false;
		key = TrimAscii(trimmed.substr(0, separator));
		value = TrimAscii(trimmed.substr(separator + 1));
		return !key.empty();
	}

	std::vector<TextLine> SplitLines(const std::string& bytes)
	{
		std::vector<TextLine> lines;
		std::size_t start = 0;
		while (start < bytes.size())
		{
			const std::size_t newline = bytes.find('\n', start);
			TextLine line;
			if (newline == std::string::npos)
			{
				line.text = bytes.substr(start);
				start = bytes.size();
			}
			else
			{
				std::size_t textEnd = newline;
				if (textEnd > start && bytes[textEnd - 1] == '\r')
				{
					--textEnd;
					line.ending = "\r\n";
				}
				else line.ending = "\n";
				line.text = bytes.substr(start, textEnd - start);
				start = newline + 1;
			}
			lines.push_back(line);
		}
		return lines;
	}

	std::string JoinLines(const std::vector<TextLine>& lines, const std::string& prefix)
	{
		std::ostringstream output;
		output << prefix;
		for (std::size_t index = 0; index < lines.size(); ++index)
			output << lines[index].text << lines[index].ending;
		return output.str();
	}

	std::string DetectNewline(const std::string& bytes)
	{
		const std::size_t newline = bytes.find('\n');
		if (newline != std::string::npos && newline > 0 && bytes[newline - 1] == '\r')
			return "\r\n";
		return "\n";
	}

	std::string NormalizeFriendlyName(const std::string& value)
	{
		std::string normalized;
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			const unsigned char byte = static_cast<unsigned char>(*character);
			if (byte >= 'A' && byte <= 'Z') normalized.push_back(static_cast<char>(byte - 'A' + 'a'));
			else if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9'))
				normalized.push_back(static_cast<char>(byte));
		}
		return normalized;
	}

	bool AsciiCaseInsensitivePrefix(const std::string& prefix, const std::string& value)
	{
		if (prefix.size() > value.size()) return false;
		for (std::size_t index = 0; index < prefix.size(); ++index)
		{
			unsigned char first = static_cast<unsigned char>(prefix[index]);
			unsigned char second = static_cast<unsigned char>(value[index]);
			if (first >= 'A' && first <= 'Z') first = static_cast<unsigned char>(first - 'A' + 'a');
			if (second >= 'A' && second <= 'Z') second = static_cast<unsigned char>(second - 'A' + 'a');
			if (first != second) return false;
		}
		return true;
	}

	bool IsWinmmTruncatedName(const std::string& shorter, const std::string& longer)
	{
		// WAVEINCAPSA::szPname contains 31 payload bytes. Requiring the complete
		// payload prevents ordinary common prefixes from becoming identities.
		const std::size_t winmmPayloadBytes = 31;
		if (shorter.size() != winmmPayloadBytes || longer.size() <= shorter.size()) return false;
		std::string ignored;
		if (!ValidateUtf8Field(shorter, MAX_ENDPOINT_NAME_UTF8_BYTES, ignored) ||
			!ValidateUtf8Field(longer, MAX_ENDPOINT_NAME_UTF8_BYTES, ignored)) return false;
		return AsciiCaseInsensitivePrefix(shorter, longer);
	}

	bool IsSafeDevice(const CaptureDevice& device)
	{
		std::string error;
		return ValidateUtf8Field(device.endpointId, MAX_ENDPOINT_ID_UTF8_BYTES, error) &&
			ValidateUtf8Field(device.friendlyName, MAX_ENDPOINT_NAME_UTF8_BYTES, error);
	}

	void FillResolution(DeviceResolution& resolution, const CaptureDevice& device,
		bool compatibility)
	{
		resolution.found = true;
		resolution.usedCompatibilityIndex = compatibility;
		resolution.winmmIndex = device.winmmIndex;
		resolution.endpointId = device.endpointId;
		resolution.friendlyName = device.friendlyName;
	}

	std::string IntegerText(int value)
	{
		std::ostringstream text;
		text << value;
		return text.str();
	}

	void AddInteger(std::vector<IniSetting>& settings, const char* section,
		const char* key, int value)
	{
		settings.push_back(IniSetting(section, key, IntegerText(value)));
	}

	bool BuildPresetSettings(const DeviceResolution& resolvedDevice,
		std::vector<IniSetting>& settings, std::string& error)
	{
		settings.clear();
		if (!resolvedDevice.found || resolvedDevice.usedCompatibilityIndex ||
			resolvedDevice.endpointId.empty() || resolvedDevice.friendlyName.empty())
		{
			error = "The Adelaide FLEX preset requires an explicitly resolved stable capture endpoint.";
			return false;
		}

		std::string presetId;
		std::string presetName;
		std::string endpointId;
		std::string endpointName;
		if (!EncodeIniUtf8Field(ADELAIDE_FLEX_PRESET_ID, MAX_PROFILE_ID_UTF8_BYTES,
			presetId, error) ||
			!EncodeIniUtf8Field(ADELAIDE_FLEX_PRESET_NAME, MAX_PROFILE_NAME_UTF8_BYTES,
				presetName, error) ||
			!EncodeIniUtf8Field(resolvedDevice.endpointId, MAX_ENDPOINT_ID_UTF8_BYTES,
				endpointId, error) ||
			!EncodeIniUtf8Field(resolvedDevice.friendlyName, MAX_ENDPOINT_NAME_UTF8_BYTES,
				endpointName, error)) return false;

		const DecoderProfileValues values = AdelaideFlexPreset();
		AddInteger(settings, "PDW", "AudioEnabled", values.audioEnabled);
		AddInteger(settings, "PDW", "AudioSource", values.audioSource);
		AddInteger(settings, "PDW", "AudioDevice", values.audioDevice);
		AddInteger(settings, "PDW", "AudioSampleRate", values.audioSampleRate);
		AddInteger(settings, "PDW", "AudioConfiguration", values.audioConfiguration);
		AddInteger(settings, "PDW", "ComPortEnabled", values.comPortEnabled);
		AddInteger(settings, "PDW", "DecodePocsag", values.decodePocsag);
		AddInteger(settings, "PDW", "DecodeFlex", values.decodeFlex);
		AddInteger(settings, "PDW", "PocsagFlex", values.monitorPaging);
		AddInteger(settings, "PDW", "PocsagShowBoth", values.pocsagShowBoth);
		AddInteger(settings, "PDW", "ShowCFS", values.showCfs);
		AddInteger(settings, "PDW", "Flex1600", values.flex1600);
		AddInteger(settings, "PDW", "Flex3200", values.flex3200);
		AddInteger(settings, "PDW", "Flex6400", values.flex6400);
		AddInteger(settings, "PDW", "BTSYNC", values.bitSync);
		AddInteger(settings, "PDW", "MINMSG", values.minimumMessageLength);
		AddInteger(settings, "PDW", "InvertData", values.invertData);
		AddInteger(settings, "PDW", "Percent", values.panePercent);
		AddInteger(settings, "PDW", "Threshold1600", values.threshold1600);
		AddInteger(settings, "PDW", "Threshold512", 0);
		AddInteger(settings, "PDW", "Threshold1200", 0);
		AddInteger(settings, "PDW", "Threshold2400", 0);
		AddInteger(settings, "PDW", "Resync512", 0);
		AddInteger(settings, "PDW", "Resync1200", 0);
		AddInteger(settings, "PDW", "Resync1600", 0);
		AddInteger(settings, "PDW", "Resync2400", 0);
		AddInteger(settings, "PDW", "Centering512", 0);
		AddInteger(settings, "PDW", "Centering1200", 0);
		AddInteger(settings, "PDW", "Centering1600", 0);
		AddInteger(settings, "PDW", "Centering2400", 0);
		settings.push_back(IniSetting("InputProfile", "PresetId", presetId));
		settings.push_back(IniSetting("InputProfile", "PresetName", presetName));
		settings.push_back(IniSetting("InputProfile", "DeviceEndpointId", endpointId));
		settings.push_back(IniSetting("InputProfile", "DeviceFriendlyName", endpointName));
		settings.push_back(IniSetting("InputProfile", "IdentityInvalid", "0"));
		return true;
	}

	void AppendInsertedSettings(std::vector<TextLine>& output,
		const std::vector<IniSetting>& settings,
		const std::string& sectionKey,
		std::map<std::string, std::set<std::string> >& written,
		const std::string& newline,
		bool followedByExisting,
		bool trailingNewline)
	{
		std::vector<const IniSetting*> missing;
		for (std::size_t index = 0; index < settings.size(); ++index)
		{
			if (LowerAscii(settings[index].section) != sectionKey) continue;
			const std::string key = LowerAscii(settings[index].key);
			if (written[sectionKey].insert(key).second) missing.push_back(&settings[index]);
		}
		for (std::size_t index = 0; index < missing.size(); ++index)
		{
			if (!output.empty() && output.back().ending.empty()) output.back().ending = newline;
			TextLine line;
			line.text = missing[index]->key + "=" + missing[index]->value;
			const bool hasFollowing = followedByExisting || index + 1 < missing.size();
			if (hasFollowing || trailingNewline) line.ending = newline;
			output.push_back(line);
		}
	}

	std::string MergePresetSettings(const std::string& originalInput,
		const std::vector<IniSetting>& settings)
	{
		std::string original(originalInput);
		std::string prefix;
		if (original.size() >= 3 &&
			static_cast<unsigned char>(original[0]) == 0xef &&
			static_cast<unsigned char>(original[1]) == 0xbb &&
			static_cast<unsigned char>(original[2]) == 0xbf)
		{
			prefix.assign(original, 0, 3);
			original.erase(0, 3);
		}
		const std::string newline = DetectNewline(original);
		const bool trailingNewline = !original.empty() && original[original.size() - 1] == '\n';
		const std::vector<TextLine> lines = SplitLines(original);

		std::map<std::string, std::map<std::string, const IniSetting*> > replacements;
		std::vector<std::string> sectionOrder;
		for (std::size_t index = 0; index < settings.size(); ++index)
		{
			const std::string section = LowerAscii(settings[index].section);
			if (replacements.find(section) == replacements.end()) sectionOrder.push_back(section);
			replacements[section][LowerAscii(settings[index].key)] = &settings[index];
		}

		std::vector<SectionOccurrence> occurrences;
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			std::string section;
			if (!ParseSection(lines[index].text, section)) continue;
			if (!occurrences.empty()) occurrences.back().end = index;
			SectionOccurrence occurrence;
			occurrence.sectionKey = LowerAscii(section);
			occurrence.header = index;
			occurrence.end = lines.size();
			occurrences.push_back(occurrence);
		}
		std::map<std::string, std::size_t> insertionAt;
		std::set<std::string> existingSections;
		for (std::size_t index = 0; index < occurrences.size(); ++index)
		{
			if (!replacements.count(occurrences[index].sectionKey)) continue;
			existingSections.insert(occurrences[index].sectionKey);
			std::size_t insertion = occurrences[index].end;
			while (insertion > occurrences[index].header + 1 &&
				TrimAscii(lines[insertion - 1].text).empty()) --insertion;
			insertionAt[occurrences[index].sectionKey] = insertion;
		}

		std::vector<TextLine> output;
		std::map<std::string, std::set<std::string> > written;
		std::string currentSection;
		for (std::size_t index = 0; index <= lines.size(); ++index)
		{
			for (std::size_t sectionIndex = 0; sectionIndex < sectionOrder.size(); ++sectionIndex)
			{
				const std::string& section = sectionOrder[sectionIndex];
				if (insertionAt.count(section) && insertionAt[section] == index)
					AppendInsertedSettings(output, settings, section, written, newline,
						index < lines.size(), trailingNewline && index == lines.size());
			}
			if (index == lines.size()) break;

			std::string section;
			if (ParseSection(lines[index].text, section))
			{
				currentSection = LowerAscii(section);
				output.push_back(lines[index]);
				continue;
			}

			std::string key;
			std::string value;
			if (replacements.count(currentSection) && ParseKeyValue(lines[index].text, key, value))
			{
				const std::string normalizedKey = LowerAscii(key);
				std::map<std::string, const IniSetting*>::const_iterator replacement =
					replacements[currentSection].find(normalizedKey);
				if (replacement != replacements[currentSection].end())
				{
					if (written[currentSection].insert(normalizedKey).second)
					{
						TextLine replaced = lines[index];
						replaced.text = replacement->second->key + "=" + replacement->second->value;
						output.push_back(replaced);
					}
					continue;
				}
			}
			output.push_back(lines[index]);
		}

		for (std::size_t sectionIndex = 0; sectionIndex < sectionOrder.size(); ++sectionIndex)
		{
			const std::string& section = sectionOrder[sectionIndex];
			if (existingSections.count(section)) continue;
			if (!output.empty() && output.back().ending.empty()) output.back().ending = newline;
			if (!output.empty() && !TrimAscii(output.back().text).empty())
			{
				TextLine blank;
				blank.ending = newline;
				output.push_back(blank);
			}
			TextLine header;
			for (std::size_t settingIndex = 0; settingIndex < settings.size(); ++settingIndex)
			{
				if (LowerAscii(settings[settingIndex].section) == section)
				{
					header.text = "[" + settings[settingIndex].section + "]";
					break;
				}
			}
			header.ending = newline;
			output.push_back(header);
			AppendInsertedSettings(output, settings, section, written, newline,
				sectionIndex + 1 < sectionOrder.size(), trailingNewline);
		}
		if (!trailingNewline && !output.empty()) output.back().ending.clear();
		return JoinLines(output, prefix);
	}

	bool ReadIniValue(const std::string& contents, const std::string& wantedSection,
		const std::string& wantedKey, std::string& value)
	{
		std::string bytes(contents);
		if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
			static_cast<unsigned char>(bytes[1]) == 0xbb &&
			static_cast<unsigned char>(bytes[2]) == 0xbf) bytes.erase(0, 3);
		const std::vector<TextLine> lines = SplitLines(bytes);
		std::string currentSection;
		bool found = false;
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			std::string section;
			if (ParseSection(lines[index].text, section))
			{
				currentSection = LowerAscii(section);
				continue;
			}
			std::string key;
			std::string parsedValue;
			if (currentSection == LowerAscii(wantedSection) &&
				ParseKeyValue(lines[index].text, key, parsedValue) &&
				LowerAscii(key) == LowerAscii(wantedKey))
			{
				value = parsedValue;
				found = true;
			}
		}
		return found;
	}

	bool VerifyPresetContents(const std::string& contents,
		const std::vector<IniSetting>& settings, std::string& error)
	{
		for (std::size_t index = 0; index < settings.size(); ++index)
		{
			std::string actual;
			if (!ReadIniValue(contents, settings[index].section, settings[index].key, actual) ||
				actual != settings[index].value)
			{
				error = "The staged profile did not contain the expected " +
					settings[index].section + "." + settings[index].key + " value.";
				return false;
			}
		}
		return true;
	}

	std::string WindowsError(const char* operation, DWORD code)
	{
		std::ostringstream text;
		text << operation << " failed with Windows error " <<
			static_cast<unsigned long>(code) << ".";
		return text.str();
	}

	bool ResolveFullPath(const std::string& path, std::string& fullPath, std::string& error)
	{
		if (path.empty())
		{
			error = "The settings path is empty.";
			return false;
		}
		char resolved[MAX_PATH] = {};
		const DWORD length = GetFullPathNameA(path.c_str(), static_cast<DWORD>(_countof(resolved)),
			resolved, NULL);
		if (!length)
		{
			error = WindowsError("Resolving the settings path", GetLastError());
			return false;
		}
		if (length >= _countof(resolved))
		{
			error = "The settings path is too long for this PDW build.";
			return false;
		}
		fullPath = resolved;
		return true;
	}

	bool ReadOpenFileBytes(HANDLE file, std::string& contents, std::string& error)
	{
		contents.clear();
		LARGE_INTEGER beginning = {};
		if (!SetFilePointerEx(file, beginning, NULL, FILE_BEGIN))
		{
			error = WindowsError("Seeking the file", GetLastError());
			return false;
		}
		LARGE_INTEGER length = {};
		if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
			static_cast<unsigned long long>(length.QuadPart) > MAX_INI_BYTES)
		{
			const DWORD code = GetLastError();
			error = length.QuadPart > static_cast<LONGLONG>(MAX_INI_BYTES) ?
				"The settings file is too large to process safely." :
				WindowsError("Reading the file size", code);
			return false;
		}
		contents.assign(static_cast<std::size_t>(length.QuadPart), '\0');
		std::size_t total = 0;
		while (total < contents.size())
		{
			const DWORD requested = static_cast<DWORD>((std::min)(contents.size() - total,
				static_cast<std::size_t>(0x7fffffff)));
			DWORD read = 0;
			if (!ReadFile(file, &contents[total], requested, &read, NULL) || !read)
			{
				const DWORD code = GetLastError();
				error = WindowsError("Reading the file", code);
				return false;
			}
			total += read;
		}
		return true;
	}

	bool ReadFileBytes(const std::string& path, std::string& contents, DWORD& attributes,
		std::string& error)
	{
		contents.clear();
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_readFileFailureTestHook && g_readFileFailureTestHook(path.c_str()))
		{
			error = WindowsError("Reading the file", ERROR_ACCESS_DENIED);
			return false;
		}
#endif
		attributes = GetFileAttributesA(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			error = WindowsError("Reading file attributes", GetLastError());
			return false;
		}
		if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			error = "The requested file path refers to a directory.";
			return false;
		}
		HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			error = WindowsError("Opening the file", GetLastError());
			return false;
		}
		const bool read = ReadOpenFileBytes(file, contents, error);
		CloseHandle(file);
		return read;
	}

	struct FileIdentity
	{
		FileIdentity() : valid(false), volumeSerial(0), fileIndexHigh(0), fileIndexLow(0) {}
		bool valid;
		DWORD volumeSerial;
		DWORD fileIndexHigh;
		DWORD fileIndexLow;
	};

	FileIdentity IdentityFromInformation(const BY_HANDLE_FILE_INFORMATION& information)
	{
		FileIdentity identity;
		identity.valid = true;
		identity.volumeSerial = information.dwVolumeSerialNumber;
		identity.fileIndexHigh = information.nFileIndexHigh;
		identity.fileIndexLow = information.nFileIndexLow;
		return identity;
	}

	bool SameFileIdentity(const FileIdentity& left, const FileIdentity& right)
	{
		return left.valid && right.valid && left.volumeSerial == right.volumeSerial &&
			left.fileIndexHigh == right.fileIndexHigh && left.fileIndexLow == right.fileIndexLow;
	}

	bool OpenVerifiedTargetGuard(const std::string& path,
		const std::string& expectedContents, HANDLE& guard, DWORD& attributes,
		FileIdentity& identity, std::string& error)
	{
		guard = INVALID_HANDLE_VALUE;
		const DWORD pathAttributes = GetFileAttributesA(path.c_str());
		if (pathAttributes == INVALID_FILE_ATTRIBUTES)
		{
			error = WindowsError("Reading file attributes", GetLastError());
			return false;
		}
		if (pathAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			error = "The requested file path refers to a directory.";
			return false;
		}

		// Deny write sharing from the final byte comparison through ReplaceFileA.
		// Delete sharing is required so ReplaceFileA can atomically exchange the
		// directory entry while this handle continues to pin the verified file.
		guard = CreateFileA(path.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES | READ_CONTROL,
			FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (guard == INVALID_HANDLE_VALUE)
		{
			const DWORD code = GetLastError();
			error = code == ERROR_SHARING_VIOLATION ?
				"The settings file is being edited by another program; no changes were made." :
				WindowsError("Locking the settings file for commit", code);
			return false;
		}

		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(guard, &information))
		{
			const DWORD code = GetLastError();
			CloseHandle(guard);
			guard = INVALID_HANDLE_VALUE;
			error = WindowsError("Reading the locked settings file attributes", code);
			return false;
		}
		attributes = information.dwFileAttributes;
		identity = IdentityFromInformation(information);
		std::string current;
		if (!ReadOpenFileBytes(guard, current, error))
		{
			CloseHandle(guard);
			guard = INVALID_HANDLE_VALUE;
			return false;
		}
		if (current != expectedContents)
		{
			CloseHandle(guard);
			guard = INVALID_HANDLE_VALUE;
			error = "The settings file changed while the transaction was being prepared.";
			return false;
		}
		return true;
	}

	bool SetGuardReadOnlyAttribute(HANDLE guard, bool readOnly, std::string& error)
	{
		FILE_BASIC_INFO basicInformation = {};
		if (!GetFileInformationByHandleEx(guard, FileBasicInfo, &basicInformation,
			sizeof(basicInformation)))
		{
			error = WindowsError("Reading the guarded settings attributes", GetLastError());
			return false;
		}
		if (readOnly)
			basicInformation.FileAttributes |= FILE_ATTRIBUTE_READONLY;
		else
			basicInformation.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
		if (!basicInformation.FileAttributes)
			basicInformation.FileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (!SetFileInformationByHandle(guard, FileBasicInfo, &basicInformation,
			sizeof(basicInformation)))
		{
			error = WindowsError(readOnly ?
				"Restoring the guarded read-only settings attribute" :
				"Clearing the guarded read-only settings attribute", GetLastError());
			return false;
		}
		return true;
	}

	bool OpenVerifiedRecoveryCopyGuard(const std::string& path,
		const std::string& expectedContents, HANDLE& guard,
		FileIdentity& identity, std::string& error)
	{
		guard = CreateFileA(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | READ_CONTROL,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (guard == INVALID_HANDLE_VALUE)
		{
			error = WindowsError("Pinning the recovery-copy source", GetLastError());
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		std::string contents;
		if (!GetFileInformationByHandle(guard, &information) ||
			!ReadOpenFileBytes(guard, contents, error) || contents != expectedContents)
		{
			if (error.empty())
				error = "The pinned recovery-copy source changed before it could be copied.";
			CloseHandle(guard);
			guard = INVALID_HANDLE_VALUE;
			return false;
		}
		identity = IdentityFromInformation(information);
		return true;
	}

	bool OpenPinnedFileSnapshot(const std::string& path, HANDLE& guard,
		std::string& contents, DWORD& attributes, FileIdentity& identity,
		std::string& error)
	{
		guard = CreateFileA(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | READ_CONTROL,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (guard == INVALID_HANDLE_VALUE)
		{
			error = WindowsError("Pinning the settings file for a snapshot", GetLastError());
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(guard, &information) ||
			!ReadOpenFileBytes(guard, contents, error))
		{
			if (error.empty()) error = WindowsError("Reading the pinned settings snapshot",
				GetLastError());
			CloseHandle(guard);
			guard = INVALID_HANDLE_VALUE;
			return false;
		}
		attributes = information.dwFileAttributes;
		identity = IdentityFromInformation(information);
		return true;
	}

	bool WriteOpenFileBytesDurably(HANDLE file, const std::string& contents,
		std::string& error)
	{
		LARGE_INTEGER beginning = {};
		if (!SetFilePointerEx(file, beginning, NULL, FILE_BEGIN) || !SetEndOfFile(file))
		{
			error = WindowsError("Truncating the protected staged file", GetLastError());
			return false;
		}
		std::size_t total = 0;
		while (total < contents.size())
		{
			const DWORD requested = static_cast<DWORD>((std::min)(contents.size() - total,
				static_cast<std::size_t>(0x7fffffff)));
			DWORD written = 0;
			if (!WriteFile(file, contents.data() + total, requested, &written, NULL) || !written)
			{
				error = WindowsError("Writing the staged file", GetLastError());
				return false;
			}
			total += written;
		}
		if (!FlushFileBuffers(file))
		{
			error = WindowsError("Flushing the staged file", GetLastError());
			return false;
		}
		return true;
	}

	bool CopyBackupStreamsDurably(HANDLE source, HANDLE destination,
		std::string& error)
	{
		std::vector<unsigned char> buffer(64u * 1024u);
		LPVOID readContext = NULL;
		LPVOID writeContext = NULL;
		bool copied = true;
		for (;;)
		{
			DWORD read = 0;
			if (!BackupRead(source, &buffer[0], static_cast<DWORD>(buffer.size()),
				&read, FALSE, TRUE, &readContext))
			{
				error = WindowsError("Reading metadata-preserving recovery streams",
					GetLastError());
				copied = false;
				break;
			}
			if (!read) break;
			DWORD written = 0;
			if (!BackupWrite(destination, &buffer[0], read, &written, FALSE, TRUE,
				&writeContext) || written != read)
			{
				error = WindowsError("Writing metadata-preserving recovery streams",
					GetLastError());
				copied = false;
				break;
			}
		}
		DWORD ignored = 0;
		BackupRead(source, NULL, 0, &ignored, TRUE, FALSE, &readContext);
		BackupWrite(destination, NULL, 0, &ignored, TRUE, FALSE, &writeContext);
		if (copied && !FlushFileBuffers(destination))
		{
			error = WindowsError("Flushing metadata-preserving recovery streams",
				GetLastError());
			copied = false;
		}
		return copied;
	}

	std::string DirectoryName(const std::string& path)
	{
		const std::string::size_type separator = path.find_last_of("\\/");
		if (separator == std::string::npos) return std::string(".");
		if (separator == 2 && path.size() > 2 && path[1] == ':') return path.substr(0, 3);
		return path.substr(0, separator);
	}

	bool ReadDaclDescriptor(const std::string& path,
		std::vector<unsigned char>& descriptor, WORD& control, std::string& error)
	{
		DWORD required = 0;
		GetFileSecurityA(path.c_str(), DACL_SECURITY_INFORMATION, NULL, 0, &required);
		if (!required || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			error = WindowsError("Sizing file security", GetLastError());
			return false;
		}
		descriptor.assign(required, 0);
		if (!GetFileSecurityA(path.c_str(), DACL_SECURITY_INFORMATION,
			reinterpret_cast<PSECURITY_DESCRIPTOR>(&descriptor[0]), required, &required))
		{
			error = WindowsError("Reading file security", GetLastError());
			return false;
		}
		DWORD revision = 0;
		if (!GetSecurityDescriptorControl(
			reinterpret_cast<PSECURITY_DESCRIPTOR>(&descriptor[0]), &control, &revision))
		{
			error = WindowsError("Reading file security control", GetLastError());
			return false;
		}
		return true;
	}

	bool ReadDaclDescriptorFromHandle(HANDLE file,
		std::vector<unsigned char>& descriptor, WORD& control, std::string& error)
	{
		DWORD required = 0;
		GetKernelObjectSecurity(file, DACL_SECURITY_INFORMATION, NULL, 0, &required);
		if (!required || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			error = WindowsError("Sizing guarded file security", GetLastError());
			return false;
		}
		descriptor.assign(required, 0);
		if (!GetKernelObjectSecurity(file, DACL_SECURITY_INFORMATION,
			reinterpret_cast<PSECURITY_DESCRIPTOR>(&descriptor[0]), required, &required))
		{
			error = WindowsError("Reading guarded file security", GetLastError());
			return false;
		}
		DWORD revision = 0;
		if (!GetSecurityDescriptorControl(
			reinterpret_cast<PSECURITY_DESCRIPTOR>(&descriptor[0]), &control, &revision))
		{
			error = WindowsError("Reading guarded file security control", GetLastError());
			return false;
		}
		return true;
	}

	bool EquivalentDescriptorDacl(PSECURITY_DESCRIPTOR left,
		PSECURITY_DESCRIPTOR right)
	{
		BOOL leftPresent = FALSE;
		BOOL rightPresent = FALSE;
		BOOL leftDefaulted = FALSE;
		BOOL rightDefaulted = FALSE;
		PACL leftAcl = NULL;
		PACL rightAcl = NULL;
		if (!left || !right ||
			!GetSecurityDescriptorDacl(left, &leftPresent, &leftAcl, &leftDefaulted) ||
			!GetSecurityDescriptorDacl(right, &rightPresent, &rightAcl, &rightDefaulted))
			return false;
		if (leftPresent != rightPresent) return false;
		if (!leftPresent) return true;
		if (!leftAcl || !rightAcl || leftAcl->AclSize != rightAcl->AclSize) return false;
		return memcmp(leftAcl, rightAcl, leftAcl->AclSize) == 0;
	}

	bool EquivalentDacl(const std::vector<unsigned char>& left,
		const std::vector<unsigned char>& right)
	{
		if (left.empty() || right.empty()) return false;
		return EquivalentDescriptorDacl(
			reinterpret_cast<PSECURITY_DESCRIPTOR>(
				const_cast<unsigned char*>(&left[0])),
			reinterpret_cast<PSECURITY_DESCRIPTOR>(
				const_cast<unsigned char*>(&right[0])));
	}

	bool BuildPrivateSecurityDescriptor(std::vector<unsigned char>& tokenInformation,
		std::vector<unsigned char>& aclStorage, SECURITY_DESCRIPTOR& descriptor,
		std::string& error)
	{
		HANDLE token = NULL;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			error = WindowsError("Opening the process token for private settings staging",
				GetLastError());
			return false;
		}
		DWORD required = 0;
		GetTokenInformation(token, TokenUser, NULL, 0, &required);
		if (!required || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			const DWORD code = GetLastError();
			CloseHandle(token);
			error = WindowsError("Sizing the current-user security identity", code);
			return false;
		}
		tokenInformation.assign(required, 0);
		if (!GetTokenInformation(token, TokenUser, &tokenInformation[0], required, &required))
		{
			const DWORD code = GetLastError();
			CloseHandle(token);
			error = WindowsError("Reading the current-user security identity", code);
			return false;
		}
		CloseHandle(token);
		const TOKEN_USER* user = reinterpret_cast<const TOKEN_USER*>(&tokenInformation[0]);
		const DWORD aclBytes = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) +
			GetLengthSid(user->User.Sid);
		aclStorage.assign(aclBytes, 0);
		PACL acl = reinterpret_cast<PACL>(&aclStorage[0]);
		if (!InitializeAcl(acl, aclBytes, ACL_REVISION) ||
			!AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, user->User.Sid) ||
			!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
			!SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
			!SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
		{
			error = WindowsError("Building a private settings-file DACL", GetLastError());
			return false;
		}
		return true;
	}

	bool CreateSecureTemporaryPath(const std::string& targetPath, const char* prefix,
		const std::string& securityReferencePath, std::string& temporaryPath,
		HANDLE securityReferenceHandle, HANDLE& temporaryHandle, std::string& error)
	{
		temporaryHandle = INVALID_HANDLE_VALUE;
		if (!prefix || strlen(prefix) < 3 || strlen(prefix) > 16)
		{
			error = "A secure temporary-file prefix must contain between three and sixteen characters.";
			return false;
		}

		std::vector<unsigned char> sourceSecurity;
		WORD sourceControl = 0;
		SECURITY_DESCRIPTOR privateDescriptor = {};
		std::vector<unsigned char> tokenInformation;
		std::vector<unsigned char> privateAcl;
		PSECURITY_DESCRIPTOR creationDescriptor = NULL;
		HANDLE ownedReferenceHandle = INVALID_HANDLE_VALUE;
		HANDLE referenceHandle = securityReferenceHandle;
		if (referenceHandle == INVALID_HANDLE_VALUE)
		{
			ownedReferenceHandle = CreateFileA(securityReferencePath.c_str(),
				READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, NULL);
			referenceHandle = ownedReferenceHandle;
		}
		if (referenceHandle != INVALID_HANDLE_VALUE)
		{
			BY_HANDLE_FILE_INFORMATION information = {};
			if (!GetFileInformationByHandle(referenceHandle, &information) ||
				(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
				!ReadDaclDescriptorFromHandle(referenceHandle, sourceSecurity,
					sourceControl, error))
			{
				if (ownedReferenceHandle != INVALID_HANDLE_VALUE)
					CloseHandle(ownedReferenceHandle);
				if (error.empty()) error = "The settings security reference is not a regular file.";
				return false;
			}
			creationDescriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(&sourceSecurity[0]);
			if (ownedReferenceHandle != INVALID_HANDLE_VALUE)
				CloseHandle(ownedReferenceHandle);
		}
		else
		{
			const DWORD code = GetLastError();
			if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
			{
				error = WindowsError("Inspecting the settings security reference", code);
				return false;
			}
			if (!BuildPrivateSecurityDescriptor(tokenInformation, privateAcl,
				privateDescriptor, error)) return false;
			creationDescriptor = &privateDescriptor;
			sourceControl = SE_DACL_PROTECTED;
		}

		SECURITY_ATTRIBUTES securityAttributes = {};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.lpSecurityDescriptor = creationDescriptor;
		const std::string directory = DirectoryName(targetPath);
		for (unsigned int attempt = 0; attempt < 32; ++attempt)
		{
			unsigned char randomBytes[16] = {};
			HCRYPTPROV provider = 0;
			if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_FULL,
				CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
			{
				error = WindowsError("Opening the Windows random-number provider", GetLastError());
				return false;
			}
			const BOOL generated = CryptGenRandom(provider,
				static_cast<DWORD>(sizeof(randomBytes)), randomBytes);
			const DWORD randomCode = generated ? ERROR_SUCCESS : GetLastError();
			CryptReleaseContext(provider, 0);
			if (!generated)
			{
				error = WindowsError("Generating a protected settings temporary name", randomCode);
				return false;
			}
			static const char hex[] = "0123456789abcdef";
			char randomHex[sizeof(randomBytes) * 2 + 1] = {};
			for (std::size_t index = 0; index < sizeof(randomBytes); ++index)
			{
				randomHex[index * 2] = hex[randomBytes[index] >> 4];
				randomHex[index * 2 + 1] = hex[randomBytes[index] & 0x0f];
			}
			const std::string separator = !directory.empty() &&
				(directory[directory.size() - 1] == '\\' ||
				directory[directory.size() - 1] == '/') ? "" : "\\";
			const std::string candidate = directory + separator + prefix + "-" +
				randomHex + ".tmp";
			if (candidate.size() >= MAX_PATH)
			{
				error = "The protected settings temporary pathname is too long.";
				return false;
			}
			HANDLE file = CreateFileA(candidate.c_str(),
				GENERIC_READ | GENERIC_WRITE | DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER, 0,
				&securityAttributes, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
			if (file == INVALID_HANDLE_VALUE)
			{
				const DWORD code = GetLastError();
				if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) continue;
				error = WindowsError("Creating a protected settings temporary file", code);
				return false;
			}
			std::vector<unsigned char> createdSecurity;
			WORD createdControl = 0;
			if (!ReadDaclDescriptorFromHandle(file, createdSecurity, createdControl, error) ||
				!EquivalentDescriptorDacl(creationDescriptor,
					reinterpret_cast<PSECURITY_DESCRIPTOR>(&createdSecurity[0])) ||
				((sourceControl ^ createdControl) & SE_DACL_PROTECTED) != 0)
			{
				if (error.empty())
					error = "The protected settings temporary file did not receive the requested DACL.";
				std::string cleanupError;
				if (!MarkSensitiveTemporaryFileForDeletion(file, candidate, cleanupError) &&
					!cleanupError.empty()) error += " " + cleanupError;
				CloseHandle(file);
				return false;
			}
			temporaryPath = candidate;
			temporaryHandle = file;
			return true;
		}
		error = "PDW could not reserve an unoccupied protected settings temporary path.";
		return false;
	}

	bool CreateTemporaryPath(const std::string& targetPath, std::string& temporaryPath,
		HANDLE securityReferenceHandle, HANDLE& temporaryHandle, std::string& error)
	{
		return CreateSecureTemporaryPath(targetPath, "PAP", targetPath,
			temporaryPath, securityReferenceHandle, temporaryHandle, error);
	}

	bool ApplyPortableAttributes(HANDLE temporaryHandle, DWORD sourceAttributes,
		bool includeReadOnly, std::string& error)
	{
		DWORD mask = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
			FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
		if (includeReadOnly) mask |= FILE_ATTRIBUTE_READONLY;
		const DWORD portableAttributes = sourceAttributes & mask;
		FILE_BASIC_INFO basicInformation = {};
		if (!GetFileInformationByHandleEx(temporaryHandle, FileBasicInfo,
			&basicInformation, sizeof(basicInformation)))
		{
			error = WindowsError("Reading staged settings attributes", GetLastError());
			return false;
		}
		basicInformation.FileAttributes = portableAttributes ?
			portableAttributes : FILE_ATTRIBUTE_NORMAL;
		if (!SetFileInformationByHandle(temporaryHandle, FileBasicInfo,
			&basicInformation, sizeof(basicInformation)))
		{
			error = WindowsError("Applying staged settings attributes", GetLastError());
			return false;
		}
		return true;
	}

	BOOL InvokeReplaceFile(const char* replacedPath, const char* replacementPath,
		const char* backupPath, DWORD flags)
	{
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_replaceFileTestHook)
			return g_replaceFileTestHook(replacedPath, replacementPath, backupPath,
				flags) ? TRUE : FALSE;
#endif
		return ReplaceFileA(replacedPath, replacementPath, backupPath, flags, NULL, NULL);
	}

	void AppendDetail(std::string& detail, const std::string& sentence);

	bool CreateVacantBackupPath(const std::string& targetPath,
		std::string& backupPath, std::string& error,
		HANDLE securityReferenceHandle = INVALID_HANDLE_VALUE)
	{
		HANDLE reservedHandle = INVALID_HANDLE_VALUE;
		if (!CreateTemporaryPath(targetPath, backupPath, securityReferenceHandle,
			reservedHandle, error)) return false;
	#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_beforeVerifiedDeleteTestHook)
			g_beforeVerifiedDeleteTestHook(backupPath.c_str());
	#endif
		std::string removalError;
		if (!MarkSensitiveTemporaryFileForDeletion(reservedHandle, backupPath,
			removalError))
		{
			CloseHandle(reservedHandle);
			error = removalError;
			return false;
		}
		CloseHandle(reservedHandle);
		const DWORD attributes = GetFileAttributesA(backupPath.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES)
		{
			error = "The reserved backup pathname is unexpectedly occupied at \"" +
				backupPath + "\".";
			return false;
		}
		const DWORD code = GetLastError();
		if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
		{
			error = WindowsError("Verifying the vacant backup pathname", code);
			return false;
		}
		return true;
	}

	struct InspectedPath
	{
		InspectedPath()
			: exists(false), missing(false), readable(false), attributes(0), errorCode(ERROR_SUCCESS) {}
		bool exists;
		bool missing;
		bool readable;
		DWORD attributes;
		DWORD errorCode;
		FileIdentity identity;
		std::string bytes;
		std::string error;
	};

	InspectedPath InspectPath(const std::string& path)
	{
		InspectedPath state;
		state.attributes = GetFileAttributesA(path.c_str());
		if (state.attributes == INVALID_FILE_ATTRIBUTES)
		{
			state.errorCode = GetLastError();
			state.missing = state.errorCode == ERROR_FILE_NOT_FOUND ||
				state.errorCode == ERROR_PATH_NOT_FOUND;
			if (!state.missing)
				state.error = WindowsError("Inspecting a transaction file", state.errorCode);
			return state;
		}
		state.exists = true;
		if (state.attributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			state.error = "A transaction pathname unexpectedly refers to a directory.";
			return state;
		}
		HANDLE identityHandle = CreateFileA(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (identityHandle != INVALID_HANDLE_VALUE)
		{
			BY_HANDLE_FILE_INFORMATION information = {};
			if (GetFileInformationByHandle(identityHandle, &information))
				state.identity = IdentityFromInformation(information);
			else
				state.error = WindowsError("Reading transaction-file identity", GetLastError());
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
			if (g_readFileFailureTestHook && g_readFileFailureTestHook(path.c_str()))
				state.error = WindowsError("Reading the file", ERROR_ACCESS_DENIED);
			else
#endif
				state.readable = ReadOpenFileBytes(identityHandle, state.bytes, state.error);
			CloseHandle(identityHandle);
		}
		else
		{
			state.errorCode = GetLastError();
			state.error = WindowsError("Opening a transaction file for inspection",
				state.errorCode);
		}
		return state;
	}

	bool OpenPinnedInspectedPath(const std::string& path, HANDLE& handle,
		InspectedPath& state)
	{
		handle = INVALID_HANDLE_VALUE;
		state = InspectedPath();
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_readFileFailureTestHook && g_readFileFailureTestHook(path.c_str()))
		{
			state.errorCode = ERROR_ACCESS_DENIED;
			state.error = WindowsError("Reading the file", state.errorCode);
			return false;
		}
#endif
		handle = CreateFileA(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (handle == INVALID_HANDLE_VALUE)
		{
			state.errorCode = GetLastError();
			state.missing = state.errorCode == ERROR_FILE_NOT_FOUND ||
				state.errorCode == ERROR_PATH_NOT_FOUND;
			state.error = WindowsError("Pinning a transaction file for verification",
				state.errorCode);
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(handle, &information))
		{
			state.errorCode = GetLastError();
			state.error = WindowsError("Reading pinned transaction-file identity",
				state.errorCode);
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
			return false;
		}
		state.exists = true;
		state.attributes = information.dwFileAttributes;
		state.identity = IdentityFromInformation(information);
		state.readable = ReadOpenFileBytes(handle, state.bytes, state.error);
		if (!state.readable)
		{
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
			return false;
		}
		return true;
	}

	bool DeletePinnedVerifiedPath(const std::string& path, HANDLE handle,
		const InspectedPath& state, const std::string& expectedBytes,
		const FileIdentity& expectedIdentity, const char* description,
		std::string& detail)
	{
		if (handle == INVALID_HANDLE_VALUE || !state.readable ||
			state.bytes != expectedBytes || !SameFileIdentity(state.identity, expectedIdentity))
			return false;
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_beforeVerifiedDeleteTestHook)
			g_beforeVerifiedDeleteTestHook(path.c_str());
		if (g_deleteFileFailureTestHook && g_deleteFileFailureTestHook(path.c_str()))
		{
			SetLastError(ERROR_ACCESS_DENIED);
			AppendDetail(detail, WindowsError(description, ERROR_ACCESS_DENIED) +
				" The file was retained at \"" + path + "\".");
			return false;
		}
#endif
		FILE_DISPOSITION_INFO disposition = {};
		disposition.DeleteFile = TRUE;
		if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
			sizeof(disposition)))
		{
			AppendDetail(detail, WindowsError(description, GetLastError()) +
				" The file was retained at \"" + path + "\".");
			return false;
		}
		return true;
	}

	void AppendDetail(std::string& detail, const std::string& sentence)
	{
		if (sentence.empty()) return;
		if (!detail.empty() && detail[detail.size() - 1] != ' ') detail += ' ';
		detail += sentence;
	}

	void MarkSensitiveStagedFileForDeletionAndClose(HANDLE& file,
		const std::string& path, std::string& detail)
	{
		if (file == INVALID_HANDLE_VALUE) return;
		std::string cleanupError;
		if (!MarkSensitiveTemporaryFileForDeletion(file, path, cleanupError))
			AppendDetail(detail, cleanupError);
		CloseHandle(file);
		file = INVALID_HANDLE_VALUE;
	}

	bool RestoreMissingTargetFromDisplacedFile(const std::string& targetPath,
		const std::string& displacedPath, const std::string& verifiedBytes,
		const FileIdentity& verifiedIdentity,
		std::string& retainedCopyPath, std::string& detail)
	{
		std::string copyError;
		HANDLE displacedGuard = INVALID_HANDLE_VALUE;
		DWORD displacedAttributes = 0;
		FileIdentity displacedIdentity;
		if (!OpenVerifiedTargetGuard(displacedPath, verifiedBytes, displacedGuard,
			displacedAttributes, displacedIdentity, copyError) ||
			!SameFileIdentity(displacedIdentity, verifiedIdentity))
		{
			if (displacedGuard != INVALID_HANDLE_VALUE) CloseHandle(displacedGuard);
			AppendDetail(detail,
				"The displaced recovery file did not match the guarded pre-commit identity and was left untouched at \"" +
				displacedPath + "\".");
			if (!copyError.empty()) AppendDetail(detail, copyError);
			return false;
		}
		HANDLE retainedCopyHandle = INVALID_HANDLE_VALUE;
		if (!CreateSecureTemporaryPath(targetPath, "PAP", displacedPath,
			retainedCopyPath, displacedGuard, retainedCopyHandle, copyError))
		{
			CloseHandle(displacedGuard);
			AppendDetail(detail, "PDW could not create a recovery staging file: " + copyError);
			return false;
		}
		if (!CopyBackupStreamsDurably(displacedGuard, retainedCopyHandle, copyError) ||
			!ApplyPortableAttributes(retainedCopyHandle, displacedAttributes, true, copyError))
		{
			CloseHandle(retainedCopyHandle);
			CloseHandle(displacedGuard);
			AppendDetail(detail, "PDW could not create the metadata-aware recovery copy at \"" +
				retainedCopyPath + "\": " + copyError);
			return false;
		}
		std::string stagedBytes;
		BY_HANDLE_FILE_INFORMATION retainedInformation = {};
		if (!ReadOpenFileBytes(retainedCopyHandle, stagedBytes, copyError) ||
			stagedBytes != verifiedBytes ||
			!GetFileInformationByHandle(retainedCopyHandle, &retainedInformation) ||
			SameFileIdentity(IdentityFromInformation(retainedInformation), verifiedIdentity))
		{
			CloseHandle(retainedCopyHandle);
			CloseHandle(displacedGuard);
			AppendDetail(detail,
				"PDW could not verify the recovery staging file, so it was retained at \"" +
				retainedCopyPath + "\".");
			if (!copyError.empty()) AppendDetail(detail, copyError);
			return false;
		}
		CloseHandle(retainedCopyHandle);
		AppendDetail(detail,
			"A byte-exact, metadata-aware recovery copy was retained at \"" +
			retainedCopyPath + "\".");

		// Move the actual displaced file back so its attributes, security descriptor,
		// encryption and alternate streams survive. Deliberately omit
		// MOVEFILE_REPLACE_EXISTING: a newly recreated target always wins.
		if (!MoveFileExA(displacedPath.c_str(), targetPath.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			const DWORD code = GetLastError();
			CloseHandle(displacedGuard);
			const InspectedPath targetAfterMove = InspectPath(targetPath);
			if (targetAfterMove.missing)
				AppendDetail(detail, WindowsError("Restoring the missing settings path", code) +
					" The displaced file and its verified recovery copy were retained.");
			else
				AppendDetail(detail,
					"The settings path was recreated concurrently; PDW left that pathname occupant untouched.");
			return false;
		}

		CloseHandle(displacedGuard);
		HANDLE restoredGuard = INVALID_HANDLE_VALUE;
		FileIdentity restoredIdentity;
		std::string restoredError;
		const bool restored = OpenVerifiedRecoveryCopyGuard(targetPath, verifiedBytes,
			restoredGuard, restoredIdentity, restoredError) &&
			SameFileIdentity(restoredIdentity, verifiedIdentity);
		if (restoredGuard != INVALID_HANDLE_VALUE) CloseHandle(restoredGuard);
		if (restored) return true;
		AppendDetail(detail,
			"PDW restored the missing pathname, but a later change prevented byte-and-identity verification.");
		if (!restoredError.empty()) AppendDetail(detail, restoredError);
		return false;
	}

	struct FailedReplaceRecovery
	{
		FailedReplaceRecovery()
			: targetRestored(false), backupRetained(false), replacementRetained(false) {}
		bool targetRestored;
		bool backupRetained;
		bool replacementRetained;
		std::string backupRecoveryPath;
		std::string replacementRecoveryPath;
		std::string additionalRecoveryPath;
		std::string detail;
	};

	FailedReplaceRecovery RecoverFailedReplace(const std::string& targetPath,
		const std::string& replacementPath, const std::string& backupPath,
		const std::string& expectedCurrent, const FileIdentity& expectedCurrentIdentity,
		DWORD failureCode)
	{
		FailedReplaceRecovery recovery;
		const InspectedPath targetBefore = InspectPath(targetPath);

		if (targetBefore.missing)
		{
			recovery.targetRestored = RestoreMissingTargetFromDisplacedFile(targetPath,
				backupPath, expectedCurrent, expectedCurrentIdentity,
				recovery.additionalRecoveryPath, recovery.detail);
			if (recovery.targetRestored)
				AppendDetail(recovery.detail,
					"PDW restored the missing settings pathname from the guarded pre-commit file.");
			else
				AppendDetail(recovery.detail,
					"The settings pathname is missing; no unverified file was restored.");
		}
		else if (!targetBefore.exists && !targetBefore.missing)
		{
			AppendDetail(recovery.detail,
				"PDW could not determine whether the settings pathname still exists and did not attempt an overwrite.");
		}

		// After ReplaceFile reports failure, never delete a backup-path occupant by
		// pathname. It may be the only surviving operator object, including a genuine
		// zero-byte file in the documented error-1177 partial-move state.
		const InspectedPath backupAfter = InspectPath(backupPath);
		if (!backupAfter.missing)
		{
			recovery.backupRetained = true;
			recovery.backupRecoveryPath = backupPath;
			AppendDetail(recovery.detail,
				"The displaced backup was retained at \"" + backupPath + "\".");
		}

		// The target can disappear again after any pathname snapshot. Retain the
		// protected stage after every failed ReplaceFile call instead of risking
		// deletion of the last complete settings candidate.
		const InspectedPath replacementState = InspectPath(replacementPath);
		if (!replacementState.missing)
		{
			recovery.replacementRetained = true;
			recovery.replacementRecoveryPath = replacementPath;
			AppendDetail(recovery.detail,
				"The protected replacement-side file was retained at \"" +
				replacementPath + "\" after the failed transaction.");
		}
		if (failureCode == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
			AppendDetail(recovery.detail,
				"Windows reported error 1177 (replacement partial move).");
		return recovery;
	}

	SettingsTransactionOutcome StageAndReplace(const std::string& targetPath, const std::string& expectedCurrent,
		const std::string& replacement, std::string& error,
		const FileIdentity* expectedIdentity = NULL)
	{
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		g_lastConcurrentRecoveryPath.clear();
		g_lastSecondaryRecoveryPath.clear();
#endif
		HANDLE targetGuard = INVALID_HANDLE_VALUE;
		DWORD currentAttributes = 0;
		FileIdentity guardedTargetIdentity;
		if (!OpenVerifiedTargetGuard(targetPath, expectedCurrent, targetGuard,
			currentAttributes, guardedTargetIdentity, error))
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		if (expectedIdentity &&
			!SameFileIdentity(guardedTargetIdentity, *expectedIdentity))
		{
			CloseHandle(targetGuard);
			error = "The settings file identity changed while the transaction was being prepared.";
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_afterTargetGuardTestHook)
			g_afterTargetGuardTestHook(targetPath.c_str());
#endif
		std::string temporaryPath;
		HANDLE temporaryFile = INVALID_HANDLE_VALUE;
		if (!CreateTemporaryPath(targetPath, temporaryPath, targetGuard,
			temporaryFile, error))
		{
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_beforeSecureTempWriteTestHook)
			g_beforeSecureTempWriteTestHook(temporaryPath.c_str());
#endif
		if (!WriteOpenFileBytesDurably(temporaryFile, replacement, error))
		{
			MarkSensitiveStagedFileForDeletionAndClose(temporaryFile, temporaryPath, error);
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		if (!ApplyPortableAttributes(temporaryFile, currentAttributes, false, error))
		{
			MarkSensitiveStagedFileForDeletionAndClose(temporaryFile, temporaryPath, error);
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		std::string staged;
		if (!ReadOpenFileBytes(temporaryFile, staged, error) || staged != replacement)
		{
			if (error.empty()) error = "The staged file did not verify byte-for-byte.";
			MarkSensitiveStagedFileForDeletionAndClose(temporaryFile, temporaryPath, error);
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		BY_HANDLE_FILE_INFORMATION stagedInformation = {};
		if (!GetFileInformationByHandle(temporaryFile, &stagedInformation))
		{
			error = WindowsError("Reading protected staged-file identity", GetLastError());
			MarkSensitiveStagedFileForDeletionAndClose(temporaryFile, temporaryPath, error);
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		const FileIdentity stagedIdentity = IdentityFromInformation(stagedInformation);
		const bool targetWasReadOnly = (currentAttributes & FILE_ATTRIBUTE_READONLY) != 0;
		if (targetWasReadOnly && !SetGuardReadOnlyAttribute(targetGuard, false, error))
		{
			MarkSensitiveStagedFileForDeletionAndClose(temporaryFile, temporaryPath, error);
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		std::string displacedPath;
		if (!CreateVacantBackupPath(targetPath, displacedPath, error, targetGuard))
		{
			if (targetWasReadOnly)
			{
				std::string restoreError;
				if (!SetGuardReadOnlyAttribute(targetGuard, true, restoreError))
					AppendDetail(error, restoreError);
			}
			MarkSensitiveStagedFileForDeletionAndClose(temporaryFile, temporaryPath, error);
			CloseHandle(targetGuard);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		CloseHandle(temporaryFile);
		temporaryFile = INVALID_HANDLE_VALUE;
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
		if (g_beforeReplaceTestHook) g_beforeReplaceTestHook(targetPath.c_str());
		if (g_beforeStagedCommitTestHook)
			g_beforeStagedCommitTestHook(temporaryPath.c_str());
#endif
		// Always ask ReplaceFileA to retain the pathname occupant that is actually
		// displaced. If another process atomically swapped the path after our guarded
		// read, those operator bytes survive in displacedPath instead of being lost.
		if (!InvokeReplaceFile(targetPath.c_str(), temporaryPath.c_str(), displacedPath.c_str(),
			REPLACEFILE_WRITE_THROUGH))
		{
			const DWORD code = GetLastError();
			if (targetWasReadOnly)
			{
				std::string restoreError;
				if (!SetGuardReadOnlyAttribute(targetGuard, true, restoreError))
					AppendDetail(error, restoreError);
			}
			std::string commitError = WindowsError("Committing the settings transaction", code);
			if (error.empty()) error = commitError;
			else error = commitError + " " + error;
			const FailedReplaceRecovery failureRecovery = RecoverFailedReplace(targetPath,
				temporaryPath, displacedPath, expectedCurrent, guardedTargetIdentity, code);
			CloseHandle(targetGuard);
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
			if (failureRecovery.backupRetained)
				g_lastConcurrentRecoveryPath = failureRecovery.backupRecoveryPath;
			else if (!failureRecovery.additionalRecoveryPath.empty())
				g_lastConcurrentRecoveryPath = failureRecovery.additionalRecoveryPath;
			if (failureRecovery.replacementRetained)
				g_lastSecondaryRecoveryPath = failureRecovery.replacementRecoveryPath;
#endif
			if (!failureRecovery.detail.empty()) error += " " + failureRecovery.detail;
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}
		HANDLE committedStageGuard = INVALID_HANDLE_VALUE;
		FileIdentity committedStageIdentity;
		std::string committedStageError;
		const bool committedStageVerified = OpenVerifiedRecoveryCopyGuard(targetPath,
			replacement, committedStageGuard, committedStageIdentity, committedStageError) &&
			SameFileIdentity(committedStageIdentity, stagedIdentity);
		if (!committedStageVerified)
		{
			if (committedStageGuard != INVALID_HANDLE_VALUE)
				CloseHandle(committedStageGuard);
			CloseHandle(targetGuard);
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
			g_lastConcurrentRecoveryPath = displacedPath;
#endif
			error = "Windows reported that the settings replacement completed, but PDW could not "
				"verify the protected staged bytes and file identity at the active path. "
				"PDW did not perform any post-commit replacement or rollback. The displaced "
				"file was retained at \"" + displacedPath + "\".";
			if (!committedStageError.empty()) AppendDetail(error, committedStageError);
			return SETTINGS_TRANSACTION_NOT_COMMITTED;
		}

		// The commit point is the verified staged bytes and staged file identity while
		// the active path is pinned against deletion. From here onward PDW never
		// replaces or rolls back the active path. Cleanup can only remove the exact
		// guarded pre-commit object; every other occupant is retained and reported.
		HANDLE displacedHandle = INVALID_HANDLE_VALUE;
		InspectedPath displacedState;
		std::string warning;
		if (OpenPinnedInspectedPath(displacedPath, displacedHandle, displacedState) &&
			displacedState.bytes == expectedCurrent &&
			SameFileIdentity(displacedState.identity, guardedTargetIdentity))
		{
			if (!DeletePinnedVerifiedPath(displacedPath, displacedHandle, displacedState,
				expectedCurrent, guardedTargetIdentity,
				"Removing the verified displaced settings file", warning))
			{
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
				g_lastConcurrentRecoveryPath = displacedPath;
#endif
			}
		}
		else
		{
			if (!displacedState.error.empty()) AppendDetail(warning, displacedState.error);
			AppendDetail(warning,
				"The displaced settings object was not the exact guarded pre-commit file and "
				"was retained at \"" + displacedPath + "\".");
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
			g_lastConcurrentRecoveryPath = displacedPath;
#endif
		}
		if (displacedHandle != INVALID_HANDLE_VALUE) CloseHandle(displacedHandle);
		CloseHandle(committedStageGuard);
		CloseHandle(targetGuard);
		if (!warning.empty())
		{
			error = "PDW committed and verified the new settings, but transaction cleanup "
				"requires operator attention. " + warning;
			return SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING;
		}
		error.clear();
		return SETTINGS_TRANSACTION_COMMITTED;
	}

	SettingsTransactionOutcome CommitAndVerify(const std::string& targetPath,
		const std::string& original,
		const std::string& replacement, std::string& error,
		const FileIdentity* expectedIdentity = NULL)
	{
		return StageAndReplace(targetPath, original, replacement, error,
			expectedIdentity);
	}

	std::string ChooseBackupPath(const std::string& iniPath)
	{
		SYSTEMTIME localTime = {};
		GetLocalTime(&localTime);
		char stamp[32] = {};
		_snprintf_s(stamp, sizeof(stamp), _TRUNCATE, "%04u%02u%02u-%02u%02u%02u",
			static_cast<unsigned int>(localTime.wYear),
			static_cast<unsigned int>(localTime.wMonth),
			static_cast<unsigned int>(localTime.wDay),
			static_cast<unsigned int>(localTime.wHour),
			static_cast<unsigned int>(localTime.wMinute),
			static_cast<unsigned int>(localTime.wSecond));
		const std::string base = iniPath + ".pre-adelaide-flex-" + stamp + ".bak";
		if (GetFileAttributesA(base.c_str()) == INVALID_FILE_ATTRIBUTES) return base;
		for (unsigned int suffix = 2; suffix < 10000; ++suffix)
		{
			std::ostringstream candidate;
			candidate << base << '.' << suffix;
			if (GetFileAttributesA(candidate.str().c_str()) == INVALID_FILE_ATTRIBUTES)
				return candidate.str();
		}
		return std::string();
	}

	bool CreateVerifiedBackup(const std::string& sourcePath, const std::string& original,
		std::string& backupPath, std::string& error)
	{
		backupPath = ChooseBackupPath(sourcePath);
		if (backupPath.empty())
		{
			error = "PDW could not choose a unique settings-backup path.";
			return false;
		}
		if (!CopyFileA(sourcePath.c_str(), backupPath.c_str(), TRUE))
		{
			error = WindowsError("Creating the settings backup", GetLastError());
			backupPath.clear();
			return false;
		}
		std::string backup;
		DWORD ignoredAttributes = 0;
		if (!ReadFileBytes(backupPath, backup, ignoredAttributes, error) || backup != original)
		{
			if (error.empty()) error = "The settings backup was not byte-for-byte identical.";
			error += " The unverified backup was not deleted and remains at \"" +
				backupPath + "\".";
			return false;
		}
		return true;
	}

	int HexValue(char character)
	{
		if (character >= '0' && character <= '9') return character - '0';
		if (character >= 'a' && character <= 'f') return character - 'a' + 10;
		if (character >= 'A' && character <= 'F') return character - 'A' + 10;
		return -1;
	}
}

bool CreateSecureTemporarySettingsFile(const std::string& referencePath,
	const char* prefix, std::string& temporaryPath, void*& nativeHandle,
	std::string& error)
{
	error.clear();
	temporaryPath.clear();
	nativeHandle = INVALID_HANDLE_VALUE;
	HANDLE file = INVALID_HANDLE_VALUE;
	const bool created = CreateSecureTemporaryPath(referencePath, prefix, referencePath,
		temporaryPath, INVALID_HANDLE_VALUE, file, error);
	if (created) nativeHandle = file;
	return created;
}

bool MarkSensitiveTemporaryFileForDeletion(void* nativeHandle,
	const std::string& path, std::string& error)
{
	error.clear();
	HANDLE file = static_cast<HANDLE>(nativeHandle);
	if (file == NULL || file == INVALID_HANDLE_VALUE || path.empty())
	{
		error = "The sensitive settings file handle or path is invalid.";
		return false;
	}
#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
	if (g_deleteFileFailureTestHook && g_deleteFileFailureTestHook(path.c_str()))
	{
		error = WindowsError("Marking the sensitive settings file for deletion",
			ERROR_ACCESS_DENIED) + " The sensitive file was retained at \"" + path + "\".";
		return false;
	}
#endif
	FILE_DISPOSITION_INFO disposition = {};
	disposition.DeleteFile = TRUE;
	if (!SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
		sizeof(disposition)))
	{
		error = WindowsError("Marking the sensitive settings file for deletion",
			GetLastError()) + " The sensitive file was retained at \"" + path + "\".";
		return false;
	}
	return true;
}

CaptureDevice::CaptureDevice() : winmmIndex(-1) {}

DeviceResolution::DeviceResolution()
	: found(false), usedCompatibilityIndex(false), winmmIndex(-1) {}

DecoderProfileValues AdelaideFlexPreset()
{
	DecoderProfileValues values = {};
	values.audioEnabled = 1;
	values.audioSource = 0;
	values.audioDevice = 0;
	values.audioSampleRate = 44100;
	values.audioConfiguration = 0;
	values.comPortEnabled = 0;
	values.decodePocsag = 1;
	values.decodeFlex = 1;
	values.monitorPaging = 1;
	values.pocsagShowBoth = 1;
	values.showCfs = 1;
	values.flex1600 = 1;
	values.flex3200 = 0;
	values.flex6400 = 0;
	values.bitSync = 13107;
	values.minimumMessageLength = 15;
	values.invertData = 1;
	values.panePercent = 69;
	values.threshold1600 = 2;
	values.auxiliaryCustomValuesAreDefault = 1;
	return values;
}

bool IsAdelaideFlexProfile(const DecoderProfileValues& values)
{
	const DecoderProfileValues preset = AdelaideFlexPreset();
	return values.audioEnabled == preset.audioEnabled &&
		values.audioSource == preset.audioSource &&
		values.audioSampleRate == preset.audioSampleRate &&
		values.audioConfiguration == preset.audioConfiguration &&
		values.comPortEnabled == preset.comPortEnabled &&
		values.decodePocsag == preset.decodePocsag &&
		values.decodeFlex == preset.decodeFlex &&
		values.monitorPaging == preset.monitorPaging &&
		values.pocsagShowBoth == preset.pocsagShowBoth &&
		values.showCfs == preset.showCfs &&
		values.flex1600 == preset.flex1600 &&
		values.flex3200 == preset.flex3200 &&
		values.flex6400 == preset.flex6400 &&
		values.bitSync == preset.bitSync &&
		values.minimumMessageLength == preset.minimumMessageLength &&
		values.invertData == preset.invertData &&
		values.panePercent == preset.panePercent &&
		values.threshold1600 == preset.threshold1600 &&
		values.auxiliaryCustomValuesAreDefault == preset.auxiliaryCustomValuesAreDefault;
}

bool ValidateUtf8Field(const std::string& value, std::size_t maximumUtf8Bytes,
	std::string& error)
{
	error.clear();
	if (value.size() > maximumUtf8Bytes)
	{
		error = "The UTF-8 field exceeds its byte limit.";
		return false;
	}
	for (std::size_t index = 0; index < value.size();)
	{
		const unsigned char first = static_cast<unsigned char>(value[index]);
		unsigned int codePoint = 0;
		std::size_t length = 0;
		if (first <= 0x7f)
		{
			codePoint = first;
			length = 1;
		}
		else if (first >= 0xc2 && first <= 0xdf)
		{
			codePoint = first & 0x1f;
			length = 2;
		}
		else if (first >= 0xe0 && first <= 0xef)
		{
			codePoint = first & 0x0f;
			length = 3;
		}
		else if (first >= 0xf0 && first <= 0xf4)
		{
			codePoint = first & 0x07;
			length = 4;
		}
		else
		{
			error = "The field contains invalid UTF-8.";
			return false;
		}
		if (index + length > value.size())
		{
			error = "The field contains truncated UTF-8.";
			return false;
		}
		for (std::size_t continuation = 1; continuation < length; ++continuation)
		{
			const unsigned char byte = static_cast<unsigned char>(value[index + continuation]);
			if ((byte & 0xc0) != 0x80)
			{
				error = "The field contains invalid UTF-8 continuation bytes.";
				return false;
			}
			codePoint = (codePoint << 6) | (byte & 0x3f);
		}
		if ((length == 2 && codePoint < 0x80) ||
			(length == 3 && codePoint < 0x800) ||
			(length == 4 && codePoint < 0x10000) ||
			(codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff)
		{
			error = "The field contains a non-canonical UTF-8 code point.";
			return false;
		}
		if (codePoint <= 0x1f || codePoint == 0x7f)
		{
			error = "The field contains a prohibited control character.";
			return false;
		}
		index += length;
	}
	return true;
}

bool EncodeIniUtf8Field(const std::string& value, std::size_t maximumUtf8Bytes,
	std::string& encoded, std::string& error)
{
	encoded.clear();
	if (!ValidateUtf8Field(value, maximumUtf8Bytes, error)) return false;
	static const char digits[] = "0123456789ABCDEF";
	encoded.assign(UTF8_HEX_PREFIX);
	encoded.reserve(encoded.size() + value.size() * 2);
	for (std::size_t index = 0; index < value.size(); ++index)
	{
		const unsigned char byte = static_cast<unsigned char>(value[index]);
		encoded.push_back(digits[byte >> 4]);
		encoded.push_back(digits[byte & 0x0f]);
	}
	return true;
}

bool DecodeIniUtf8Field(const std::string& encoded, std::size_t maximumUtf8Bytes,
	std::string& value, std::string& error)
{
	value.clear();
	error.clear();
	const std::size_t prefixLength = std::char_traits<char>::length(UTF8_HEX_PREFIX);
	if (encoded.size() < prefixLength || encoded.compare(0, prefixLength, UTF8_HEX_PREFIX) != 0)
	{
		error = "The INI field is not utf8-hex encoded.";
		return false;
	}
	const std::size_t hexLength = encoded.size() - prefixLength;
	if (hexLength % 2 != 0 || hexLength / 2 > maximumUtf8Bytes)
	{
		error = "The INI field has an invalid or overlength utf8-hex payload.";
		return false;
	}
	value.reserve(hexLength / 2);
	for (std::size_t index = prefixLength; index < encoded.size(); index += 2)
	{
		const int high = HexValue(encoded[index]);
		const int low = HexValue(encoded[index + 1]);
		if (high < 0 || low < 0)
		{
			value.clear();
			error = "The INI field contains a non-hexadecimal byte.";
			return false;
		}
		value.push_back(static_cast<char>((high << 4) | low));
	}
	if (!ValidateUtf8Field(value, maximumUtf8Bytes, error))
	{
		value.clear();
		return false;
	}
	return true;
}

bool FriendlyDeviceNamesMatch(const std::string& first, const std::string& second)
{
	std::string error;
	if (first.empty() || second.empty() ||
		!ValidateUtf8Field(first, MAX_ENDPOINT_NAME_UTF8_BYTES, error) ||
		!ValidateUtf8Field(second, MAX_ENDPOINT_NAME_UTF8_BYTES, error)) return false;
	if (first == second) return true;
	const std::string normalizedFirst = NormalizeFriendlyName(first);
	const std::string normalizedSecond = NormalizeFriendlyName(second);
	if (!normalizedFirst.empty() && normalizedFirst == normalizedSecond) return true;
	return IsWinmmTruncatedName(first, second) || IsWinmmTruncatedName(second, first);
}

DeviceResolution ResolveCaptureDevice(const std::vector<CaptureDevice>& devices,
	const std::string& preferredEndpointId,
	const std::string& preferredFriendlyName,
	int compatibilityIndex,
	bool allowCompatibilityFallback)
{
	DeviceResolution resolution;
	std::string error;
	if (!preferredEndpointId.empty())
	{
		if (!ValidateUtf8Field(preferredEndpointId, MAX_ENDPOINT_ID_UTF8_BYTES, error))
			return resolution;
		const CaptureDevice* match = NULL;
		for (std::vector<CaptureDevice>::const_iterator device = devices.begin();
			device != devices.end(); ++device)
		{
			if (!IsSafeDevice(*device) || device->endpointId != preferredEndpointId) continue;
			if (!match || (match->winmmIndex < 0 && device->winmmIndex >= 0)) match = &*device;
		}
		if (match) FillResolution(resolution, *match, false);
		// A persisted endpoint identity is authoritative. Its friendly name and
		// compatibility ordinal must never redirect it to a different endpoint.
		return resolution;
	}

	if (!preferredFriendlyName.empty())
	{
		if (!ValidateUtf8Field(preferredFriendlyName, MAX_ENDPOINT_NAME_UTF8_BYTES, error))
			return resolution;
		const CaptureDevice* match = NULL;
		std::string matchedEndpointId;
		for (std::vector<CaptureDevice>::const_iterator device = devices.begin();
			device != devices.end(); ++device)
		{
			if (device->endpointId.empty() || !IsSafeDevice(*device) ||
				!FriendlyDeviceNamesMatch(device->friendlyName, preferredFriendlyName)) continue;
			if (!match)
			{
				match = &*device;
				matchedEndpointId = device->endpointId;
				continue;
			}
			if (device->endpointId != matchedEndpointId) return resolution;
			if (match->winmmIndex < 0 && device->winmmIndex >= 0) match = &*device;
		}
		if (match)
		{
			FillResolution(resolution, *match, false);
			return resolution;
		}
	}

	if (!allowCompatibilityFallback) return resolution;
	const CaptureDevice* compatibilityMatch = NULL;
	for (std::vector<CaptureDevice>::const_iterator device = devices.begin();
		device != devices.end(); ++device)
	{
		if (device->winmmIndex != compatibilityIndex || !IsSafeDevice(*device)) continue;
		if (!compatibilityMatch)
		{
			compatibilityMatch = &*device;
			continue;
		}
		if (device->endpointId != compatibilityMatch->endpointId ||
			device->friendlyName != compatibilityMatch->friendlyName) return resolution;
	}
	if (compatibilityMatch) FillResolution(resolution, *compatibilityMatch, true);
	return resolution;
}

CaptureDeviceSaveDecision DecideCaptureDeviceSave(bool explicitSelectionMade,
	bool hasConfiguredStableEndpoint, bool configuredStableEndpointResolved)
{
	if (explicitSelectionMade) return CAPTURE_SAVE_BIND_EXPLICIT_SELECTION;
	if (hasConfiguredStableEndpoint && !configuredStableEndpointResolved)
		return CAPTURE_SAVE_REJECT_UNAVAILABLE_STABLE_IDENTITY;
	return CAPTURE_SAVE_KEEP_CONFIGURED_IDENTITY;
}

bool SettingsTransactionCommitted(SettingsTransactionOutcome outcome)
{
	return outcome == SETTINGS_TRANSACTION_COMMITTED ||
		outcome == SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING;
}

SettingsTransactionOutcome ApplyAdelaideFlexPresetToIni(const std::string& iniPath,
	const DeviceResolution& resolvedDevice, std::string& backupPath, std::string& error)
{
	backupPath.clear();
	error.clear();
	std::vector<IniSetting> settings;
	if (!BuildPresetSettings(resolvedDevice, settings, error))
		return SETTINGS_TRANSACTION_NOT_COMMITTED;

	std::string fullPath;
	if (!ResolveFullPath(iniPath, fullPath, error))
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	std::string original;
	DWORD attributes = 0;
	FileIdentity originalIdentity;
	HANDLE originalGuard = INVALID_HANDLE_VALUE;
	if (!OpenPinnedFileSnapshot(fullPath, originalGuard, original, attributes,
		originalIdentity, error)) return SETTINGS_TRANSACTION_NOT_COMMITTED;
	if (attributes & FILE_ATTRIBUTE_READONLY)
	{
		CloseHandle(originalGuard);
		error = "The settings file is read-only; no changes were made.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	const bool backupCreated = CreateVerifiedBackup(fullPath, original, backupPath, error);
	CloseHandle(originalGuard);
	if (!backupCreated) return SETTINGS_TRANSACTION_NOT_COMMITTED;

	const std::string merged = MergePresetSettings(original, settings);
	if (merged.empty() || merged.size() > MAX_INI_BYTES)
	{
		error = "The merged settings file is empty or too large.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	if (!VerifyPresetContents(merged, settings, error))
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	return CommitAndVerify(fullPath, original, merged, error, &originalIdentity);
}

SettingsTransactionOutcome RestoreIniFromVerifiedBackup(const std::string& iniPath,
	const std::string& backupPath, std::string& error)
{
	error.clear();
	std::string fullTarget;
	std::string fullBackup;
	if (!ResolveFullPath(iniPath, fullTarget, error) ||
		!ResolveFullPath(backupPath, fullBackup, error))
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	if (LowerAscii(fullTarget) == LowerAscii(fullBackup))
	{
		error = "The settings file and backup must be different files.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}

	std::string current;
	std::string backup;
	DWORD currentAttributes = 0;
	DWORD backupAttributes = 0;
	if (!ReadFileBytes(fullTarget, current, currentAttributes, error) ||
		!ReadFileBytes(fullBackup, backup, backupAttributes, error))
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	if (currentAttributes & FILE_ATTRIBUTE_READONLY)
	{
		error = "The settings file is read-only; no changes were made.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}

	// Re-read before staging to detect a backup that changed during verification.
	std::string verifiedBackup;
	DWORD ignoredAttributes = 0;
	if (!ReadFileBytes(fullBackup, verifiedBackup, ignoredAttributes, error) ||
		verifiedBackup != backup)
	{
		if (error.empty()) error = "The backup changed while it was being verified.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	return CommitAndVerify(fullTarget, current, backup, error);
}

SettingsTransactionOutcome CommitVerifiedFileTransaction(const std::string& path,
	const std::string& expectedCurrent, const std::string& replacement,
	std::string& error)
{
	error.clear();
	if (path.empty() || replacement.empty() || replacement.size() > MAX_INI_BYTES)
	{
		error = "The replacement file is empty or too large.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	return CommitAndVerify(path, expectedCurrent, replacement, error);
}

SettingsTransactionOutcome CommitVerifiedFileTransactionForIdentity(const std::string& path,
	const std::string& expectedCurrent, const std::string& replacement,
	unsigned long volumeSerial, unsigned long fileIndexHigh,
	unsigned long fileIndexLow, std::string& error)
{
	error.clear();
	if (path.empty() || replacement.empty() || replacement.size() > MAX_INI_BYTES)
	{
		error = "The replacement file is empty or too large.";
		return SETTINGS_TRANSACTION_NOT_COMMITTED;
	}
	FileIdentity expectedIdentity;
	expectedIdentity.valid = true;
	expectedIdentity.volumeSerial = static_cast<DWORD>(volumeSerial);
	expectedIdentity.fileIndexHigh = static_cast<DWORD>(fileIndexHigh);
	expectedIdentity.fileIndexLow = static_cast<DWORD>(fileIndexLow);
	return CommitAndVerify(path, expectedCurrent, replacement, error,
		&expectedIdentity);
}

} // namespace audio_profile
} // namespace pdw
