#ifndef PDW_WASAPI_CAPTURE_H
#define PDW_WASAPI_CAPTURE_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <mmreg.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "audio_capture_core.h"

namespace pdw
{
namespace signal
{

enum WasapiSampleType
{
	WASAPI_SAMPLE_PCM8,
	WASAPI_SAMPLE_PCM16,
	WASAPI_SAMPLE_PCM24,
	WASAPI_SAMPLE_PCM32,
	WASAPI_SAMPLE_FLOAT32
};

struct WasapiSampleFormat
{
	WasapiSampleType type;
	std::uint16_t channels;
	std::uint16_t bytesPerSample;
	std::uint16_t validBitsPerSample;
	std::uint32_t sampleRate;
};

bool DescribeWasapiFormat(const WAVEFORMATEX* waveFormat, WasapiSampleFormat& format);
bool ConvertWasapiFramesToMono(const unsigned char* frames,
	std::size_t frameCount,
	const WasapiSampleFormat& format,
	std::vector<float>& monoSamples);

// Thread-owned handles and callback state may be released only after Windows
// has positively confirmed that the capture thread exited. This small policy
// helper is public so the timeout/failure cases have deterministic unit tests.
bool WasapiThreadResourcesMayBeReleased(DWORD waitResult);

typedef AudioSampleSink WasapiCaptureSink;

enum WasapiCaptureState
{
	WASAPI_CAPTURE_STOPPED,
	WASAPI_CAPTURE_STARTING,
	WASAPI_CAPTURE_RUNNING,
	WASAPI_CAPTURE_DEVICE_LOST,
	WASAPI_CAPTURE_FAILED
};

class WasapiCaptureSource
{
public:
	WasapiCaptureSource();
	~WasapiCaptureSource();

	bool StartDefault(WasapiCaptureSink* sink);
	bool StartEndpoint(const std::string& endpointId, WasapiCaptureSink* sink);
	bool Stop();
	WasapiCaptureState state() const;
	std::string lastError() const;

private:
	WasapiCaptureSource(const WasapiCaptureSource&);
	WasapiCaptureSource& operator=(const WasapiCaptureSource&);

	static DWORD WINAPI ThreadEntry(LPVOID context);
	DWORD CaptureThread();
	bool Start(const std::wstring& endpointId, WasapiCaptureSink* sink);
	void CleanupStoppedThread();
	void SetState(WasapiCaptureState state, const char* error);

	mutable CRITICAL_SECTION lock_;
	HANDLE stopEvent_;
	HANDLE readyEvent_;
	HANDLE sampleEvent_;
	HANDLE thread_;
	WasapiCaptureSink* sink_;
	WasapiCaptureState state_;
	std::string lastError_;
	std::wstring endpointId_;
};

} // namespace signal
} // namespace pdw

#endif
