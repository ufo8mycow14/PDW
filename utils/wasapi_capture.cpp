#include "wasapi_capture.h"

#include "audio_signal_core.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pdw
{
namespace signal
{

namespace
{
template <typename T>
void ReleaseInterface(T*& value)
{
	if (value)
	{
		value->Release();
		value = NULL;
	}
}

float ReadPcm24(const unsigned char* sample)
{
	int value = static_cast<int>(sample[0]) |
		(static_cast<int>(sample[1]) << 8) |
		(static_cast<int>(sample[2]) << 16);
	if (value & 0x00800000) value |= static_cast<int>(0xff000000);
	return ClampNormalized(static_cast<float>(value) / 8388608.0f);
}

float ReadSample(const unsigned char* sample, const WasapiSampleFormat& format)
{
	switch (format.type)
	{
		case WASAPI_SAMPLE_PCM8:
			return NormalizePcm8(sample[0]);
		case WASAPI_SAMPLE_PCM16:
		{
			std::int16_t value = 0;
			std::memcpy(&value, sample, sizeof(value));
			return NormalizePcm16(value);
		}
		case WASAPI_SAMPLE_PCM24:
			return ReadPcm24(sample);
		case WASAPI_SAMPLE_PCM32:
		{
			std::int32_t value = 0;
			std::memcpy(&value, sample, sizeof(value));
			const unsigned int validBits = format.validBitsPerSample ? format.validBitsPerSample : 32;
			if (validBits < 32) value >>= (32 - validBits);
			const double scale = std::ldexp(1.0, static_cast<int>(validBits - 1));
			return ClampNormalized(static_cast<float>(value / scale));
		}
		case WASAPI_SAMPLE_FLOAT32:
		{
			float value = 0.0f;
			std::memcpy(&value, sample, sizeof(value));
			return ClampNormalized(value);
		}
	}
	return 0.0f;
}

const char* HResultMessage(HRESULT result)
{
	if (result == AUDCLNT_E_DEVICE_INVALIDATED) return "Audio device was removed or changed.";
	if (result == AUDCLNT_E_UNSUPPORTED_FORMAT) return "Windows audio mix format is unsupported.";
	if (result == AUDCLNT_E_SERVICE_NOT_RUNNING) return "Windows Audio service is not running.";
	if (result == E_ACCESSDENIED) return "Access to the audio capture device was denied.";
	return "Unable to initialize Windows audio capture.";
}
}

bool DescribeWasapiFormat(const WAVEFORMATEX* waveFormat, WasapiSampleFormat& format)
{
	if (!waveFormat || waveFormat->nChannels == 0 || waveFormat->nSamplesPerSec == 0)
		return false;

	WORD formatTag = waveFormat->wFormatTag;
	WORD validBits = waveFormat->wBitsPerSample;
	if (formatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		if (waveFormat->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
			return false;
		const WAVEFORMATEXTENSIBLE* extensible =
			reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(waveFormat);
		validBits = extensible->Samples.wValidBitsPerSample;
		if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
			formatTag = WAVE_FORMAT_IEEE_FLOAT;
		else if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
			formatTag = WAVE_FORMAT_PCM;
		else
			return false;
	}

	format.channels = waveFormat->nChannels;
	format.sampleRate = waveFormat->nSamplesPerSec;
	format.validBitsPerSample = validBits;
	format.bytesPerSample = static_cast<std::uint16_t>(waveFormat->wBitsPerSample / 8);

	if (formatTag == WAVE_FORMAT_IEEE_FLOAT && waveFormat->wBitsPerSample == 32)
		format.type = WASAPI_SAMPLE_FLOAT32;
	else if (formatTag == WAVE_FORMAT_PCM && waveFormat->wBitsPerSample == 8)
		format.type = WASAPI_SAMPLE_PCM8;
	else if (formatTag == WAVE_FORMAT_PCM && waveFormat->wBitsPerSample == 16)
		format.type = WASAPI_SAMPLE_PCM16;
	else if (formatTag == WAVE_FORMAT_PCM && waveFormat->wBitsPerSample == 24)
		format.type = WASAPI_SAMPLE_PCM24;
	else if (formatTag == WAVE_FORMAT_PCM && waveFormat->wBitsPerSample == 32 && validBits >= 16)
		format.type = WASAPI_SAMPLE_PCM32;
	else
		return false;

	return format.bytesPerSample != 0;
}

bool ConvertWasapiFramesToMono(const unsigned char* frames,
	std::size_t frameCount,
	const WasapiSampleFormat& format,
	std::vector<float>& monoSamples)
{
	monoSamples.clear();
	if ((!frames && frameCount != 0) || format.channels == 0 || format.bytesPerSample == 0)
		return false;
	monoSamples.reserve(frameCount);
	const std::size_t frameBytes = static_cast<std::size_t>(format.channels) * format.bytesPerSample;
	for (std::size_t frame = 0; frame < frameCount; ++frame)
	{
		float mono = 0.0f;
		for (std::size_t channel = 0; channel < format.channels; ++channel)
			mono += ReadSample(frames + frame * frameBytes + channel * format.bytesPerSample, format);
		monoSamples.push_back(ClampNormalized(mono / format.channels));
	}
	return true;
}

WasapiCaptureSource::WasapiCaptureSource()
	: stopEvent_(NULL), readyEvent_(NULL), sampleEvent_(NULL), thread_(NULL),
	  sink_(NULL), state_(WASAPI_CAPTURE_STOPPED)
{
	InitializeCriticalSection(&lock_);
}

WasapiCaptureSource::~WasapiCaptureSource()
{
	Stop();
	DeleteCriticalSection(&lock_);
}

bool WasapiCaptureSource::StartDefault(WasapiCaptureSink* sink)
{
	if (!sink) return false;
	Stop();
	sink_ = sink;
	stopEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	readyEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	sampleEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!stopEvent_ || !readyEvent_ || !sampleEvent_)
	{
		SetState(WASAPI_CAPTURE_FAILED, "Unable to create audio synchronization events.");
		Stop();
		return false;
	}
	SetState(WASAPI_CAPTURE_STARTING, NULL);
	thread_ = CreateThread(NULL, 0, ThreadEntry, this, 0, NULL);
	if (!thread_)
	{
		SetState(WASAPI_CAPTURE_FAILED, "Unable to create the audio capture thread.");
		Stop();
		return false;
	}
	if (WaitForSingleObject(readyEvent_, 5000) != WAIT_OBJECT_0)
	{
		SetState(WASAPI_CAPTURE_FAILED, "Windows audio capture did not start in time.");
		Stop();
		return false;
	}
	return state() == WASAPI_CAPTURE_RUNNING;
}

void WasapiCaptureSource::Stop()
{
	if (stopEvent_) SetEvent(stopEvent_);
	if (thread_)
	{
		WaitForSingleObject(thread_, 5000);
		CloseHandle(thread_);
		thread_ = NULL;
	}
	if (sampleEvent_) { CloseHandle(sampleEvent_); sampleEvent_ = NULL; }
	if (readyEvent_) { CloseHandle(readyEvent_); readyEvent_ = NULL; }
	if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = NULL; }
	sink_ = NULL;
	if (state() != WASAPI_CAPTURE_FAILED && state() != WASAPI_CAPTURE_DEVICE_LOST)
		SetState(WASAPI_CAPTURE_STOPPED, NULL);
}

WasapiCaptureState WasapiCaptureSource::state() const
{
	EnterCriticalSection(&lock_);
	const WasapiCaptureState value = state_;
	LeaveCriticalSection(&lock_);
	return value;
}

std::string WasapiCaptureSource::lastError() const
{
	EnterCriticalSection(&lock_);
	const std::string value = lastError_;
	LeaveCriticalSection(&lock_);
	return value;
}

void WasapiCaptureSource::SetState(WasapiCaptureState state, const char* error)
{
	EnterCriticalSection(&lock_);
	state_ = state;
	if (error) lastError_ = error;
	else if (state == WASAPI_CAPTURE_STARTING || state == WASAPI_CAPTURE_RUNNING || state == WASAPI_CAPTURE_STOPPED)
		lastError_.clear();
	LeaveCriticalSection(&lock_);
}

DWORD WINAPI WasapiCaptureSource::ThreadEntry(LPVOID context)
{
	return static_cast<WasapiCaptureSource*>(context)->CaptureThread();
}

DWORD WasapiCaptureSource::CaptureThread()
{
	IMMDeviceEnumerator* enumerator = NULL;
	IMMDevice* device = NULL;
	IAudioClient* audioClient = NULL;
	IAudioCaptureClient* captureClient = NULL;
	WAVEFORMATEX* mixFormat = NULL;
	HRESULT result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	const bool uninitializeCom = SUCCEEDED(result);
	if (result == RPC_E_CHANGED_MODE) result = S_OK;

	if (SUCCEEDED(result))
		result = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
	if (SUCCEEDED(result)) result = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
	if (SUCCEEDED(result))
		result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
			reinterpret_cast<void**>(&audioClient));
	if (SUCCEEDED(result)) result = audioClient->GetMixFormat(&mixFormat);

	WasapiSampleFormat sampleFormat = {};
	if (SUCCEEDED(result) && !DescribeWasapiFormat(mixFormat, sampleFormat))
		result = AUDCLNT_E_UNSUPPORTED_FORMAT;
	if (SUCCEEDED(result))
		result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
			0, 0, mixFormat, NULL);
	if (SUCCEEDED(result)) result = audioClient->SetEventHandle(sampleEvent_);
	if (SUCCEEDED(result))
		result = audioClient->GetService(__uuidof(IAudioCaptureClient),
			reinterpret_cast<void**>(&captureClient));
	if (SUCCEEDED(result)) result = audioClient->Start();

	if (FAILED(result))
	{
		SetState(result == AUDCLNT_E_DEVICE_INVALIDATED ? WASAPI_CAPTURE_DEVICE_LOST : WASAPI_CAPTURE_FAILED,
			HResultMessage(result));
		SetEvent(readyEvent_);
	}
	else
	{
		SetState(WASAPI_CAPTURE_RUNNING, NULL);
		SetEvent(readyEvent_);
		HANDLE events[2] = { stopEvent_, sampleEvent_ };
		bool running = true;
		std::vector<float> monoSamples;
		while (running)
		{
			const DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
			if (waitResult == WAIT_OBJECT_0) break;
			if (waitResult != WAIT_OBJECT_0 + 1)
			{
				SetState(WASAPI_CAPTURE_FAILED, "Windows audio event wait failed.");
				break;
			}

			UINT32 packetFrames = 0;
			result = captureClient->GetNextPacketSize(&packetFrames);
			while (SUCCEEDED(result) && packetFrames > 0)
			{
				BYTE* data = NULL;
				UINT32 frames = 0;
				DWORD flags = 0;
				result = captureClient->GetBuffer(&data, &frames, &flags, NULL, NULL);
				if (FAILED(result)) break;
				const bool discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
				if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
					monoSamples.assign(frames, 0.0f);
				else if (!ConvertWasapiFramesToMono(data, frames, sampleFormat, monoSamples))
				{
					captureClient->ReleaseBuffer(frames);
					SetState(WASAPI_CAPTURE_FAILED, "Windows audio samples could not be converted.");
					running = false;
					break;
				}
				if (sink_ && !monoSamples.empty())
					sink_->OnAudioSamples(&monoSamples[0], monoSamples.size(), sampleFormat.sampleRate, discontinuity);
				result = captureClient->ReleaseBuffer(frames);
				if (FAILED(result)) break;
				result = captureClient->GetNextPacketSize(&packetFrames);
			}
			if (FAILED(result))
			{
				SetState(result == AUDCLNT_E_DEVICE_INVALIDATED ? WASAPI_CAPTURE_DEVICE_LOST : WASAPI_CAPTURE_FAILED,
					HResultMessage(result));
				break;
			}
		}
		audioClient->Stop();
		if (state() == WASAPI_CAPTURE_RUNNING) SetState(WASAPI_CAPTURE_STOPPED, NULL);
	}

	if (mixFormat) CoTaskMemFree(mixFormat);
	ReleaseInterface(captureClient);
	ReleaseInterface(audioClient);
	ReleaseInterface(device);
	ReleaseInterface(enumerator);
	if (uninitializeCom) CoUninitialize();
	return 0;
}

} // namespace signal
} // namespace pdw
