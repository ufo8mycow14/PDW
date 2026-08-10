#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>

#include "ini_preservation_core.h"

namespace
{
	int failures = 0;

	void Expect(bool condition, const char* description)
	{
		if (condition) return;
		std::cerr << "FAILED: " << description << '\n';
		++failures;
	}

	std::size_t Count(const std::string& text, const std::string& value)
	{
		std::size_t count = 0;
		std::size_t position = 0;
		while ((position = text.find(value, position)) != std::string::npos)
		{
			++count;
			position += value.size();
		}
		return count;
	}

	std::string ReadFile(const char* path)
	{
		std::ifstream input(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
	}

	bool HasBareLineFeed(const std::string& text)
	{
		for (std::size_t index = 0; index < text.size(); ++index)
			if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r')) return true;
		return false;
	}
}

int main()
{
	const std::string existing =
		"; operator note\r\n"
		"[PDW]\r\n"
		"xPos=99\r\n"
		"FutureLegacyFlag=keep\r\n"
		"xPos=duplicate\r\n"
		"LogfilePath=C:\\old-default\r\n"
		"\r\n"
		"[FTP]\r\n"
		"Enable=1\r\n"
		"FileCount=3\r\n"
		"File1=C:\\old-a.txt\r\n"
		"File2=C:\\old-b.txt\r\n"
		"VendorOption=yes\r\n"
		"\r\n"
		"[ThirdPartyDecoder]\r\n"
		"PrivateFutureSetting=unchanged\r\n";
	const std::string generated =
		"[PDW]\n"
		"xPos=10\n"
		"yPos=20\n"
		"\n"
		"[FTP]\n"
		"Enable=0\n"
		"FileCount=1\n"
		"File1=C:\\new.txt\n"
		"\n"
		"[Filter]\n"
		"FilterCmdArgs=\"/legacy %1\"\n"
		"\n"
		"[OutputHealth]\n"
		"AlertsEnabled=1\n"
		"FailureThreshold=3\n";

	const std::string merged = pdw::ini::MergeKnownSettings(existing, generated);
	Expect(merged.find("; operator note\r\n") != std::string::npos,
		"comments are preserved");
	Expect(merged.find("FutureLegacyFlag=keep") != std::string::npos,
		"unknown keys in known sections are preserved");
	Expect(merged.find("[ThirdPartyDecoder]\r\nPrivateFutureSetting=unchanged") != std::string::npos,
		"unknown sections are preserved");
	Expect(merged.find("xPos=10") != std::string::npos && Count(merged, "xPos=") == 1,
		"known duplicate keys collapse to the authoritative value");
	Expect(merged.find("yPos=20") != std::string::npos,
		"missing known keys are added");
	Expect(merged.find("LogfilePath=") == std::string::npos,
		"reset-to-default optional keys are removed");
	Expect(merged.find("File1=C:\\new.txt") != std::string::npos &&
		merged.find("File2=") == std::string::npos,
		"stale dynamic FTP file entries are removed");
	Expect(merged.find("VendorOption=yes") != std::string::npos,
		"unrecognized FTP extensions remain available");
	Expect(merged.find("FilterCmdArgs=\"/legacy %1\"") != std::string::npos,
		"quoted legacy command arguments round-trip exactly");
	Expect(merged.find("[OutputHealth]\r\nAlertsEnabled=1") != std::string::npos,
		"new known sections are appended");
	Expect(!HasBareLineFeed(merged),
		"existing CRLF style is retained");
	Expect(pdw::ini::MergeKnownSettings(merged, generated) == merged,
		"repeated settings saves are byte-stable");

	const std::string bom("\xef\xbb\xbf", 3);
	const std::string bomMerged = pdw::ini::MergeKnownSettings(
		bom + "[pdw]\r\nXPOS=1\r\nUnknown=2\r\n", "[PDW]\nxPos=7\n");
	Expect(bomMerged.compare(0, 3, bom) == 0, "UTF-8 BOM is retained");
	Expect(bomMerged.find("xPos=7") != std::string::npos &&
		bomMerged.find("Unknown=2") != std::string::npos,
		"section and key matching is case-insensitive");

	char temporary[MAX_PATH] = {};
	char tempDirectory[MAX_PATH] = {};
	GetTempPathA(static_cast<DWORD>(sizeof(tempDirectory)), tempDirectory);
	Expect(GetTempFileNameA(tempDirectory, "PIT", 0, temporary) != 0,
		"test INI path is created");
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		output << existing;
	}
	std::string error;
	Expect(pdw::ini::WriteMergedSettingsFile(temporary, generated, error),
		"merged INI is written atomically");
	const std::string disk = ReadFile(temporary);
	Expect(disk.find("FutureLegacyFlag=keep") != std::string::npos &&
		disk.find("xPos=10") != std::string::npos,
		"atomic file output contains preserved and updated settings");
	SetFileAttributesA(temporary, FILE_ATTRIBUTE_READONLY);
	Expect(pdw::ini::WriteMergedSettingsFile(temporary, generated, error),
		"legacy read-only INI can still be updated");
	Expect((GetFileAttributesA(temporary) & FILE_ATTRIBUTE_READONLY) == 0,
		"legacy save behavior clears the read-only attribute");
	DeleteFileA(temporary);

	std::string newPath = std::string(temporary) + ".new";
	DeleteFileA(newPath.c_str());
	Expect(pdw::ini::WriteMergedSettingsFile(newPath, generated, error),
		"missing INI file is created from generated settings");
	Expect(ReadFile(newPath.c_str()).find("[OutputHealth]") != std::string::npos,
		"new INI contains all generated sections");
	DeleteFileA(newPath.c_str());

	if (failures) return 1;
	std::cout << "INI preservation tests passed.\n";
	return 0;
}
