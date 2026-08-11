#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ini_preservation_core.h"
#include "local_audio_profile_core.h"

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

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
	const char ATOMIC_OPERATOR_BYTES[] =
		"[OperatorOwned]\r\nConcurrentPathSwap=must-survive\r\n";
	bool g_atomicSwapSucceeded = false;
	DWORD g_atomicSwapError = ERROR_SUCCESS;
	unsigned int g_replaceCallCount = 0;
	bool g_partialMoveSucceeded = false;
	DWORD g_partialMoveError = ERROR_SUCCESS;
	std::string g_deleteFailurePath;
	std::string g_missingStagePath;
	bool g_missingTargetOccupied = false;
	bool g_readOnlySwapSucceeded = false;
	DWORD g_readOnlySwapError = ERROR_SUCCESS;
	bool g_missingStageSwapSucceeded = false;
	DWORD g_missingStageSwapError = ERROR_SUCCESS;
	bool g_existingIdentitySwapSucceeded = false;
	DWORD g_existingIdentitySwapError = ERROR_SUCCESS;
	std::string g_existingIdentitySwapBytes;
	const char READ_ONLY_OPERATOR_BYTES[] =
		"[OperatorOwned]\r\nReadOnlySwap=must-survive\r\n";

	void WriteFileBytes(const std::string& path, const std::string& bytes)
	{
		std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	void AtomicallySwapOperatorFile(const char* path)
	{
		const std::string replacement = std::string(path) + ".operator-swap";
		DeleteFileA(replacement.c_str());
		WriteFileBytes(replacement, ATOMIC_OPERATOR_BYTES);
		g_atomicSwapSucceeded = ReplaceFileA(path, replacement.c_str(), NULL,
			REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
		if (!g_atomicSwapSucceeded)
		{
			g_atomicSwapError = GetLastError();
			DeleteFileA(replacement.c_str());
		}
	}

	bool InjectInitialPartialMove(const char* replacedPath,
		const char*, const char* backupPath, unsigned long)
	{
		++g_replaceCallCount;
		g_partialMoveSucceeded = MoveFileExA(replacedPath, backupPath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
		if (!g_partialMoveSucceeded)
		{
			g_partialMoveError = GetLastError();
			SetLastError(g_partialMoveError);
			return false;
		}
		SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2);
		return false;
	}

	bool FailSelectedDelete(const char* path)
	{
		return path && !g_deleteFailurePath.empty() && g_deleteFailurePath == path;
	}

	void OccupyMissingTargetBeforeCommit(const char* targetPath, const char* stagedPath)
	{
		g_missingStagePath = stagedPath ? stagedPath : "";
		WriteFileBytes(targetPath, "[OperatorOwned]\r\nFirstRunRace=must-survive\r\n");
		g_missingTargetOccupied = GetFileAttributesA(targetPath) != INVALID_FILE_ATTRIBUTES;
		g_deleteFailurePath = g_missingStagePath;
	}

	void AtomicallySwapReadOnlyOperatorFile(const char* path)
	{
		const std::string replacement = std::string(path) + ".readonly-operator";
		DeleteFileA(replacement.c_str());
		WriteFileBytes(replacement, READ_ONLY_OPERATOR_BYTES);
		g_readOnlySwapSucceeded = ReplaceFileA(path, replacement.c_str(), NULL,
			REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
		if (g_readOnlySwapSucceeded)
			SetFileAttributesA(path, FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE);
		else
		{
			g_readOnlySwapError = GetLastError();
			DeleteFileA(replacement.c_str());
		}
	}

	void AtomicallySwapMissingStageBeforeCommit(const char*, const char* stagedPath)
	{
		g_missingStagePath = stagedPath ? stagedPath : "";
		const std::string replacement = g_missingStagePath + ".identity-swap";
		DeleteFileA(replacement.c_str());
		WriteFileBytes(replacement, ReadFile(g_missingStagePath.c_str()));
		g_missingStageSwapSucceeded = ReplaceFileA(g_missingStagePath.c_str(),
			replacement.c_str(), NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
		if (!g_missingStageSwapSucceeded)
		{
			g_missingStageSwapError = GetLastError();
			DeleteFileA(replacement.c_str());
		}
	}

	void AtomicallySwapExistingIdentityBeforeCommit(const char* path)
	{
		const std::string replacement = std::string(path) + ".existing-identity-swap";
		DeleteFileA(replacement.c_str());
		WriteFileBytes(replacement, g_existingIdentitySwapBytes);
		g_existingIdentitySwapSucceeded = ReplaceFileA(path, replacement.c_str(), NULL,
			REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
		if (!g_existingIdentitySwapSucceeded)
		{
			g_existingIdentitySwapError = GetLastError();
			DeleteFileA(replacement.c_str());
		}
	}
#endif
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

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
	WriteFileBytes(temporary, existing);
	Expect(SetFileAttributesA(temporary, FILE_ATTRIBUTE_READONLY) != FALSE,
		"read-only swap regression marks the original target read-only");
	g_readOnlySwapSucceeded = false;
	g_readOnlySwapError = ERROR_SUCCESS;
	pdw::audio_profile::SetBeforeReplaceTestHook(AtomicallySwapReadOnlyOperatorFile);
	const bool readOnlySwapWrite = pdw::ini::WriteMergedSettingsFile(temporary,
		generated, error);
	pdw::audio_profile::SetBeforeReplaceTestHook(NULL);
	Expect(g_readOnlySwapSucceeded,
		"read-only swap regression atomically replaces the pathname occupant");
	Expect(!readOnlySwapWrite && ReadFile(temporary) == READ_ONLY_OPERATOR_BYTES,
		"guarded read-only handling fails closed without overwriting operator bytes");
	Expect((GetFileAttributesA(temporary) & FILE_ATTRIBUTE_READONLY) != 0,
		"guarded read-only handling never clears the swapped operator file attribute");
	SetFileAttributesA(temporary, FILE_ATTRIBUTE_NORMAL);
	WriteFileBytes(temporary, existing);
	g_existingIdentitySwapBytes = existing;
	g_existingIdentitySwapSucceeded = false;
	g_existingIdentitySwapError = ERROR_SUCCESS;
	pdw::ini::SetBeforeExistingIniCommitTestHook(
		AtomicallySwapExistingIdentityBeforeCommit);
	const bool existingIdentityWrite = pdw::ini::WriteMergedSettingsFile(temporary,
		generated, error);
	pdw::ini::SetBeforeExistingIniCommitTestHook(NULL);
	Expect(g_existingIdentitySwapSucceeded,
		"existing INI identity regression performs the same-byte pathname swap");
	Expect(!existingIdentityWrite && ReadFile(temporary) == existing &&
		error.find("identity changed") != std::string::npos,
		"general INI save rejects a same-byte different-file swap before staging");
	WriteFileBytes(temporary, existing);

	std::string sensitivePath;
	std::string sensitiveError;
	void* sensitiveNativeHandle = INVALID_HANDLE_VALUE;
	Expect(pdw::ini::CreateSensitiveSettingsTemporaryFile(temporary, "PDS",
		sensitivePath, sensitiveNativeHandle, sensitiveError),
		"protected sensitive settings snapshot is created");
	const std::string sensitiveBytes = "[SMTP]\r\nPassword=secret-must-not-leak\r\n";
	DWORD sensitiveWritten = 0;
	HANDLE sensitiveHandle = static_cast<HANDLE>(sensitiveNativeHandle);
	Expect(WriteFile(sensitiveHandle, sensitiveBytes.data(),
		static_cast<DWORD>(sensitiveBytes.size()), &sensitiveWritten, NULL) != FALSE &&
		sensitiveWritten == static_cast<DWORD>(sensitiveBytes.size()) &&
		FlushFileBuffers(sensitiveHandle) != FALSE,
		"protected sensitive settings snapshot bytes are flushed");
	std::string deleteOnClosePath;
	std::string deleteOnCloseError;
	void* deleteOnCloseNativeHandle = INVALID_HANDLE_VALUE;
	Expect(pdw::ini::CreateSensitiveSettingsTemporaryFile(temporary, "PDS",
		deleteOnClosePath, deleteOnCloseNativeHandle, deleteOnCloseError),
		"protected delete-on-close success fixture is created");
	HANDLE deleteOnCloseHandle = static_cast<HANDLE>(deleteOnCloseNativeHandle);
	Expect(pdw::ini::MarkSensitiveSettingsTemporaryFileForDeletion(
		deleteOnCloseHandle, deleteOnClosePath, deleteOnCloseError),
		"a protected sensitive settings file can be marked for exact-handle deletion");
	CloseHandle(deleteOnCloseHandle);
	SetLastError(ERROR_SUCCESS);
	Expect(GetFileAttributesA(deleteOnClosePath.c_str()) == INVALID_FILE_ATTRIBUTES &&
		(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND),
		"the exact marked sensitive file disappears when its owning handle closes");
	g_deleteFailurePath = sensitivePath;
	pdw::audio_profile::SetDeleteFileFailureTestHook(FailSelectedDelete);
	const bool sensitiveRemoved = pdw::ini::MarkSensitiveSettingsTemporaryFileForDeletion(
		sensitiveHandle, sensitivePath, sensitiveError);
	pdw::audio_profile::SetDeleteFileFailureTestHook(NULL);
	CloseHandle(sensitiveHandle);
	Expect(!sensitiveRemoved && ReadFile(sensitivePath.c_str()) == sensitiveBytes,
		"injected sensitive snapshot cleanup failure retains the exact bytes");
	Expect(sensitiveError.find(sensitivePath) != std::string::npos,
		"sensitive snapshot cleanup failure reports the exact retained path");
	DeleteFileA(sensitivePath.c_str());
	g_deleteFailurePath.clear();

	WriteFileBytes(temporary, existing);
	g_replaceCallCount = 0;
	g_partialMoveSucceeded = false;
	g_partialMoveError = ERROR_SUCCESS;
	pdw::audio_profile::SetReplaceFileTestHook(InjectInitialPartialMove);
	const pdw::audio_profile::SettingsTransactionOutcome partialMoveWrite =
		pdw::ini::WriteMergedSettingsFile(temporary,
		generated, error);
	pdw::audio_profile::SetReplaceFileTestHook(NULL);
	const std::string partialRecovery =
		pdw::audio_profile::LastConcurrentRecoveryPathForTest();
	const std::string partialStage =
		pdw::audio_profile::LastSecondaryRecoveryPathForTest();
	Expect(partialMoveWrite == pdw::audio_profile::SETTINGS_TRANSACTION_NOT_COMMITTED &&
		g_replaceCallCount == 1,
		"general INI write fails closed on injected ReplaceFile error 1177");
	Expect(g_partialMoveSucceeded,
		"general INI 1177 regression moves the target occupant to the backup path");
	Expect(error.find("1177") != std::string::npos,
		"general INI 1177 failure reports the documented Windows error");
	Expect(ReadFile(temporary) == existing,
		"general INI 1177 recovery restores the original target bytes");
	Expect(!partialRecovery.empty() && ReadFile(partialRecovery.c_str()) == existing,
		"general INI 1177 recovery retains an explicit byte-exact recovery file");
	Expect(!partialStage.empty() &&
		ReadFile(partialStage.c_str()).find("[OutputHealth]") != std::string::npos &&
		error.find(partialStage) != std::string::npos,
		"general INI 1177 recovery retains and reports the protected replacement stage");
	DeleteFileA(partialRecovery.c_str());
	DeleteFileA(partialStage.c_str());

	WriteFileBytes(temporary, existing);
	g_atomicSwapSucceeded = false;
	g_atomicSwapError = ERROR_SUCCESS;
	pdw::audio_profile::SetBeforeReplaceTestHook(AtomicallySwapOperatorFile);
	const pdw::audio_profile::SettingsTransactionOutcome atomicSwapWrite =
		pdw::ini::WriteMergedSettingsFile(temporary,
		generated, error);
	pdw::audio_profile::SetBeforeReplaceTestHook(NULL);
	const std::string atomicRecovery =
		pdw::audio_profile::LastConcurrentRecoveryPathForTest();
	Expect(g_atomicSwapSucceeded,
		"general INI regression performs the deterministic atomic pathname swap");
	Expect(atomicSwapWrite ==
		pdw::audio_profile::SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING &&
		error.find(atomicRecovery) != std::string::npos,
		"general INI write reports a verified commit with the displaced concurrent pathname");
	Expect(ReadFile(temporary).find("[OutputHealth]") != std::string::npos,
		"general INI write leaves its verified merged settings active after the commit point");
	Expect(!atomicRecovery.empty() &&
		ReadFile(atomicRecovery.c_str()) == ATOMIC_OPERATOR_BYTES,
		"general INI write retains the swapped operator bytes in recovery");
	DeleteFileA(atomicRecovery.c_str());
#endif
	DeleteFileA(temporary);

	std::string newPath = std::string(temporary) + ".new";
	DeleteFileA(newPath.c_str());
#if defined(PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS) && defined(PDW_INI_PRESERVATION_TEST_HOOKS)
	g_missingStagePath.clear();
	g_missingTargetOccupied = false;
	g_deleteFailurePath.clear();
	pdw::ini::SetBeforeMissingIniCommitTestHook(OccupyMissingTargetBeforeCommit);
	const std::string firstRunSensitiveGenerated = generated +
		"\n[SMTP]\nPassword=first-run-secret\n";
	const bool missingRaceWrite = pdw::ini::WriteMergedSettingsFile(newPath,
		firstRunSensitiveGenerated, error);
	pdw::ini::SetBeforeMissingIniCommitTestHook(NULL);
	Expect(!missingRaceWrite && g_missingTargetOccupied,
		"first-run INI creation fails closed when the pathname becomes occupied");
	Expect(ReadFile(newPath.c_str()) ==
		"[OperatorOwned]\r\nFirstRunRace=must-survive\r\n",
		"first-run INI race preserves the operator-created pathname occupant");
	Expect(!g_missingStagePath.empty() &&
		ReadFile(g_missingStagePath.c_str()).find("Password=first-run-secret") !=
		std::string::npos,
		"a failed first-run commit retains the complete protected staged settings");
	Expect(error.find(g_missingStagePath) != std::string::npos,
		"a failed first-run commit reports the exact retained sensitive path");
	DeleteFileA(g_missingStagePath.c_str());
	SetFileAttributesA(newPath.c_str(), FILE_ATTRIBUTE_NORMAL);
	DeleteFileA(newPath.c_str());
	g_missingStageSwapSucceeded = false;
	g_missingStageSwapError = ERROR_SUCCESS;
	g_missingStagePath.clear();
	pdw::ini::SetBeforeMissingIniCommitTestHook(
		AtomicallySwapMissingStageBeforeCommit);
	const bool missingStageSwapWrite = pdw::ini::WriteMergedSettingsFile(newPath,
		generated, error);
	pdw::ini::SetBeforeMissingIniCommitTestHook(NULL);
	Expect(g_missingStageSwapSucceeded,
		"first-run staged-file identity regression performs the atomic swap");
	Expect(!missingStageSwapWrite &&
		error.find("staged-file identity") != std::string::npos,
		"first-run INI creation rejects a post-close staged-file swap");
	Expect(ReadFile(newPath.c_str()).find("[OutputHealth]") != std::string::npos,
		"first-run identity rejection leaves the current pathname occupant untouched");
	SetFileAttributesA(newPath.c_str(), FILE_ATTRIBUTE_NORMAL);
	DeleteFileA(newPath.c_str());
#endif
	Expect(pdw::ini::WriteMergedSettingsFile(newPath, generated, error),
		"missing INI file is created from generated settings");
	Expect(ReadFile(newPath.c_str()).find("[OutputHealth]") != std::string::npos,
		"new INI contains all generated sections");
	DeleteFileA(newPath.c_str());

	if (failures) return 1;
	std::cout << "INI preservation tests passed.\n";
	return 0;
}
