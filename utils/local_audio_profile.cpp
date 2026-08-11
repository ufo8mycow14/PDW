#include "local_audio_profile.h"

#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>

#include <cstring>
#include <string>
#include <vector>

#include "headers/pdw.h"
#include "headers/initapp.h"
#include "headers/mobitex.h"
#include "headers/sound_in.h"

namespace pdw
{
namespace audio_profile
{

namespace
{
	std::string WideToUtf8(const wchar_t* value)
	{
		if (!value || !value[0]) return std::string();
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			value, -1, NULL, 0, NULL, NULL);
		if (required <= 1) return std::string();
		std::vector<char> text(static_cast<std::size_t>(required));
		if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
			&text[0], required, NULL, NULL)) return std::string();
		return std::string(&text[0]);
	}

	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty()) return std::wstring();
		const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			value.c_str(), -1, NULL, 0);
		if (required <= 1) return std::wstring();
		std::vector<wchar_t> text(static_cast<std::size_t>(required));
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
			&text[0], required)) return std::wstring();
		return std::wstring(&text[0]);
	}

	bool ValidDevice(const CaptureDevice& device)
	{
		std::string error;
		return (device.endpointId.empty() || ValidateUtf8Field(device.endpointId,
			MAX_ENDPOINT_ID_UTF8_BYTES, error)) &&
			ValidateUtf8Field(device.friendlyName, MAX_ENDPOINT_NAME_UTF8_BYTES, error);
	}

	std::string FullFriendlyNameForEndpoint(const std::string& endpointId)
	{
		const std::wstring wideId = Utf8ToWide(endpointId);
		if (wideId.empty()) return std::string();
		const HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		const bool uninitialize = SUCCEEDED(initialized);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return std::string();

		IMMDeviceEnumerator* enumerator = NULL;
		IMMDevice* device = NULL;
		IPropertyStore* properties = NULL;
		PROPVARIANT name;
		PropVariantInit(&name);
		std::string result;
		HRESULT status = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
		if (SUCCEEDED(status)) status = enumerator->GetDevice(wideId.c_str(), &device);
		if (SUCCEEDED(status)) status = device->OpenPropertyStore(STGM_READ, &properties);
		if (SUCCEEDED(status)) status = properties->GetValue(PKEY_Device_FriendlyName, &name);
		if (SUCCEEDED(status) && name.vt == VT_LPWSTR) result = WideToUtf8(name.pwszVal);

		PropVariantClear(&name);
		if (properties) properties->Release();
		if (device) device->Release();
		if (enumerator) enumerator->Release();
		if (uninitialize) CoUninitialize();
		return result;
	}

	std::vector<CaptureDevice> EnumerateCoreAudioDevices()
	{
		std::vector<CaptureDevice> devices;
		const HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		const bool uninitialize = SUCCEEDED(initialized);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return devices;

		IMMDeviceEnumerator* enumerator = NULL;
		IMMDeviceCollection* collection = NULL;
		HRESULT status = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
		if (SUCCEEDED(status))
			status = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
		UINT count = 0;
		if (SUCCEEDED(status)) status = collection->GetCount(&count);
		for (UINT index = 0; SUCCEEDED(status) && index < count; ++index)
		{
			IMMDevice* device = NULL;
			LPWSTR identifier = NULL;
			IPropertyStore* properties = NULL;
			PROPVARIANT name;
			PropVariantInit(&name);
			HRESULT itemStatus = collection->Item(index, &device);
			if (SUCCEEDED(itemStatus)) itemStatus = device->GetId(&identifier);
			if (SUCCEEDED(itemStatus)) itemStatus = device->OpenPropertyStore(STGM_READ, &properties);
			if (SUCCEEDED(itemStatus)) itemStatus = properties->GetValue(PKEY_Device_FriendlyName, &name);
			if (SUCCEEDED(itemStatus) && identifier && name.vt == VT_LPWSTR)
			{
				CaptureDevice capture;
				capture.endpointId = WideToUtf8(identifier);
				capture.friendlyName = WideToUtf8(name.pwszVal);
				if (!capture.endpointId.empty() && ValidDevice(capture)) devices.push_back(capture);
			}
			PropVariantClear(&name);
			if (properties) properties->Release();
			if (identifier) CoTaskMemFree(identifier);
			if (device) device->Release();
		}

		if (collection) collection->Release();
		if (enumerator) enumerator->Release();
		if (uninitialize) CoUninitialize();
		return devices;
	}

	std::string WinmmEndpointId(UINT deviceIndex)
	{
		DWORD byteCount = 0;
		const HWAVEIN device = reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(deviceIndex));
		if (waveInMessage(device, DRV_QUERYFUNCTIONINSTANCEIDSIZE,
			reinterpret_cast<DWORD_PTR>(&byteCount), 0) != MMSYSERR_NOERROR ||
			byteCount < sizeof(wchar_t) || byteCount > 64u * 1024u) return std::string();
		std::vector<wchar_t> identifier(byteCount / sizeof(wchar_t) + 1, L'\0');
		if (waveInMessage(device, DRV_QUERYFUNCTIONINSTANCEID,
			reinterpret_cast<DWORD_PTR>(&identifier[0]), byteCount) != MMSYSERR_NOERROR)
			return std::string();
		return WideToUtf8(&identifier[0]);
	}

	DecoderProfileValues CurrentValues()
	{
		DecoderProfileValues values = {};
		values.audioEnabled = Profile.audioEnabled;
		values.audioSource = Profile.audioSource;
		values.audioDevice = Profile.audioDevice;
		values.audioSampleRate = Profile.audioSampleRate;
		values.audioConfiguration = Profile.audioConfig;
		values.comPortEnabled = Profile.comPortEnabled;
		values.decodePocsag = Profile.decodepocsag;
		values.decodeFlex = Profile.decodeflex;
		values.monitorPaging = Profile.monitor_paging ? 1 : 0;
		values.pocsagShowBoth = Profile.pocsag_showboth;
		values.showCfs = Profile.show_cfs;
		values.flex1600 = Profile.flex_1600;
		values.flex3200 = Profile.flex_3200;
		values.flex6400 = Profile.flex_6400;
		values.bitSync = mb.bitsync;
		values.minimumMessageLength = mb.min_msg_len;
		values.invertData = Profile.invert;
		values.panePercent = Profile.percent;
		values.threshold1600 = Profile.audioThreshold[INDEX1600];
		values.auxiliaryCustomValuesAreDefault =
			Profile.audioThreshold[INDEX512] == 0 &&
			Profile.audioThreshold[INDEX1200] == 0 &&
			Profile.audioThreshold[INDEX2400] == 0 &&
			Profile.audioResync[INDEX512] == 0 && Profile.audioResync[INDEX1200] == 0 &&
			Profile.audioResync[INDEX1600] == 0 && Profile.audioResync[INDEX2400] == 0 &&
			Profile.audioCentering[INDEX512] == 0 && Profile.audioCentering[INDEX1200] == 0 &&
			Profile.audioCentering[INDEX1600] == 0 && Profile.audioCentering[INDEX2400] == 0 ? 1 : 0;
		return values;
	}

	bool HasAdelaideFlexPresetId()
	{
		return std::strcmp(Profile.audioProfileId, ADELAIDE_FLEX_PRESET_ID) == 0;
	}

	bool HasDeterministicAdelaideCustomValues()
	{
		return Profile.audioThreshold[INDEX512] == 0 &&
			Profile.audioThreshold[INDEX1200] == 0 &&
			Profile.audioThreshold[INDEX1600] == 2 &&
			Profile.audioThreshold[INDEX2400] == 0 &&
			Profile.audioResync[INDEX512] == 0 && Profile.audioResync[INDEX1200] == 0 &&
			Profile.audioResync[INDEX1600] == 0 && Profile.audioResync[INDEX2400] == 0 &&
			Profile.audioCentering[INDEX512] == 0 && Profile.audioCentering[INDEX1200] == 0 &&
			Profile.audioCentering[INDEX1600] == 0 && Profile.audioCentering[INDEX2400] == 0;
	}

	template <std::size_t Size>
	void CopyProfileField(char (&destination)[Size], const std::string& value)
	{
		strncpy_s(destination, Size, value.c_str(), _TRUNCATE);
	}

	void ApplyPresetToMemory(const DeviceResolution& resolution)
	{
		const DecoderProfileValues values = AdelaideFlexPreset();
		Profile.audioEnabled = values.audioEnabled;
		Profile.audioSource = values.audioSource;
		Profile.audioDevice = resolution.winmmIndex >= 0 ? resolution.winmmIndex : values.audioDevice;
		Profile.audioSampleRate = values.audioSampleRate;
		Profile.audioConfig = values.audioConfiguration;
		Profile.comPortEnabled = values.comPortEnabled;
		Profile.decodepocsag = values.decodePocsag;
		Profile.decodeflex = values.decodeFlex;
		Profile.monitor_paging = values.monitorPaging != 0;
		Profile.pocsag_showboth = values.pocsagShowBoth;
		Profile.show_cfs = values.showCfs;
		Profile.flex_1600 = values.flex1600;
		Profile.flex_3200 = values.flex3200;
		Profile.flex_6400 = values.flex6400;
		mb.bitsync = values.bitSync;
		mb.bitsync_rev = ~mb.bitsync;
		mb.min_msg_len = values.minimumMessageLength;
		Profile.invert = values.invertData;
		Profile.percent = values.panePercent;
		Profile.audioThreshold[INDEX1600] = values.threshold1600;
		Profile.audioThreshold[INDEX512] = 0;
		Profile.audioThreshold[INDEX1200] = 0;
		Profile.audioThreshold[INDEX2400] = 0;
		std::memset(Profile.audioResync, 0, sizeof(Profile.audioResync));
		std::memset(Profile.audioCentering, 0, sizeof(Profile.audioCentering));
		CopyProfileField(Profile.audioProfileId, ADELAIDE_FLEX_PRESET_ID);
		CopyProfileField(Profile.audioProfileName, ADELAIDE_FLEX_PRESET_NAME);
		CopyProfileField(Profile.audioDeviceEndpointId, resolution.endpointId);
		CopyProfileField(Profile.audioDeviceName, resolution.friendlyName);
		Profile.audioDeviceIdentityInvalid = 0;
		SetAudioConfig(Profile.audioConfig);
	}

	bool PersistResolution(const DeviceResolution& resolution)
	{
		std::string error;
		if (!resolution.found || resolution.usedCompatibilityIndex ||
			resolution.endpointId.empty() ||
			!ValidateUtf8Field(resolution.endpointId, MAX_ENDPOINT_ID_UTF8_BYTES, error) ||
			!ValidateUtf8Field(resolution.friendlyName, MAX_ENDPOINT_NAME_UTF8_BYTES, error))
			return false;
		if (Profile.audioDeviceIdentityInvalid == 0 &&
			std::strcmp(Profile.audioDeviceEndpointId, resolution.endpointId.c_str()) == 0 &&
			std::strcmp(Profile.audioDeviceName, resolution.friendlyName.c_str()) == 0 &&
			(resolution.winmmIndex < 0 || Profile.audioDevice == resolution.winmmIndex)) return true;

		const PROFILE previous = Profile;
		if (resolution.winmmIndex >= 0) Profile.audioDevice = resolution.winmmIndex;
		CopyProfileField(Profile.audioDeviceEndpointId, resolution.endpointId);
		CopyProfileField(Profile.audioDeviceName, resolution.friendlyName);
		Profile.audioDeviceIdentityInvalid = 0;
		if (TryWriteSettings()) return true;
		Profile = previous;
		return false;
	}
}

std::vector<CaptureDevice> EnumerateCaptureDevices()
{
	std::vector<CaptureDevice> devices = EnumerateCoreAudioDevices();
	const UINT count = waveInGetNumDevs();
	for (UINT index = 0; index < count; ++index)
	{
		WAVEINCAPSW capabilities = {};
		if (waveInGetDevCapsW(index, &capabilities, sizeof(capabilities)) != MMSYSERR_NOERROR)
			continue;
		const std::string endpointId = WinmmEndpointId(index);
		bool attached = false;
		if (!endpointId.empty())
		{
			for (std::vector<CaptureDevice>::iterator device = devices.begin();
				device != devices.end(); ++device)
			{
				if (device->endpointId != endpointId) continue;
				device->winmmIndex = static_cast<int>(index);
				attached = true;
				break;
			}
		}
		if (attached) continue;
		CaptureDevice device;
		device.winmmIndex = static_cast<int>(index);
		device.endpointId = endpointId;
		device.friendlyName = endpointId.empty() ? std::string() :
			FullFriendlyNameForEndpoint(endpointId);
		if (device.friendlyName.empty()) device.friendlyName = WideToUtf8(capabilities.szPname);
		if (ValidDevice(device)) devices.push_back(device);
	}
	return devices;
}

bool IsAdelaideFlexPresetSelected()
{
	return HasAdelaideFlexPresetId() && IsAdelaideFlexProfile(CurrentValues()) &&
		HasDeterministicAdelaideCustomValues();
}

bool ResolveConfiguredCaptureDevice(DeviceResolution& resolution, std::string& error)
{
	error.clear();
	resolution = DeviceResolution();
	if (Profile.audioDeviceIdentityInvalid)
	{
		error = "The saved Windows audio identity is invalid or damaged. Select the intended input explicitly in Interface Setup before enabling capture.";
		return false;
	}
	const bool hasEndpointIdentity = Profile.audioDeviceEndpointId[0] != '\0';
	const bool hasFriendlyBootstrap = Profile.audioDeviceName[0] != '\0';
	const bool hasProfileIdentity = Profile.audioProfileId[0] != '\0' ||
		Profile.audioProfileName[0] != '\0';
	const bool stableSelection = hasEndpointIdentity || hasFriendlyBootstrap || hasProfileIdentity;
	if (hasProfileIdentity && !hasEndpointIdentity && !hasFriendlyBootstrap)
	{
		error = "The named audio profile has no saved endpoint identity. Reconnect VB-Cable and explicitly apply the profile again.";
		return false;
	}

	const std::vector<CaptureDevice> devices = EnumerateCaptureDevices();
	resolution = ResolveCaptureDevice(devices, Profile.audioDeviceEndpointId,
		Profile.audioDeviceName, Profile.audioDevice, !stableSelection);
	if (!resolution.found)
	{
		if (stableSelection)
			error = std::string("The configured capture endpoint is unavailable or ambiguous: ") +
				(Profile.audioDeviceName[0] ? Profile.audioDeviceName : "saved Windows audio endpoint") +
				". Reconnect it or explicitly select the intended input; PDW will not choose another device.";
		else
			error = "Windows did not report the configured local audio capture device. Open Interface Setup and select an input.";
		return false;
	}
	if (stableSelection && resolution.endpointId.empty())
	{
		error = "The configured input has no stable Windows endpoint identity. Select an active Windows capture endpoint explicitly in Interface Setup.";
		return false;
	}
	return true;
}

bool ResolveAdelaideFlexCaptureDevice(DeviceResolution& resolution, std::string& error)
{
	resolution = ResolveCaptureDevice(EnumerateCaptureDevices(), std::string(),
		VB_CABLE_CAPTURE_NAME, 0, false);
	if (resolution.found && !resolution.usedCompatibilityIndex &&
		!resolution.endpointId.empty())
	{
		error.clear();
		return true;
	}
	resolution = DeviceResolution();
	error = "PDW requires exactly one active CABLE Output (VB-Audio Virtual Cable) recording endpoint. Install or reconnect VB-Cable, remove duplicate/ambiguous endpoints, then try again.";
	return false;
}

bool RememberWinmmCaptureDevice(int winmmIndex)
{
	const std::vector<CaptureDevice> devices = EnumerateCaptureDevices();
	const CaptureDevice* selectedDeviceInfo = NULL;
	for (std::vector<CaptureDevice>::const_iterator device = devices.begin();
		device != devices.end(); ++device)
	{
		if (device->winmmIndex != winmmIndex) continue;
		if (selectedDeviceInfo && (selectedDeviceInfo->endpointId != device->endpointId ||
			selectedDeviceInfo->friendlyName != device->friendlyName)) return false;
		selectedDeviceInfo = &*device;
	}
	if (!selectedDeviceInfo || !ValidDevice(*selectedDeviceInfo)) return false;

	Profile.audioDevice = winmmIndex;
	Profile.audioDeviceIdentityInvalid = 0;
	if (selectedDeviceInfo->endpointId.empty())
	{
		Profile.audioDeviceEndpointId[0] = '\0';
		Profile.audioDeviceName[0] = '\0';
		Profile.audioProfileId[0] = '\0';
		Profile.audioProfileName[0] = '\0';
		return true;
	}
	CopyProfileField(Profile.audioDeviceEndpointId, selectedDeviceInfo->endpointId);
	CopyProfileField(Profile.audioDeviceName, selectedDeviceInfo->friendlyName);
	if (!FriendlyDeviceNamesMatch(selectedDeviceInfo->friendlyName, VB_CABLE_CAPTURE_NAME))
	{
		Profile.audioProfileId[0] = '\0';
		Profile.audioProfileName[0] = '\0';
	}
	return true;
}

void RefreshSelectedAudioProfileIdentity()
{
	if (!HasAdelaideFlexPresetId() || IsAdelaideFlexPresetSelected()) return;
	Profile.audioProfileId[0] = '\0';
	Profile.audioProfileName[0] = '\0';
}

bool CommitCaptureDeviceResolution(const DeviceResolution& resolution)
{
	return PersistResolution(resolution);
}

SettingsTransactionOutcome ApplyAdelaideFlexPreset(const DeviceResolution& resolution,
	std::string& backupPath, std::string& error)
{
	const SettingsTransactionOutcome outcome = ApplyAdelaideFlexPresetToIni(
		szIniPathName, resolution, backupPath, error);
	if (!SettingsTransactionCommitted(outcome)) return outcome;
	ApplyPresetToMemory(resolution);
	return outcome;
}

SettingsTransactionOutcome RestoreAudioProfileBackup(const std::string& backupPath,
	std::string& error)
{
	return RestoreIniFromVerifiedBackup(szIniPathName, backupPath, error);
}

} // namespace audio_profile
} // namespace pdw
