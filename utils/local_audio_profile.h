#ifndef PDW_LOCAL_AUDIO_PROFILE_H
#define PDW_LOCAL_AUDIO_PROFILE_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "local_audio_profile_core.h"

namespace pdw
{
namespace audio_profile
{

std::vector<CaptureDevice> EnumerateCaptureDevices();
bool ResolveConfiguredCaptureDevice(DeviceResolution& resolution, std::string& error);
bool ResolveAdelaideFlexCaptureDevice(DeviceResolution& resolution, std::string& error);
bool RememberWinmmCaptureDevice(int winmmIndex);
bool IsAdelaideFlexPresetSelected();
void RefreshSelectedAudioProfileIdentity();
bool CommitCaptureDeviceResolution(const DeviceResolution& resolution);
SettingsTransactionOutcome ApplyAdelaideFlexPreset(const DeviceResolution& resolution,
	std::string& backupPath,
	std::string& error);
SettingsTransactionOutcome RestoreAudioProfileBackup(const std::string& backupPath,
	std::string& error);

} // namespace audio_profile
} // namespace pdw

#endif
