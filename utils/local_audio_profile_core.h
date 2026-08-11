#ifndef PDW_LOCAL_AUDIO_PROFILE_CORE_H
#define PDW_LOCAL_AUDIO_PROFILE_CORE_H

#include <cstddef>
#include <string>
#include <vector>

namespace pdw
{
namespace audio_profile
{

extern const char* const ADELAIDE_FLEX_PRESET_ID;
extern const char* const ADELAIDE_FLEX_PRESET_NAME;
extern const char* const VB_CABLE_CAPTURE_NAME;

extern const std::size_t MAX_PROFILE_ID_UTF8_BYTES;
extern const std::size_t MAX_PROFILE_NAME_UTF8_BYTES;
extern const std::size_t MAX_ENDPOINT_ID_UTF8_BYTES;
extern const std::size_t MAX_ENDPOINT_NAME_UTF8_BYTES;

struct DecoderProfileValues
{
	int audioEnabled;
	int audioSource;
	int audioDevice;
	int audioSampleRate;
	int audioConfiguration;
	int comPortEnabled;
	int decodePocsag;
	int decodeFlex;
	int monitorPaging;
	int pocsagShowBoth;
	int showCfs;
	int flex1600;
	int flex3200;
	int flex6400;
	int bitSync;
	int minimumMessageLength;
	int invertData;
	int panePercent;
	int threshold1600;
	int auxiliaryCustomValuesAreDefault;
};

struct CaptureDevice
{
	CaptureDevice();
	int winmmIndex;
	std::string endpointId;
	std::string friendlyName;
};

struct DeviceResolution
{
	DeviceResolution();
	bool found;
	bool usedCompatibilityIndex;
	int winmmIndex;
	std::string endpointId;
	std::string friendlyName;
};

enum CaptureDeviceSaveDecision
{
	CAPTURE_SAVE_KEEP_CONFIGURED_IDENTITY,
	CAPTURE_SAVE_BIND_EXPLICIT_SELECTION,
	CAPTURE_SAVE_REJECT_UNAVAILABLE_STABLE_IDENTITY
};

DecoderProfileValues AdelaideFlexPreset();
bool IsAdelaideFlexProfile(const DecoderProfileValues& values);

// Endpoint IDs are opaque Windows identities and are compared byte-for-byte.
// Friendly names are only a bootstrap aid when no endpoint ID has been saved.
bool FriendlyDeviceNamesMatch(const std::string& first, const std::string& second);
DeviceResolution ResolveCaptureDevice(const std::vector<CaptureDevice>& devices,
	const std::string& preferredEndpointId,
	const std::string& preferredFriendlyName,
	int compatibilityIndex,
	bool allowCompatibilityFallback);

// An unchanged UI selection must not overwrite a configured stable endpoint
// merely because that endpoint is temporarily unavailable. A deliberate user
// selection is the only path that may bind a different endpoint identity.
CaptureDeviceSaveDecision DecideCaptureDeviceSave(bool explicitSelectionMade,
	bool hasConfiguredStableEndpoint,
	bool configuredStableEndpointResolved);

// INI profile strings are stored as ASCII-only "utf8-hex:" fields. The byte
// limit applies to the decoded UTF-8 value, not the encoded representation.
bool ValidateUtf8Field(const std::string& value,
	std::size_t maximumUtf8Bytes,
	std::string& error);
bool EncodeIniUtf8Field(const std::string& value,
	std::size_t maximumUtf8Bytes,
	std::string& encoded,
	std::string& error);
bool DecodeIniUtf8Field(const std::string& encoded,
	std::size_t maximumUtf8Bytes,
	std::string& value,
	std::string& error);

enum SettingsTransactionOutcome
{
	SETTINGS_TRANSACTION_NOT_COMMITTED = 0,
	SETTINGS_TRANSACTION_COMMITTED,
	SETTINGS_TRANSACTION_COMMITTED_WITH_WARNING
};

bool SettingsTransactionCommitted(SettingsTransactionOutcome outcome);

// This is an explicit user action. It requires a resolved stable endpoint,
// creates and verifies a byte-exact backup, stages a comment-preserving merge,
// and atomically commits only after the staged file has been verified.
SettingsTransactionOutcome ApplyAdelaideFlexPresetToIni(const std::string& iniPath,
	const DeviceResolution& resolvedDevice,
	std::string& backupPath,
	std::string& error);

// Restore stages and verifies the complete backup before an atomic replacement,
// then verifies that the restored target is byte-for-byte identical.
SettingsTransactionOutcome RestoreIniFromVerifiedBackup(const std::string& iniPath,
	const std::string& backupPath,
	std::string& error);

// Shared by normal PDW.INI saves: the caller supplies the exact bytes it read
// and the fully generated replacement. The transaction detects pathname swaps,
// retains displaced versions, recovers documented ReplaceFile partial moves,
// and verifies the final commit byte-for-byte.
SettingsTransactionOutcome CommitVerifiedFileTransaction(const std::string& path,
	const std::string& expectedCurrent,
	const std::string& replacement,
	std::string& error);
SettingsTransactionOutcome CommitVerifiedFileTransactionForIdentity(const std::string& path,
	const std::string& expectedCurrent, const std::string& replacement,
	unsigned long volumeSerial, unsigned long fileIndexHigh,
	unsigned long fileIndexLow, std::string& error);

// Create a same-directory empty temporary file with the reference file's DACL
// applied atomically at creation. If the reference does not exist, a protected
// current-user-only DACL is used. The returned native Windows handle remains
// exclusive so callers write/flush the exact protected object rather than
// reopening a raceable pathname. The caller owns that handle and must remove
// the file with the checked helper after closing it.
bool CreateSecureTemporarySettingsFile(const std::string& referencePath,
	const char* prefix, std::string& temporaryPath, void*& nativeHandle,
	std::string& error);
bool MarkSensitiveTemporaryFileForDeletion(void* nativeHandle,
	const std::string& path, std::string& error);

#ifdef PDW_LOCAL_AUDIO_PROFILE_TEST_HOOKS
typedef void (*BeforeReplaceTestHook)(const char* targetPath);
typedef void (*BeforeSecureTempWriteTestHook)(const char* stagedPath);
typedef void (*BeforeVerifiedDeleteTestHook)(const char* path);
typedef void (*AfterTargetGuardTestHook)(const char* targetPath);
typedef void (*BeforeStagedCommitTestHook)(const char* stagedPath);
typedef bool (*ReplaceFileTestHook)(const char* replacedPath,
	const char* replacementPath, const char* backupPath, unsigned long flags);
typedef bool (*PathFailureTestHook)(const char* path);
void SetBeforeReplaceTestHook(BeforeReplaceTestHook hook);
void SetBeforeSecureTempWriteTestHook(BeforeSecureTempWriteTestHook hook);
void SetBeforeVerifiedDeleteTestHook(BeforeVerifiedDeleteTestHook hook);
void SetAfterTargetGuardTestHook(AfterTargetGuardTestHook hook);
void SetBeforeStagedCommitTestHook(BeforeStagedCommitTestHook hook);
void SetReplaceFileTestHook(ReplaceFileTestHook hook);
void SetReadFileFailureTestHook(PathFailureTestHook hook);
void SetDeleteFileFailureTestHook(PathFailureTestHook hook);
const char* LastConcurrentRecoveryPathForTest();
const char* LastSecondaryRecoveryPathForTest();
#endif

} // namespace audio_profile
} // namespace pdw

#endif
