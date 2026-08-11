#include "local_audio_profile_core.h"

#include <windows.h>
#include <aclapi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

std::string ReadAll(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::binary);
	Require(input.good(), std::string("could not read ") + path);
	std::ostringstream bytes;
	bytes << input.rdbuf();
	return bytes.str();
}

void WriteAll(const std::string& path, const std::string& bytes)
{
	const DWORD attributes = GetFileAttributesA(path.c_str());
	HANDLE output = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
		attributes == INVALID_FILE_ATTRIBUTES ? CREATE_NEW : OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	Require(output != INVALID_HANDLE_VALUE, std::string("could not create ") + path);
	LARGE_INTEGER beginning = {};
	Require(SetFilePointerEx(output, beginning, NULL, FILE_BEGIN) != FALSE &&
		SetEndOfFile(output) != FALSE, std::string("could not truncate ") + path);
	std::size_t total = 0;
	while (total < bytes.size())
	{
		DWORD written = 0;
		const DWORD requested = static_cast<DWORD>((std::min)(bytes.size() - total,
			static_cast<std::size_t>(0x7fffffff)));
		Require(WriteFile(output, bytes.data() + total, requested, &written, NULL) != FALSE &&
			written != 0, std::string("could not write ") + path);
		total += written;
	}
	Require(FlushFileBuffers(output) != FALSE, std::string("could not flush ") + path);
	CloseHandle(output);
}

std::vector<unsigned char> ReadDaclSecurityDescriptor(const std::string& path)
{
	DWORD required = 0;
	SetLastError(ERROR_SUCCESS);
	GetFileSecurityA(path.c_str(), DACL_SECURITY_INFORMATION, NULL, 0, &required);
	Require(required != 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
		"could not size the test file DACL");
	std::vector<unsigned char> descriptor(required);
	Require(GetFileSecurityA(path.c_str(), DACL_SECURITY_INFORMATION,
		reinterpret_cast<PSECURITY_DESCRIPTOR>(&descriptor[0]), required, &required) != FALSE,
		"could not read the test file DACL");
	return descriptor;
}

bool EquivalentDaclSecurity(const std::vector<unsigned char>& left,
	const std::vector<unsigned char>& right)
{
	if (left.empty() || right.empty()) return false;
	PSECURITY_DESCRIPTOR leftDescriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(
		const_cast<unsigned char*>(&left[0]));
	PSECURITY_DESCRIPTOR rightDescriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(
		const_cast<unsigned char*>(&right[0]));
	BOOL leftPresent = FALSE;
	BOOL rightPresent = FALSE;
	BOOL leftDefaulted = FALSE;
	BOOL rightDefaulted = FALSE;
	PACL leftAcl = NULL;
	PACL rightAcl = NULL;
	WORD leftControl = 0;
	WORD rightControl = 0;
	DWORD revision = 0;
	if (!GetSecurityDescriptorDacl(leftDescriptor, &leftPresent, &leftAcl, &leftDefaulted) ||
		!GetSecurityDescriptorDacl(rightDescriptor, &rightPresent, &rightAcl, &rightDefaulted) ||
		!GetSecurityDescriptorControl(leftDescriptor, &leftControl, &revision) ||
		!GetSecurityDescriptorControl(rightDescriptor, &rightControl, &revision) ||
		leftPresent != rightPresent ||
		((leftControl ^ rightControl) & SE_DACL_PROTECTED) != 0)
		return false;
	if (!leftPresent) return true;
	return leftAcl && rightAcl && leftAcl->AclSize == rightAcl->AclSize &&
		memcmp(leftAcl, rightAcl, leftAcl->AclSize) == 0;
}

void SetRestrictiveCurrentUserDacl(const std::string& path)
{
	HANDLE token = NULL;
	Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE,
		"could not open the current process token");
	DWORD required = 0;
	GetTokenInformation(token, TokenUser, NULL, 0, &required);
	Require(required != 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
		"could not size the current user token");
	std::vector<unsigned char> tokenBytes(required);
	Require(GetTokenInformation(token, TokenUser, &tokenBytes[0], required, &required) != FALSE,
		"could not read the current user token");
	CloseHandle(token);

	const TOKEN_USER* tokenUser = reinterpret_cast<const TOKEN_USER*>(&tokenBytes[0]);
	EXPLICIT_ACCESSA access = {};
	access.grfAccessPermissions = FILE_ALL_ACCESS;
	access.grfAccessMode = SET_ACCESS;
	access.grfInheritance = NO_INHERITANCE;
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.TrusteeType = TRUSTEE_IS_USER;
	access.Trustee.ptstrName = static_cast<LPSTR>(tokenUser->User.Sid);
	PACL acl = NULL;
	Require(SetEntriesInAclA(1, &access, NULL, &acl) == ERROR_SUCCESS,
		"could not build the restrictive test DACL");
	const DWORD result = SetNamedSecurityInfoA(const_cast<LPSTR>(path.c_str()), SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
		NULL, NULL, acl, NULL);
	LocalFree(acl);
	Require(result == ERROR_SUCCESS, "could not apply the restrictive test DACL");
}

void RestoreDaclSecurityDescriptor(const std::string& path,
	const std::vector<unsigned char>& descriptor)
{
	Require(!descriptor.empty() && SetFileSecurityA(path.c_str(), DACL_SECURITY_INFORMATION,
		reinterpret_cast<PSECURITY_DESCRIPTOR>(
			const_cast<unsigned char*>(&descriptor[0]))) != FALSE,
		"could not restore the original test DACL");
}

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
const char CONCURRENT_BEFORE_REPLACE_BYTES[] =
	"[OperatorOwned]\r\nBeforeReplace=must-survive\r\n";
const char ATOMIC_PATH_SWAP_BYTES[] =
	"[OperatorOwned]\r\nAtomicPathSwap=must-survive\r\n";
bool g_beforeReplaceWriterOpened = false;
bool g_beforeReplaceWriteCompleted = false;
DWORD g_beforeReplaceWriterError = ERROR_SUCCESS;
bool g_secureTempSwapSucceeded = false;
DWORD g_secureTempSwapError = ERROR_SUCCESS;
bool g_verifiedDeleteSwapSucceeded = false;
DWORD g_verifiedDeleteSwapError = ERROR_SUCCESS;
unsigned int g_verifiedDeleteSwapAttempts = 0;
bool g_stagedCommitSwapSucceeded = false;
DWORD g_stagedCommitSwapError = ERROR_SUCCESS;
std::string g_stagedCommitSwapBytes;
bool g_guardedDaclSwapSucceeded = false;
DWORD g_guardedDaclSwapError = ERROR_SUCCESS;
std::string g_guardedDaclSwapBytes;
std::vector<unsigned char> g_broadDacl;
std::vector<unsigned char> g_observedSecureTempDacl;
bool g_atomicPathSwapSucceeded = false;
DWORD g_atomicPathSwapError = ERROR_SUCCESS;
unsigned int g_replaceFileCallCount = 0;
unsigned int g_injectPartialMoveOnCall = 0;
bool g_partialMoveSucceeded = false;
DWORD g_partialMoveError = ERROR_SUCCESS;
std::string g_forcedReadFailurePath;
bool g_forcedReadFailureObserved = false;
unsigned int g_deleteFileCallCount = 0;
unsigned int g_failDeleteOnCall = 0;
std::string g_failedDeletePath;
bool g_backupVerificationReadFailed = false;
bool g_concurrentDeleteSucceeded = false;
DWORD g_concurrentDeleteError = ERROR_SUCCESS;
std::string g_identitySwapBytes;
std::vector<unsigned char> g_identitySwapDacl;
bool g_identitySwapSucceeded = false;
DWORD g_identitySwapError = ERROR_SUCCESS;

void AttemptBeforeReplaceOperatorEdit(const char* path)
{
	HANDLE file = CreateFileA(path, GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		g_beforeReplaceWriterError = GetLastError();
		return;
	}
	g_beforeReplaceWriterOpened = true;
	LARGE_INTEGER beginning = {};
	DWORD written = 0;
	const DWORD expected = static_cast<DWORD>(sizeof(CONCURRENT_BEFORE_REPLACE_BYTES) - 1);
	g_beforeReplaceWriteCompleted =
		SetFilePointerEx(file, beginning, NULL, FILE_BEGIN) != FALSE &&
		WriteFile(file, CONCURRENT_BEFORE_REPLACE_BYTES, expected, &written, NULL) != FALSE &&
		written == expected && SetEndOfFile(file) != FALSE && FlushFileBuffers(file) != FALSE;
	if (!g_beforeReplaceWriteCompleted) g_beforeReplaceWriterError = GetLastError();
	CloseHandle(file);
}

void AttemptSecureTempSwapBeforeWrite(const char* path)
{
	const std::string replacementPath = std::string(path) + ".permissive-swap";
	DeleteFileA(replacementPath.c_str());
	WriteAll(replacementPath, "attacker-controlled\r\n");
	g_secureTempSwapSucceeded = ReplaceFileA(path, replacementPath.c_str(), NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
	if (!g_secureTempSwapSucceeded)
	{
		g_secureTempSwapError = GetLastError();
		DeleteFileA(replacementPath.c_str());
	}
}

void AttemptSwapBeforeVerifiedDelete(const char* path)
{
	++g_verifiedDeleteSwapAttempts;
	const std::string replacementPath = std::string(path) + ".cleanup-swap";
	DeleteFileA(replacementPath.c_str());
	WriteAll(replacementPath, "[OperatorOwned]\r\nCleanupSwap=must-survive\r\n");
	g_verifiedDeleteSwapSucceeded = ReplaceFileA(path, replacementPath.c_str(), NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
	if (!g_verifiedDeleteSwapSucceeded)
	{
		g_verifiedDeleteSwapError = GetLastError();
		DeleteFileA(replacementPath.c_str());
	}
}

void AtomicallySwapStagedFileBeforeCommit(const char* path)
{
	const std::string replacementPath = std::string(path) + ".staged-identity-swap";
	DeleteFileA(replacementPath.c_str());
	WriteAll(replacementPath, g_stagedCommitSwapBytes);
	g_stagedCommitSwapSucceeded = ReplaceFileA(path, replacementPath.c_str(), NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
	if (!g_stagedCommitSwapSucceeded)
	{
		g_stagedCommitSwapError = GetLastError();
		DeleteFileA(replacementPath.c_str());
	}
}

void AtomicallySwapTargetDaclAfterGuard(const char* path)
{
	const std::string replacementPath = std::string(path) + ".guarded-dacl-swap";
	DeleteFileA(replacementPath.c_str());
	WriteAll(replacementPath, g_guardedDaclSwapBytes);
	g_guardedDaclSwapSucceeded = ReplaceFileA(path, replacementPath.c_str(), NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
	if (g_guardedDaclSwapSucceeded)
	{
		Require(!g_broadDacl.empty() && SetFileSecurityA(path,
			DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
			reinterpret_cast<PSECURITY_DESCRIPTOR>(&g_broadDacl[0])) != FALSE,
			"could not apply the broad swapped target DACL");
	}
	else
	{
		g_guardedDaclSwapError = GetLastError();
		DeleteFileA(replacementPath.c_str());
	}
}

void CaptureSecureTemporaryDacl(const char* path)
{
	g_observedSecureTempDacl = ReadDaclSecurityDescriptor(path);
}

void AtomicallySwapOperatorEdit(const char* path)
{
	const std::string replacementPath = std::string(path) + ".operator-swap";
	DeleteFileA(replacementPath.c_str());
	WriteAll(replacementPath, ATOMIC_PATH_SWAP_BYTES);
	g_atomicPathSwapSucceeded = ReplaceFileA(path, replacementPath.c_str(), NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
	if (!g_atomicPathSwapSucceeded)
	{
		g_atomicPathSwapError = GetLastError();
		DeleteFileA(replacementPath.c_str());
	}
}

bool InjectPartialMoveReplaceFailure(const char* replacedPath,
	const char* replacementPath, const char* backupPath, unsigned long flags)
{
	++g_replaceFileCallCount;
	if (g_replaceFileCallCount != g_injectPartialMoveOnCall)
		return ReplaceFileA(replacedPath, replacementPath, backupPath,
			static_cast<DWORD>(flags), NULL, NULL) != FALSE;

	// Reproduce the documented ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 state:
	// the target occupant is at the requested backup path, the target pathname
	// is absent, and the staged replacement remains at its original pathname.
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

bool CaptureFirstDisplacedPathAndReplace(const char* replacedPath,
	const char* replacementPath, const char* backupPath, unsigned long flags)
{
	const bool replaced = ReplaceFileA(replacedPath, replacementPath, backupPath,
		static_cast<DWORD>(flags), NULL, NULL) != FALSE;
	if (replaced && g_forcedReadFailurePath.empty())
		g_forcedReadFailurePath = backupPath;
	return replaced;
}

bool FailCapturedDisplacedRead(const char* path)
{
	if (!g_forcedReadFailureObserved && !g_forcedReadFailurePath.empty() &&
		g_forcedReadFailurePath == path)
	{
		g_forcedReadFailureObserved = true;
		return true;
	}
	return false;
}

bool FailSelectedDelete(const char* path)
{
	++g_deleteFileCallCount;
	if (g_deleteFileCallCount != g_failDeleteOnCall) return false;
	g_failedDeletePath = path;
	return true;
}

bool FailBackupVerificationRead(const char* path)
{
	if (!g_backupVerificationReadFailed && path &&
		std::string(path).find(".pre-adelaide-flex-") != std::string::npos)
	{
		g_backupVerificationReadFailed = true;
		return true;
	}
	return false;
}

void DeleteTargetBeforeReplace(const char* path)
{
	g_concurrentDeleteSucceeded = DeleteFileA(path) != FALSE;
	if (!g_concurrentDeleteSucceeded) g_concurrentDeleteError = GetLastError();
}

void AtomicallySwapIdenticalMetadataFile(const char* path)
{
	const std::string replacementPath = std::string(path) + ".operator-identical-swap";
	DeleteFileA(replacementPath.c_str());
	WriteAll(replacementPath, g_identitySwapBytes);
	g_identitySwapSucceeded = ReplaceFileA(path, replacementPath.c_str(), NULL,
		REPLACEFILE_WRITE_THROUGH, NULL, NULL) != FALSE;
	if (g_identitySwapSucceeded)
	{
		SetRestrictiveCurrentUserDacl(path);
		Require(SetFileAttributesA(path,
			FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE) != FALSE,
			"could not apply identical-swap file attributes");
		WriteAll(std::string(path) + ":operator-metadata",
			"alternate-stream-must-survive");
		g_identitySwapDacl = ReadDaclSecurityDescriptor(path);
	}
	if (!g_identitySwapSucceeded)
	{
		g_identitySwapError = GetLastError();
		DeleteFileA(replacementPath.c_str());
	}
}

#endif

std::string MakeTemporaryPath()
{
	char directory[MAX_PATH] = {};
	Require(GetTempPathA(MAX_PATH, directory) > 0, "GetTempPath failed");
	char path[MAX_PATH] = {};
	Require(GetTempFileNameA(directory, "PAP", 0, path) != 0, "GetTempFileName failed");
	return path;
}

std::string LowerAscii(const std::string& value)
{
	std::string result(value);
	for (std::string::iterator character = result.begin(); character != result.end(); ++character)
		if (*character >= 'A' && *character <= 'Z') *character = static_cast<char>(*character - 'A' + 'a');
	return result;
}

std::string Trim(const std::string& value)
{
	const std::string whitespace = " \t\r";
	const std::size_t first = value.find_first_not_of(whitespace);
	if (first == std::string::npos) return std::string();
	const std::size_t last = value.find_last_not_of(whitespace);
	return value.substr(first, last - first + 1);
}

std::string IniValue(const std::string& contents, const std::string& wantedSection,
	const std::string& wantedKey)
{
	std::istringstream input(contents);
	std::string line;
	std::string section;
	std::string result;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (line.size() >= 3 && line[0] == '[')
		{
			const std::size_t close = line.find(']');
			if (close != std::string::npos) section = LowerAscii(Trim(line.substr(1, close - 1)));
			continue;
		}
		if (section != LowerAscii(wantedSection) || line.empty() || line[0] == ';' || line[0] == '#')
			continue;
		const std::size_t equals = line.find('=');
		if (equals == std::string::npos) continue;
		if (LowerAscii(Trim(line.substr(0, equals))) == LowerAscii(wantedKey))
			result = Trim(line.substr(equals + 1));
	}
	return result;
}

pdw::audio_profile::CaptureDevice Device(int index, const std::string& endpointId,
	const std::string& name)
{
	pdw::audio_profile::CaptureDevice device;
	device.winmmIndex = index;
	device.endpointId = endpointId;
	device.friendlyName = name;
	return device;
}

pdw::audio_profile::DeviceResolution ResolvedDevice(const std::string& endpointId,
	const std::string& name, int index = 7)
{
	pdw::audio_profile::DeviceResolution resolution;
	resolution.found = true;
	resolution.winmmIndex = index;
	resolution.endpointId = endpointId;
	resolution.friendlyName = name;
	return resolution;
}

std::string OriginalIni()
{
	return
		"; operator comment retained byte-for-byte\r\n"
		"[PDW]\r\n"
		"AudioEnabled=0\r\n"
		"AudioSource=2\r\n"
		"UnknownPdwSetting=operator-owned\r\n"
		"Threshold1600=9\r\n"
		"\r\n"
		"[InputProfile]\r\n"
		"PresetId=old-unsafe-raw-value\r\n"
		"PresetName=old name\r\n"
		"DeviceEndpointId=old endpoint\r\n"
		"DeviceFriendlyName=old device\r\n"
		"OperatorInputNote=unchanged\r\n"
		"\r\n"
		"# another operator comment\r\n"
		"[OperatorOwned]\r\n"
		"KeepThis=unchanged\r\n";
}

void RemoveFile(const std::string& path)
{
	if (!path.empty())
	{
		SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
		DeleteFileA(path.c_str());
	}
}

void TestPresetValues()
{
	using namespace pdw::audio_profile;
	const DecoderProfileValues values = AdelaideFlexPreset();
	Require(values.audioEnabled == 1, "preset must enable local audio");
	Require(values.audioSource == 0 && values.audioDevice == 0,
		"preset source and compatibility ordinal changed");
	Require(values.audioSampleRate == 44100 && values.audioConfiguration == 0,
		"preset audio format changed");
	Require(values.comPortEnabled == 0, "preset must disable serial capture");
	Require(values.decodePocsag == 1 && values.decodeFlex == 1 && values.monitorPaging == 1,
		"preset decoder enablement changed");
	Require(values.pocsagShowBoth == 1 && values.showCfs == 1,
		"preset display values changed");
	Require(values.flex1600 == 1 && values.flex3200 == 0 && values.flex6400 == 0,
		"preset FLEX rates changed");
	Require(values.bitSync == 13107 && values.minimumMessageLength == 15,
		"preset FLEX synchronization changed");
	Require(values.invertData == 1 && values.panePercent == 69 && values.threshold1600 == 2,
		"preset slicer values changed");
	Require(values.auxiliaryCustomValuesAreDefault == 1,
		"preset auxiliary slicers must remain deterministic defaults");
	Require(IsAdelaideFlexProfile(values), "exact preset should identify itself");

	DecoderProfileValues reordered = values;
	reordered.audioDevice = 12;
	Require(IsAdelaideFlexProfile(reordered),
		"a reordered compatibility ordinal must not invalidate the stable preset");
	reordered.flex3200 = 1;
	Require(!IsAdelaideFlexProfile(reordered), "a changed decoder value must invalidate the preset");
}

void TestUtf8ValidationAndIniEncoding()
{
	using namespace pdw::audio_profile;
	std::string error;
	std::string encoded;
	std::string decoded;
	std::string unicode = "VB-Cable ";
	unicode += "\xE3\x82\xB1\xE3\x83\xBC\xE3\x83\x96\xE3\x83\xAB ";
	unicode += "\xF0\x9F\x8E\xA7";
	Require(ValidateUtf8Field(unicode, unicode.size(), error), "valid UTF-8 should pass");
	Require(EncodeIniUtf8Field(unicode, unicode.size(), encoded, error),
		std::string("valid UTF-8 should encode: ") + error);
	Require(encoded.find("utf8-hex:") == 0 && encoded.find(unicode) == std::string::npos,
		"encoded fields must be ASCII-safe utf8-hex");
	for (std::size_t index = 0; index < encoded.size(); ++index)
		Require(static_cast<unsigned char>(encoded[index]) >= 0x20 &&
			static_cast<unsigned char>(encoded[index]) <= 0x7e,
			"encoded field contained a non-ASCII byte");
	Require(DecodeIniUtf8Field(encoded, unicode.size(), decoded, error) && decoded == unicode,
		"utf8-hex must round-trip exactly");

	const std::string boundary(MAX_PROFILE_ID_UTF8_BYTES, 'x');
	Require(ValidateUtf8Field(boundary, MAX_PROFILE_ID_UTF8_BYTES, error),
		"maximum-length field should pass");
	Require(!ValidateUtf8Field(boundary + "x", MAX_PROFILE_ID_UTF8_BYTES, error),
		"overlength field should fail");
	Require(EncodeIniUtf8Field(std::string(), 0, encoded, error) && encoded == "utf8-hex:",
		"empty fields should have a reversible representation");
	Require(DecodeIniUtf8Field(encoded, 0, decoded, error) && decoded.empty(),
		"empty utf8-hex field should decode");

	const std::string controls[] = {
		std::string("A\0B", 3), "\r", "\n", std::string(1, '\x01'), std::string(1, '\x1f'),
		std::string(1, '\x7f')
	};
	for (std::size_t index = 0; index < sizeof(controls) / sizeof(controls[0]); ++index)
		Require(!ValidateUtf8Field(controls[index], 32, error),
			"C0/DEL/CR/LF controls must be rejected");

	const std::string invalidUtf8[] = {
		std::string("\x80", 1), std::string("\xC0\x80", 2),
		std::string("\xE2\x82", 2), std::string("\xED\xA0\x80", 3),
		std::string("\xF4\x90\x80\x80", 4)
	};
	for (std::size_t index = 0; index < sizeof(invalidUtf8) / sizeof(invalidUtf8[0]); ++index)
		Require(!ValidateUtf8Field(invalidUtf8[index], 32, error),
			"invalid/non-canonical UTF-8 must be rejected");

	Require(!DecodeIniUtf8Field("plain text", 32, decoded, error),
		"unencoded INI text must be rejected");
	Require(!DecodeIniUtf8Field("utf8-hex:4", 32, decoded, error),
		"odd-length hex must be rejected");
	Require(!DecodeIniUtf8Field("utf8-hex:GG", 32, decoded, error),
		"non-hex bytes must be rejected");
	Require(!DecodeIniUtf8Field("utf8-hex:0A", 32, decoded, error),
		"encoded controls must still be rejected");
	Require(!DecodeIniUtf8Field("utf8-hex:4142", 1, decoded, error),
		"decoded byte limits must be enforced");
}

void TestDeviceResolution()
{
	using namespace pdw::audio_profile;
	std::vector<CaptureDevice> devices;
	devices.push_back(Device(0, "endpoint-mic", "Microphone Array"));
	devices.push_back(Device(7, "Endpoint-Cable", VB_CABLE_CAPTURE_NAME));

	DeviceResolution result = ResolveCaptureDevice(devices, "Endpoint-Cable",
		VB_CABLE_CAPTURE_NAME, 0, false);
	Require(result.found && result.winmmIndex == 7 && !result.usedCompatibilityIndex,
		"exact endpoint identity must survive ordinal reorder");
	result = ResolveCaptureDevice(devices, "endpoint-cable", VB_CABLE_CAPTURE_NAME, 0, true);
	Require(!result.found,
		"endpoint IDs are opaque and case-sensitive, with no name/ordinal redirect");
	result = ResolveCaptureDevice(devices, "missing-endpoint", VB_CABLE_CAPTURE_NAME, 0, true);
	Require(!result.found, "a missing stable endpoint must fail closed");

	result = ResolveCaptureDevice(devices, std::string(),
		"CABLE Output (VB Audio Virtual Cable)", 0, false);
	Require(result.found && result.endpointId == "Endpoint-Cable",
		"a unique normalized friendly name may bootstrap stable identity");

	std::vector<CaptureDevice> normalizedCollision = devices;
	normalizedCollision.push_back(Device(9, "Endpoint-Cable-2",
		"cable-output vb audio virtual cable"));
	result = ResolveCaptureDevice(normalizedCollision, std::string(),
		VB_CABLE_CAPTURE_NAME, 0, false);
	Require(!result.found, "normalization collisions must be ambiguous");

	std::vector<CaptureDevice> duplicateRepresentation = devices;
	duplicateRepresentation.push_back(Device(-1, "Endpoint-Cable", VB_CABLE_CAPTURE_NAME));
	result = ResolveCaptureDevice(duplicateRepresentation, std::string(),
		VB_CABLE_CAPTURE_NAME, 0, false);
	Require(result.found && result.winmmIndex == 7,
		"duplicate records for one exact endpoint are one identity");

	const std::string fullName = VB_CABLE_CAPTURE_NAME;
	const std::string truncated = fullName.substr(0, 31);
	Require(truncated.size() == 31 && FriendlyDeviceNamesMatch(truncated, fullName),
		"a complete WinMM payload may match its full endpoint name");
	std::vector<CaptureDevice> truncatedDevice;
	truncatedDevice.push_back(Device(3, "Endpoint-Truncated", truncated));
	result = ResolveCaptureDevice(truncatedDevice, std::string(), fullName, 0, false);
	Require(result.found && result.winmmIndex == 3,
		"a unique safe WinMM truncation may bootstrap identity");

	std::vector<CaptureDevice> truncationCollision;
	truncationCollision.push_back(Device(3, "Endpoint-A", fullName));
	truncationCollision.push_back(Device(4, "Endpoint-B", truncated + "Different suffix"));
	result = ResolveCaptureDevice(truncationCollision, std::string(), truncated, 0, false);
	Require(!result.found, "truncated-name collisions must require explicit selection");
	Require(!FriendlyDeviceNamesMatch("short common prefix", "short common prefix two"),
		"ordinary short prefixes must never match");

	std::vector<CaptureDevice> identityless;
	identityless.push_back(Device(0, std::string(), truncated));
	result = ResolveCaptureDevice(identityless, std::string(), fullName, 0, false);
	Require(!result.found, "friendly bootstrap requires a stable endpoint ID");

	std::vector<CaptureDevice> unsafe = devices;
	unsafe.push_back(Device(8, "bad\nendpoint", VB_CABLE_CAPTURE_NAME));
	result = ResolveCaptureDevice(unsafe, std::string(), VB_CABLE_CAPTURE_NAME, 0, false);
	Require(result.found && result.endpointId == "Endpoint-Cable",
		"unsafe device strings must not create a match or ambiguity");

	result = ResolveCaptureDevice(devices, std::string(), std::string(), 0, false);
	Require(!result.found, "legacy ordinal fallback must be opt-in");
	result = ResolveCaptureDevice(devices, std::string(), std::string(), 0, true);
	Require(result.found && result.winmmIndex == 0 && result.usedCompatibilityIndex,
		"explicitly allowed legacy ordinal fallback must remain compatible");
	std::vector<CaptureDevice> ambiguousOrdinal;
	ambiguousOrdinal.push_back(Device(2, "Endpoint-A", "Input A"));
	ambiguousOrdinal.push_back(Device(2, "Endpoint-B", "Input B"));
	result = ResolveCaptureDevice(ambiguousOrdinal, std::string(), std::string(), 2, true);
	Require(!result.found, "ambiguous legacy ordinals must fail closed");
}

void TestSaveDecision()
{
	using namespace pdw::audio_profile;
	Require(DecideCaptureDeviceSave(false, true, false) ==
		CAPTURE_SAVE_REJECT_UNAVAILABLE_STABLE_IDENTITY,
		"unchanged selection with a missing stable endpoint must reject save");
	Require(DecideCaptureDeviceSave(true, true, false) ==
		CAPTURE_SAVE_BIND_EXPLICIT_SELECTION,
		"an explicit selection may deliberately rebind a missing endpoint");
	Require(DecideCaptureDeviceSave(false, true, true) ==
		CAPTURE_SAVE_KEEP_CONFIGURED_IDENTITY,
		"a resolved unchanged stable endpoint should be retained");
	Require(DecideCaptureDeviceSave(false, false, false) ==
		CAPTURE_SAVE_KEEP_CONFIGURED_IDENTITY,
		"unchanged legacy ordinal settings must not invent a stable identity");
}

void TestExplicitApplyBackupAndRestore()
{
	using namespace pdw::audio_profile;
	const std::string path = MakeTemporaryPath();
	const std::string original = OriginalIni();
	WriteAll(path, original);

	std::string endpointId = "{0.0.1.00000000}.VB-Cable-";
	endpointId += "\xF0\x9F\x8E\xA7";
	std::string friendlyName = "CABLE Output - ";
	friendlyName += "\xE3\x82\xB1\xE3\x83\xBC\xE3\x83\x96\xE3\x83\xAB";
	const DeviceResolution resolved = ResolvedDevice(endpointId, friendlyName);
	std::string backup;
	std::string error;
	Require(ApplyAdelaideFlexPresetToIni(path, resolved, backup, error),
		std::string("explicit preset apply failed: ") + error);
	Require(backup.find(".pre-adelaide-flex-") != std::string::npos,
		"backup path should identify the explicit Adelaide preset transaction");
	Require(ReadAll(backup) == original, "backup must be byte-exact");

	const std::string applied = ReadAll(path);
	Require(applied.find("; operator comment retained byte-for-byte\r\n") != std::string::npos &&
		applied.find("# another operator comment\r\n") != std::string::npos,
		"operator comments must be preserved");
	Require(applied.find("UnknownPdwSetting=operator-owned\r\n") != std::string::npos &&
		applied.find("OperatorInputNote=unchanged\r\n") != std::string::npos &&
		applied.find("[OperatorOwned]\r\nKeepThis=unchanged\r\n") != std::string::npos,
		"unknown keys and sections must be preserved");

	const char* expected[][3] = {
		{ "PDW", "AudioEnabled", "1" }, { "PDW", "AudioSource", "0" },
		{ "PDW", "AudioDevice", "0" }, { "PDW", "AudioSampleRate", "44100" },
		{ "PDW", "AudioConfiguration", "0" }, { "PDW", "ComPortEnabled", "0" },
		{ "PDW", "DecodePocsag", "1" }, { "PDW", "DecodeFlex", "1" },
		{ "PDW", "PocsagFlex", "1" }, { "PDW", "PocsagShowBoth", "1" },
		{ "PDW", "ShowCFS", "1" }, { "PDW", "Flex1600", "1" },
		{ "PDW", "Flex3200", "0" }, { "PDW", "Flex6400", "0" },
		{ "PDW", "BTSYNC", "13107" }, { "PDW", "MINMSG", "15" },
		{ "PDW", "InvertData", "1" }, { "PDW", "Percent", "69" },
		{ "PDW", "Threshold1600", "2" }, { "PDW", "Threshold512", "0" },
		{ "PDW", "Threshold1200", "0" }, { "PDW", "Threshold2400", "0" },
		{ "PDW", "Resync512", "0" }, { "PDW", "Resync1200", "0" },
		{ "PDW", "Resync1600", "0" }, { "PDW", "Resync2400", "0" },
		{ "PDW", "Centering512", "0" }, { "PDW", "Centering1200", "0" },
		{ "PDW", "Centering1600", "0" }, { "PDW", "Centering2400", "0" }
	};
	for (std::size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
		Require(IniValue(applied, expected[index][0], expected[index][1]) == expected[index][2],
			std::string("explicit preset value mismatch: ") + expected[index][1]);

	struct EncodedExpectation
	{
		const char* key;
		std::string expected;
		std::size_t maximum;
	};
	const EncodedExpectation encodedExpected[] = {
		{ "PresetId", ADELAIDE_FLEX_PRESET_ID, MAX_PROFILE_ID_UTF8_BYTES },
		{ "PresetName", ADELAIDE_FLEX_PRESET_NAME, MAX_PROFILE_NAME_UTF8_BYTES },
		{ "DeviceEndpointId", endpointId, MAX_ENDPOINT_ID_UTF8_BYTES },
		{ "DeviceFriendlyName", friendlyName, MAX_ENDPOINT_NAME_UTF8_BYTES }
	};
	for (std::size_t index = 0; index < sizeof(encodedExpected) / sizeof(encodedExpected[0]); ++index)
	{
		const std::string stored = IniValue(applied, "InputProfile", encodedExpected[index].key);
		std::string decoded;
		Require(DecodeIniUtf8Field(stored, encodedExpected[index].maximum, decoded, error) &&
			decoded == encodedExpected[index].expected,
			std::string("encoded profile field did not round-trip: ") + encodedExpected[index].key);
	}
	Require(applied.find(endpointId) == std::string::npos &&
		applied.find(friendlyName) == std::string::npos,
		"non-ASCII endpoint fields must never be persisted raw");
	Require(IniValue(applied, "InputProfile", "FirstRunPrompt").empty() &&
		IniValue(applied, "InputProfile", "MigrationOffered").empty(),
		"explicit apply must not create automatic first-run or migration state");
	Require(IniValue(applied, "InputProfile", "IdentityInvalid") == "0",
		"explicit apply must clear the durable invalid-identity quarantine marker");

	Require(RestoreIniFromVerifiedBackup(path, backup, error),
		std::string("verified restore failed: ") + error);
	Require(ReadAll(path) == original, "restore must be byte-for-byte exact");
	Require(ReadAll(backup) == original, "restore must leave the verified backup intact");
	RemoveFile(path);
	RemoveFile(backup);
}

void TestTransactionalFailures()
{
	using namespace pdw::audio_profile;
	const std::string path = MakeTemporaryPath();
	const std::string original = OriginalIni();
	WriteAll(path, original);
	std::string backup;
	std::string error;

	DeviceResolution unresolved;
	Require(!ApplyAdelaideFlexPresetToIni(path, unresolved, backup, error) && backup.empty(),
		"unresolved preset apply must fail before backup");
	Require(ReadAll(path) == original, "unresolved apply changed the target");

	DeviceResolution legacy = ResolvedDevice("Endpoint", "Device");
	legacy.usedCompatibilityIndex = true;
	Require(!ApplyAdelaideFlexPresetToIni(path, legacy, backup, error) && backup.empty(),
		"legacy ordinal resolution must not qualify as explicit stable preset input");

	DeviceResolution invalid = ResolvedDevice("Endpoint\rInjected=1", "Device");
	Require(!ApplyAdelaideFlexPresetToIni(path, invalid, backup, error) && backup.empty(),
		"control characters must fail before backup");
	invalid = ResolvedDevice(std::string("\xC0\x80", 2), "Device");
	Require(!ApplyAdelaideFlexPresetToIni(path, invalid, backup, error) && backup.empty(),
		"invalid UTF-8 must fail before backup");
	invalid = ResolvedDevice(std::string(MAX_ENDPOINT_ID_UTF8_BYTES + 1, 'x'), "Device");
	Require(!ApplyAdelaideFlexPresetToIni(path, invalid, backup, error) && backup.empty(),
		"overlength endpoint IDs must fail before backup");
	invalid = ResolvedDevice("Endpoint", std::string());
	Require(!ApplyAdelaideFlexPresetToIni(path, invalid, backup, error) && backup.empty(),
		"empty supplied friendly names must fail before backup");
	Require(ReadAll(path) == original, "validation failure changed the target");

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
	g_secureTempSwapSucceeded = false;
	g_secureTempSwapError = ERROR_SUCCESS;
	SetBeforeSecureTempWriteTestHook(AttemptSecureTempSwapBeforeWrite);
	const bool protectedStageCommit = CommitVerifiedFileTransaction(path, original,
		"[PDW]\r\nProtectedStage=1\r\n", error);
	SetBeforeSecureTempWriteTestHook(NULL);
	Require(protectedStageCommit,
		std::string("protected staged-file transaction failed: ") + error);
	Require(!g_secureTempSwapSucceeded &&
		g_secureTempSwapError == ERROR_SHARING_VIOLATION,
		"the protected staged file must stay exclusively pinned through its write");
	Require(ReadAll(path) == "[PDW]\r\nProtectedStage=1\r\n",
		"the protected staged-file transaction wrote unexpected bytes");
	WriteAll(path, original);
	g_verifiedDeleteSwapSucceeded = false;
	g_verifiedDeleteSwapError = ERROR_SUCCESS;
	g_verifiedDeleteSwapAttempts = 0;
	SetBeforeVerifiedDeleteTestHook(AttemptSwapBeforeVerifiedDelete);
	const bool pinnedCleanupCommit = CommitVerifiedFileTransaction(path, original,
		"[PDW]\r\nPinnedCleanup=1\r\n", error);
	SetBeforeVerifiedDeleteTestHook(NULL);
	Require(pinnedCleanupCommit,
		std::string("pinned displaced cleanup transaction failed: ") + error);
	Require(g_verifiedDeleteSwapAttempts >= 2 && !g_verifiedDeleteSwapSucceeded &&
		g_verifiedDeleteSwapError == ERROR_SHARING_VIOLATION,
		"backup-vacancy and displaced cleanup must pin each exact pathname occupant through deletion");
	WriteAll(path, original);
	g_stagedCommitSwapBytes = "[PDW]\r\nStagedIdentity=1\r\n";
	g_stagedCommitSwapSucceeded = false;
	g_stagedCommitSwapError = ERROR_SUCCESS;
	SetBeforeStagedCommitTestHook(AtomicallySwapStagedFileBeforeCommit);
	const bool stagedIdentityCommit = CommitVerifiedFileTransaction(path, original,
		g_stagedCommitSwapBytes, error);
	SetBeforeStagedCommitTestHook(NULL);
	const std::string stagedIdentityRecovery = LastConcurrentRecoveryPathForTest();
	Require(g_stagedCommitSwapSucceeded,
		std::string("staged identity swap failed with error ") +
		std::to_string(g_stagedCommitSwapError));
	Require(!stagedIdentityCommit && ReadAll(path) == g_stagedCommitSwapBytes,
		"a post-close staged-file swap must be rejected without a second pathname overwrite");
	Require(!stagedIdentityRecovery.empty() &&
		ReadAll(stagedIdentityRecovery) == original,
		"an unverified staged commit must retain the displaced original for recovery");
	Require(error.find("did not perform any post-commit replacement or rollback") !=
		std::string::npos,
		"an unverified staged commit must report the bounded no-rollback outcome");
	RemoveFile(stagedIdentityRecovery);
	WriteAll(path, original);
	g_broadDacl = ReadDaclSecurityDescriptor(path);
	SetRestrictiveCurrentUserDacl(path);
	const std::vector<unsigned char> guardedRestrictiveDacl =
		ReadDaclSecurityDescriptor(path);
	g_guardedDaclSwapBytes = original;
	g_guardedDaclSwapSucceeded = false;
	g_guardedDaclSwapError = ERROR_SUCCESS;
	g_observedSecureTempDacl.clear();
	SetAfterTargetGuardTestHook(AtomicallySwapTargetDaclAfterGuard);
	SetBeforeSecureTempWriteTestHook(CaptureSecureTemporaryDacl);
	const SettingsTransactionOutcome guardedDaclCommit =
		CommitVerifiedFileTransaction(path, original,
		"[PDW]\r\nGuardedDacl=1\r\n", error);
	SetBeforeSecureTempWriteTestHook(NULL);
	SetAfterTargetGuardTestHook(NULL);
	const std::string guardedDaclRecovery = LastConcurrentRecoveryPathForTest();
	Require(g_guardedDaclSwapSucceeded,
		std::string("guarded DACL swap failed with error ") +
		std::to_string(g_guardedDaclSwapError));
	Require(guardedDaclCommit == SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING &&
		!g_observedSecureTempDacl.empty() &&
		ReadAll(path) == "[PDW]\r\nGuardedDacl=1\r\n",
		"same-byte target DACL swap must keep the verified commit and return a warning");
	Require(EquivalentDaclSecurity(g_observedSecureTempDacl,
		guardedRestrictiveDacl),
		"secure staging must derive its DACL from the same pinned verified target object");
	Require(!guardedDaclRecovery.empty() &&
		error.find(guardedDaclRecovery) != std::string::npos,
		"the committed warning must report the exact displaced concurrent object");
	RemoveFile(guardedDaclRecovery);
	RestoreDaclSecurityDescriptor(path, g_broadDacl);
	SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	WriteAll(path, original);

	backup.clear();
	g_backupVerificationReadFailed = false;
	SetReadFileFailureTestHook(FailBackupVerificationRead);
	const bool unreadableBackupApply = ApplyAdelaideFlexPresetToIni(path,
		ResolvedDevice("Endpoint", "Device"), backup, error);
	SetReadFileFailureTestHook(NULL);
	Require(!unreadableBackupApply && g_backupVerificationReadFailed,
		"an unreadable verified-backup candidate must fail before commit");
	Require(!backup.empty() && ReadAll(backup) == original,
		"an unreadable backup must be retained instead of deleted");
	Require(error.find(backup) != std::string::npos && ReadAll(path) == original,
		"the retained unreadable backup path must be reported without changing the target");
	RemoveFile(backup);
#endif

	Require(SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
		"could not mark test INI read-only");
	Require(!ApplyAdelaideFlexPresetToIni(path, ResolvedDevice("Endpoint", "Device"),
		backup, error) && backup.empty(), "read-only apply must fail before backup");
	Require(ReadAll(path) == original, "read-only failure changed the target");
	Require(SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE,
		"could not clear test read-only attribute");

	HANDLE lock = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Require(lock != INVALID_HANDLE_VALUE, "could not lock test INI");
	backup.clear();
	Require(!ApplyAdelaideFlexPresetToIni(path, ResolvedDevice("Endpoint", "Device"),
		backup, error), "locked commit should fail");
	CloseHandle(lock);
	Require(ReadAll(path) == original, "failed locked transaction changed the original");
	if (!backup.empty())
	{
		Require(ReadAll(backup) == original, "failure-path backup must still be byte-exact");
		RemoveFile(backup);
	}

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
	backup.clear();
	g_beforeReplaceWriterOpened = false;
	g_beforeReplaceWriteCompleted = false;
	g_beforeReplaceWriterError = ERROR_SUCCESS;
	SetBeforeReplaceTestHook(AttemptBeforeReplaceOperatorEdit);
	const bool guardedApply = ApplyAdelaideFlexPresetToIni(path,
		ResolvedDevice("Endpoint", "Device"), backup, error);
	SetBeforeReplaceTestHook(NULL);
	Require(guardedApply, std::string("write-guarded apply failed: ") + error);
	Require(!g_beforeReplaceWriterOpened && !g_beforeReplaceWriteCompleted,
		"the before-replace guard admitted a concurrent operator writer");
	Require(g_beforeReplaceWriterError == ERROR_SHARING_VIOLATION,
		"the before-replace writer must be rejected with a sharing violation");
	Require(IniValue(ReadAll(path), "PDW", "AudioEnabled") == "1",
		"the guarded transaction did not commit its verified replacement");
	Require(!backup.empty() && ReadAll(backup) == original,
		"the guarded transaction must retain its byte-exact pre-change backup");
	RemoveFile(backup);
	WriteAll(path, original);

	SetRestrictiveCurrentUserDacl(path);
	const std::vector<unsigned char> concurrentDeleteDacl =
		ReadDaclSecurityDescriptor(path);
	Require(SetFileAttributesA(path.c_str(),
		FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE) != FALSE,
		"could not protect the concurrent-delete source attributes");
	g_concurrentDeleteSucceeded = false;
	g_concurrentDeleteError = ERROR_SUCCESS;
	SetBeforeReplaceTestHook(DeleteTargetBeforeReplace);
	const bool concurrentDeleteCommit = CommitVerifiedFileTransaction(path,
		original, "[PDW]\r\nReplacement=1\r\n", error);
	SetBeforeReplaceTestHook(NULL);
	const std::string concurrentDeleteRecovery = LastSecondaryRecoveryPathForTest();
	Require(!concurrentDeleteCommit && g_concurrentDeleteSucceeded,
		std::string("concurrent target deletion regression failed with error ") +
		std::to_string(g_concurrentDeleteError));
	Require(GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES,
		"a concurrently deleted target must remain absent, not be recreated from an empty placeholder");
	Require(!concurrentDeleteRecovery.empty() &&
		ReadAll(concurrentDeleteRecovery) == "[PDW]\r\nReplacement=1\r\n",
		"the staged replacement must remain recoverable when the target is concurrently deleted");
	Require(EquivalentDaclSecurity(ReadDaclSecurityDescriptor(concurrentDeleteRecovery),
		concurrentDeleteDacl),
		"a retained settings staging file must keep the target's restrictive DACL");
	Require((GetFileAttributesA(concurrentDeleteRecovery.c_str()) &
		(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE)) ==
		(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE),
		"a retained settings staging file must keep the target's portable attributes");
	RemoveFile(concurrentDeleteRecovery);
	WriteAll(path, original);

	WriteAll(path, std::string());
	g_replaceFileCallCount = 0;
	g_injectPartialMoveOnCall = 1;
	g_partialMoveSucceeded = false;
	g_partialMoveError = ERROR_SUCCESS;
	SetReplaceFileTestHook(InjectPartialMoveReplaceFailure);
	const bool zeroBytePartialCommit = CommitVerifiedFileTransaction(path,
		std::string(), "[PDW]\r\nReplacement=1\r\n", error);
	SetReplaceFileTestHook(NULL);
	const std::string zeroByteRecovery = LastConcurrentRecoveryPathForTest();
	const std::string zeroByteStage = LastSecondaryRecoveryPathForTest();
	Require(!zeroBytePartialCommit && g_partialMoveSucceeded,
		"a genuine zero-byte target 1177 state must fail the transaction");
	Require(GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES && ReadAll(path).empty(),
		"a genuine zero-byte displaced target must be restored as the actual file");
	Require(!zeroByteRecovery.empty() && ReadAll(zeroByteRecovery).empty(),
		"a genuine zero-byte target must retain a distinct recovery pathname");
	Require(!zeroByteStage.empty() &&
		ReadAll(zeroByteStage) == "[PDW]\r\nReplacement=1\r\n" &&
		error.find(zeroByteStage) != std::string::npos,
		"the protected replacement must remain retained and reported after error 1177");
	RemoveFile(zeroByteRecovery);
	RemoveFile(zeroByteStage);
	WriteAll(path, original);

	const std::vector<unsigned char> identityOriginalDacl =
		ReadDaclSecurityDescriptor(path);
	g_identitySwapBytes = original;
	g_identitySwapDacl.clear();
	g_identitySwapSucceeded = false;
	g_identitySwapError = ERROR_SUCCESS;
	SetBeforeReplaceTestHook(AtomicallySwapIdenticalMetadataFile);
	const SettingsTransactionOutcome identicalMetadataCommit =
		CommitVerifiedFileTransaction(path,
		original, "[PDW]\r\nReplacement=1\r\n", error);
	SetBeforeReplaceTestHook(NULL);
	const std::string identityRecovery = LastConcurrentRecoveryPathForTest();
	Require(g_identitySwapSucceeded,
		std::string("identical-byte metadata swap failed with error ") +
		std::to_string(g_identitySwapError));
	Require(identicalMetadataCommit == SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING &&
		error.find(identityRecovery) != std::string::npos,
		"an identical-byte different-file swap must commit with an exact retained-path warning");
	Require(ReadAll(path) == "[PDW]\r\nReplacement=1\r\n" &&
		(GetFileAttributesA(path.c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0 &&
		ReadDaclSecurityDescriptor(path) == g_identitySwapDacl &&
		ReadAll(path + ":operator-metadata") == "alternate-stream-must-survive",
		"the verified commit must remain active with the pathname occupant's metadata");
	Require(!identityRecovery.empty() && ReadAll(identityRecovery) == original &&
		(GetFileAttributesA(identityRecovery.c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0 &&
		ReadDaclSecurityDescriptor(identityRecovery) == g_identitySwapDacl &&
		ReadAll(identityRecovery + ":operator-metadata") == "alternate-stream-must-survive",
		"the retained displaced object must preserve the operator file metadata");
	WriteAll(path, "[OperatorOwned]\r\nLaterInPlaceWrite=active-only\r\n");
	Require(ReadAll(identityRecovery) == original &&
		ReadDaclSecurityDescriptor(identityRecovery) == g_identitySwapDacl &&
		ReadAll(identityRecovery + ":operator-metadata") == "alternate-stream-must-survive",
		"the retained recovery copy must remain independent of later in-place writes");
	RemoveFile(identityRecovery);
	DeleteFileA((path + ":operator-metadata").c_str());
	RestoreDaclSecurityDescriptor(path, identityOriginalDacl);
	SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	WriteAll(path, original);

	backup.clear();
	const std::vector<unsigned char> originalDacl = ReadDaclSecurityDescriptor(path);
	SetRestrictiveCurrentUserDacl(path);
	const std::vector<unsigned char> restrictiveDacl = ReadDaclSecurityDescriptor(path);
	const DWORD recoveryAttributeMask = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE;
	Require(SetFileAttributesA(path.c_str(), recoveryAttributeMask) != FALSE,
		"could not set 1177 metadata-preservation attributes");
	g_replaceFileCallCount = 0;
	g_injectPartialMoveOnCall = 1;
	g_partialMoveSucceeded = false;
	g_partialMoveError = ERROR_SUCCESS;
	SetReplaceFileTestHook(InjectPartialMoveReplaceFailure);
	const bool partialInitialApply = ApplyAdelaideFlexPresetToIni(path,
		ResolvedDevice("Endpoint", "Device"), backup, error);
	SetReplaceFileTestHook(NULL);
	const std::string initialPartialRecovery = LastConcurrentRecoveryPathForTest();
	const std::string initialPartialStage = LastSecondaryRecoveryPathForTest();
	Require(!partialInitialApply && g_replaceFileCallCount == 1,
		"an injected initial partial-move failure must fail the transaction");
	Require(g_partialMoveSucceeded,
		std::string("the initial partial-move injection failed with error ") +
		std::to_string(g_partialMoveError));
	Require(error.find("1177") != std::string::npos,
		"the initial partial-move failure must report Windows error 1177");
	Require(ReadAll(path) == original,
		"PDW must restore a target pathname removed by the initial 1177 state");
	const DWORD restoredAttributes = GetFileAttributesA(path.c_str());
	Require(restoredAttributes != INVALID_FILE_ATTRIBUTES &&
		(restoredAttributes & recoveryAttributeMask) == recoveryAttributeMask,
		"the initial 1177 repair must preserve the displaced file attributes");
	Require(ReadDaclSecurityDescriptor(path) == restrictiveDacl,
		"the initial 1177 repair must preserve the restrictive displaced-file DACL");
	Require(!initialPartialRecovery.empty() && ReadAll(initialPartialRecovery) == original,
		"the initial 1177 displaced bytes must remain in an explicit recovery file");
	Require(!initialPartialStage.empty() &&
		IniValue(ReadAll(initialPartialStage), "PDW", "AudioEnabled") == "1" &&
		error.find(initialPartialStage) != std::string::npos,
		"the Adelaide replacement stage must remain retained and reported after error 1177");
	Require(!backup.empty() && ReadAll(backup) == original,
		"the initial 1177 failure must retain the verified pre-change backup");
	Require(ReadDaclSecurityDescriptor(backup) == restrictiveDacl &&
		(GetFileAttributesA(backup.c_str()) & recoveryAttributeMask) ==
			recoveryAttributeMask,
		"the persistent full-settings backup must preserve the target DACL and portable attributes");
	RestoreDaclSecurityDescriptor(path, originalDacl);
	Require(SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE,
		"could not restore normal test-file attributes");
	RemoveFile(initialPartialRecovery);
	RemoveFile(initialPartialStage);
	RemoveFile(backup);
	WriteAll(path, original);

	backup.clear();
	g_forcedReadFailurePath.clear();
	g_forcedReadFailureObserved = false;
	SetReplaceFileTestHook(CaptureFirstDisplacedPathAndReplace);
	SetReadFileFailureTestHook(FailCapturedDisplacedRead);
	const SettingsTransactionOutcome displacedReadApply =
		ApplyAdelaideFlexPresetToIni(path,
		ResolvedDevice("Endpoint", "Device"), backup, error);
	SetReadFileFailureTestHook(NULL);
	SetReplaceFileTestHook(NULL);
	const std::string unreadableDisplacedRecovery = LastConcurrentRecoveryPathForTest();
	Require(displacedReadApply == SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING &&
		g_forcedReadFailureObserved,
		"an injected post-commit displaced-file read failure must preserve the verified commit with warning");
	Require(!g_forcedReadFailurePath.empty() &&
		unreadableDisplacedRecovery == g_forcedReadFailurePath,
		"the unreadable displaced file must be reported as the recovery file");
	Require(IniValue(ReadAll(path), "PDW", "AudioEnabled") == "1",
		"a post-commit displaced-read failure must leave the verified replacement active");
	Require(ReadAll(unreadableDisplacedRecovery) == original,
		"the unreadable displaced file must remain byte-exact for recovery");
	Require(error.find(unreadableDisplacedRecovery) != std::string::npos &&
		error.find("committed and verified") != std::string::npos,
		"the displaced-read warning must report both commit and recovery path");
	Require(!backup.empty() && ReadAll(backup) == original,
		"the displaced-read failure must retain the verified pre-change backup");
	RemoveFile(unreadableDisplacedRecovery);
	RemoveFile(backup);
	WriteAll(path, original);

	backup.clear();
	g_deleteFileCallCount = 0;
	// The first checked deletion vacates the reserved ReplaceFile backup name;
	// fail the second deletion, which removes the full displaced INI.
	g_failDeleteOnCall = 2;
	g_failedDeletePath.clear();
	SetDeleteFileFailureTestHook(FailSelectedDelete);
	const SettingsTransactionOutcome cleanupFailureApply = ApplyAdelaideFlexPresetToIni(path,
		ResolvedDevice("Endpoint", "Device"), backup, error);
	SetDeleteFileFailureTestHook(NULL);
	const std::string cleanupRecovery = LastConcurrentRecoveryPathForTest();
	Require(cleanupFailureApply == SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING &&
		g_deleteFileCallCount >= 1 &&
		!g_failedDeletePath.empty(),
		"an injected post-commit PAP cleanup failure must preserve the verified commit with warning");
	Require(cleanupRecovery == g_failedDeletePath &&
		error.find(g_failedDeletePath) != std::string::npos,
		"a failed PAP deletion must retain and report its exact pathname");
	Require(ReadAll(cleanupRecovery) == original,
		"the retained PAP cleanup artifact must preserve its full INI bytes");
	Require(IniValue(ReadAll(path), "PDW", "AudioEnabled") == "1",
		"a post-commit cleanup failure must leave the verified Adelaide replacement active");
	Require(!backup.empty() && ReadAll(backup) == original,
		"the cleanup deletion failure must retain the verified pre-change backup");
	RemoveFile(cleanupRecovery);
	RemoveFile(backup);
	WriteAll(path, original);

	g_stagedCommitSwapBytes = "[OperatorOwned]\r\nStagedSwap1177=must-survive\r\n";
	g_stagedCommitSwapSucceeded = false;
	g_stagedCommitSwapError = ERROR_SUCCESS;
	g_replaceFileCallCount = 0;
	g_injectPartialMoveOnCall = 1;
	g_partialMoveSucceeded = false;
	g_partialMoveError = ERROR_SUCCESS;
	SetBeforeStagedCommitTestHook(AtomicallySwapStagedFileBeforeCommit);
	SetReplaceFileTestHook(InjectPartialMoveReplaceFailure);
	const SettingsTransactionOutcome stagedSwapPartialMove =
		CommitVerifiedFileTransaction(path, original,
		"[PDW]\r\nIntendedReplacement=1\r\n", error);
	SetReplaceFileTestHook(NULL);
	SetBeforeStagedCommitTestHook(NULL);
	const std::string stagedSwapRecovery = LastConcurrentRecoveryPathForTest();
	const std::string stagedSwapReplacement = LastSecondaryRecoveryPathForTest();
	Require(stagedSwapPartialMove == SETTINGS_TRANSACTION_NOT_COMMITTED &&
		g_replaceFileCallCount == 1 && g_partialMoveSucceeded &&
		g_stagedCommitSwapSucceeded,
		"the combined staged-path swap and error-1177 regression must fail before commit");
	Require(ReadAll(path) == original,
		"error-1177 recovery must restore the exact guarded pre-commit object");
	Require(!stagedSwapRecovery.empty() && ReadAll(stagedSwapRecovery) == original,
		"error-1177 recovery must retain an independent copy of the guarded object");
	Require(!stagedSwapReplacement.empty() &&
		ReadAll(stagedSwapReplacement) == g_stagedCommitSwapBytes,
		"the swapped staged object must be retained rather than deleted after ReplaceFile failure");
	Require(error.find("1177") != std::string::npos &&
		error.find(stagedSwapReplacement) != std::string::npos,
		"the combined pre-commit failure must report error 1177 and the retained stage path");
	RemoveFile(stagedSwapRecovery);
	RemoveFile(stagedSwapReplacement);
	WriteAll(path, original);

	backup.clear();
	g_atomicPathSwapSucceeded = false;
	g_atomicPathSwapError = ERROR_SUCCESS;
	SetBeforeReplaceTestHook(AtomicallySwapOperatorEdit);
	const SettingsTransactionOutcome atomicSwapApply = ApplyAdelaideFlexPresetToIni(path,
		ResolvedDevice("Endpoint", "Device"), backup, error);
	SetBeforeReplaceTestHook(NULL);
	const std::string atomicRecovery = LastConcurrentRecoveryPathForTest();
	Require(g_atomicPathSwapSucceeded,
		std::string("the deterministic atomic pathname swap failed with error ") +
		std::to_string(g_atomicPathSwapError));
	Require(atomicSwapApply == SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING &&
		error.find(atomicRecovery) != std::string::npos,
		"an atomic pathname swap must preserve the verified commit with an exact warning");
	Require(IniValue(ReadAll(path), "PDW", "AudioEnabled") == "1",
		"PDW must leave the verified Adelaide replacement active after the commit point");
	Require(!atomicRecovery.empty() && ReadAll(atomicRecovery) == ATOMIC_PATH_SWAP_BYTES,
		"the atomically displaced operator object must remain at an explicit recovery path");
	Require(!backup.empty() && ReadAll(backup) == original,
		"the atomic-swap failure must retain the verified pre-change backup");
	RemoveFile(atomicRecovery);
	RemoveFile(backup);
	WriteAll(path, original);
#endif

	const std::string missing = MakeTemporaryPath();
	RemoveFile(missing);
	backup.clear();
	Require(!ApplyAdelaideFlexPresetToIni(missing, ResolvedDevice("Endpoint", "Device"),
		backup, error) && backup.empty(), "missing target must fail without backup");

	const std::string directory = MakeTemporaryPath();
	RemoveFile(directory);
	Require(CreateDirectoryA(directory.c_str(), NULL) != FALSE, "could not create test directory");
	Require(!ApplyAdelaideFlexPresetToIni(directory, ResolvedDevice("Endpoint", "Device"),
		backup, error), "directory target must fail");
	RemoveDirectoryA(directory.c_str());

	Require(!RestoreIniFromVerifiedBackup(path, missing, error),
		"missing restore source must fail");
	Require(ReadAll(path) == original, "missing backup restore changed target");
	Require(!RestoreIniFromVerifiedBackup(path, path, error),
		"restoring a file from itself must fail");

	const std::string restoreBackup = MakeTemporaryPath();
	WriteAll(restoreBackup, "replacement bytes\r\n");
	Require(SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
		"could not mark restore target read-only");
	Require(!RestoreIniFromVerifiedBackup(path, restoreBackup, error),
		"read-only restore target must fail");
	Require(ReadAll(path) == original, "failed read-only restore changed target");
	SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);

	lock = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Require(lock != INVALID_HANDLE_VALUE, "could not lock restore target");
	Require(!RestoreIniFromVerifiedBackup(path, restoreBackup, error),
		"locked restore commit should fail");
	CloseHandle(lock);
	Require(ReadAll(path) == original, "failed locked restore changed target");

	RemoveFile(path);
	RemoveFile(restoreBackup);
}
}

int main()
{
	TestPresetValues();
	TestUtf8ValidationAndIniEncoding();
	TestDeviceResolution();
	TestSaveDecision();
	TestExplicitApplyBackupAndRestore();
	TestTransactionalFailures();
	std::cout << "local audio profile tests passed\n";
	return 0;
}
