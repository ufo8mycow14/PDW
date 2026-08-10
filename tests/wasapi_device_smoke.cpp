#include "wasapi_capture.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>

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

private:
	volatile LONG sampleCount_;
	volatile LONG discontinuities_;
	volatile LONG sampleRate_;
};
}

int main()
{
	CountingSink sink;
	pdw::signal::WasapiCaptureSource capture;
	if (!capture.StartDefault(&sink))
	{
		std::cerr << "WASAPI start failed: " << capture.lastError() << '\n';
		return 1;
	}
	Sleep(1000);
	capture.Stop();
	std::cout << "samples=" << sink.sampleCount()
		<< " sample_rate=" << sink.sampleRate()
		<< " discontinuities=" << sink.discontinuities() << '\n';
	return sink.sampleCount() > 0 && sink.sampleRate() > 0 ? 0 : 2;
}
