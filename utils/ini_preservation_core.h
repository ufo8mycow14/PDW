#ifndef PDW_INI_PRESERVATION_CORE_H
#define PDW_INI_PRESERVATION_CORE_H

#include <string>

#include "local_audio_profile_core.h"

namespace pdw
{
namespace ini
{

// Merge the complete set of settings emitted by PDW into an existing INI.
// PDW-owned keys are authoritative; unknown keys, comments, and sections are
// retained so older hardware options and future extensions survive a save.
std::string MergeKnownSettings(const std::string& existing,
	const std::string& generated);

// Atomically replace path with the merged settings using a temporary file in
// the same directory. The original remains in place if any step fails.
pdw::audio_profile::SettingsTransactionOutcome WriteMergedSettingsFile(const std::string& path,
	const std::string& generated, std::string& error);

// Full generated settings can include credentials. These helpers create a
// protected same-directory snapshot and require checked, explicitly reported
// removal before its bytes may be applied to PDW.INI.
bool CreateSensitiveSettingsTemporaryFile(const std::string& referencePath,
	const char* prefix, std::string& temporaryPath, void*& nativeHandle,
	std::string& error);
bool MarkSensitiveSettingsTemporaryFileForDeletion(void* nativeHandle,
	const std::string& path, std::string& error);

#ifdef PDW_INI_PRESERVATION_TEST_HOOKS
typedef void (*BeforeMissingIniCommitTestHook)(const char* targetPath,
	const char* stagedPath);
typedef void (*BeforeExistingIniCommitTestHook)(const char* targetPath);
void SetBeforeMissingIniCommitTestHook(BeforeMissingIniCommitTestHook hook);
void SetBeforeExistingIniCommitTestHook(BeforeExistingIniCommitTestHook hook);
#endif

} // namespace ini
} // namespace pdw

#endif
