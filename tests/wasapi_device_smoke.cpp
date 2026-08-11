#include "wasapi_capture.h"

#include <windows.h>
#include <mmdeviceapi.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
class CountingSink : public pdw::signal::WasapiCaptureSink
{
public:
	CountingSink() : sampleCount_(0), discontinuities_(0), sampleRate_(0) {}

	void OnAudioSamples(const float*, std::size_t sampleCount,
		std::uint32_t sampleRate, bool discontinuity)
	{
		InterlockedExchangeAdd(&sampleCount_, static_cast<LONG>(sampleCount));
		InterlockedExchange(&sampleRate_, static_cast<LONG>(sampleRate));
		if (discontinuity) InterlockedIncrement(&discontinuities_);
	}

	LONG sampleCount() const { return InterlockedCompareExchange(const_cast<LONG*>(&sampleCount_), 0, 0); }
	LONG sampleRate() const { return InterlockedCompareExchange(const_cast<LONG*>(&sampleRate_), 0, 0); }
	LONG discontinuities() const { return InterlockedCompareExchange(const_cast<LONG*>(&discontinuities_), 0, 0); }
	void Reset()
	{
		InterlockedExchange(&sampleCount_, 0);
		InterlockedExchange(&sampleRate_, 0);
		InterlockedExchange(&discontinuities_, 0);
	}

private:
	volatile LONG sampleCount_;
	volatile LONG discontinuities_;
	volatile LONG sampleRate_;
};

std::string DefaultCaptureEndpointId()
{
	const HRESULT initialized = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return std::string();
	const bool uninitialize = SUCCEEDED(initialized);
	IMMDeviceEnumerator* enumerator = NULL;
	IMMDevice* device = NULL;
	LPWSTR wideId = NULL;
	std::string endpointId;
	HRESULT status = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
	if (SUCCEEDED(status)) status = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
	if (SUCCEEDED(status)) status = device->GetId(&wideId);
	if (SUCCEEDED(status) && wideId)
	{
		const int required = WideCharToMultiByte(CP_UTF8, 0, wideId, -1, NULL, 0, NULL, NULL);
		if (required > 1)
		{
			std::vector<char> utf8(static_cast<std::size_t>(required));
			if (WideCharToMultiByte(CP_UTF8, 0, wideId, -1, &utf8[0], required, NULL, NULL))
				endpointId.assign(&utf8[0]);
		}
	}
	if (wideId) CoTaskMemFree(wideId);
	if (device) device->Release();
	if (enumerator) enumerator->Release();
	if (uninitialize) CoUninitialize();
	return endpointId;
}
}

int main()
{
	CountingSink sink;
	pdw::signal::WasapiCaptureSource capture;
	if (!capture.StartDefault(&sink))
	{
		std::cerr << "Default WASAPI start failed: " << capture.lastError() << '\n';
		return 1;
	}
	Sleep(1000);
	if (!capture.Stop())
	{
		std::cerr << "Default WASAPI capture did not stop cleanly: " << capture.lastError() << '\n';
		return 1;
	}
	std::cout << "default samples=" << sink.sampleCount()
		<< " sample_rate=" << sink.sampleRate()
		<< " discontinuities=" << sink.discontinuities() << '\n';
	if (sink.sampleCount() <= 0 || sink.sampleRate() <= 0) return 2;

	// A confirmed stop makes resetting the shared sink safe and ensures the
	// endpoint-specific pass cannot succeed on samples left by the default pass.
	sink.Reset();
	const std::string endpointId = DefaultCaptureEndpointId();
	if (endpointId.empty())
	{
		std::cerr << "Windows did not report a default capture endpoint ID.\n";
		return 1;
	}
	if (!capture.StartEndpoint(endpointId, &sink))
	{
		std::cerr << "Endpoint-specific WASAPI start failed: " << capture.lastError() << '\n';
		return 1;
	}
	Sleep(1000);
	if (!capture.Stop())
	{
		std::cerr << "Endpoint-specific WASAPI capture did not stop cleanly: "
			<< capture.lastError() << '\n';
		return 1;
	}
	std::cout << "endpoint samples=" << sink.sampleCount()
		<< " sample_rate=" << sink.sampleRate()
		<< " discontinuities=" << sink.discontinuities() << '\n';
	return sink.sampleCount() > 0 && sink.sampleRate() > 0 ? 0 : 2;
}
