#include "rtl_tcp_source.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
class CollectingSink : public pdw::signal::AudioSampleSink
{
public:
	CollectingSink() : callbacks(0), samples(0) {}
	void OnAudioSamples(const float*, std::size_t sampleCount, std::uint32_t, bool)
	{
		InterlockedIncrement(&callbacks);
		samples += sampleCount;
	}
	volatile LONG callbacks;
	std::size_t samples;
};

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}
}

int main(int argc, char** argv)
{
	using namespace pdw::signal;
	const std::uint32_t iqRate = 960000;
	const std::uint32_t audioRate = 48000;
	RtlFmDemodulator demodulator(iqRate, audioRate);
	std::vector<unsigned char> iq;
	const std::size_t complexSamples = 96000;
	iq.reserve(complexSamples * 2);
	double phase = 0.0;
	const double phaseStep = 0.08;
	for (std::size_t index = 0; index < complexSamples; ++index)
	{
		const int i = static_cast<int>(127.5 + 100.0 * std::cos(phase));
		const int q = static_cast<int>(127.5 + 100.0 * std::sin(phase));
		iq.push_back(static_cast<unsigned char>(i));
		iq.push_back(static_cast<unsigned char>(q));
		phase += phaseStep;
	}
	std::vector<float> audio;
	demodulator.ProcessUnsignedIq(&iq[0], iq.size(), audio);
	Expect(audio.size() >= 4798 && audio.size() <= 4801, "RTL-TCP resampling ratio");
	double average = 0.0;
	for (std::size_t index = 10; index < audio.size(); ++index) average += audio[index];
	average /= static_cast<double>(audio.size() - 10);
	Expect(std::fabs(average - phaseStep / 3.14159265358979323846) < 0.001,
		"FM discriminator preserves phase direction and magnitude");

	demodulator.Reset();
	demodulator.ProcessUnsignedIq(NULL, 0, audio);
	Expect(audio.empty(), "empty IQ input is safe");

	if (argc > 1)
	{
		RtlTcpConfig config;
		config.receiverLibraryPath = argv[1];
		config.frequencyHz = 148812500;
		config.sampleRate = 2400000;
		config.audioSampleRate = 44100;
		config.gainTenthsDb = 328;
		config.nfmBandwidthHz = 12000;
		CollectingSink sink;
		RtlSdrSource source;
		Expect(!source.Start(config, 99, &sink), "unavailable RTL-SDR device reports failure");
		Expect(source.state() == RTL_TCP_FAILED, "unavailable RTL-SDR state is failed");

		RtlTcpConfig lossConfig = config;
		lossConfig.frequencyHz = 148812501;
		Expect(source.Start(lossConfig, 0, &sink), "RTL-SDR starts before simulated async loss");
		const DWORD lossWaitStarted = GetTickCount();
		while (source.state() != RTL_TCP_FAILED && GetTickCount() - lossWaitStarted < 2000) Sleep(5);
		Expect(source.state() == RTL_TCP_FAILED, "unexpected async end is reported as failed");
		Expect(source.lastError().find("ended unexpectedly") != std::string::npos,
			"unexpected async end retains an actionable error");

		const bool started = source.Start(config, 0, &sink);
		if (!started) std::cerr << "RTL-SDR start error: " << source.lastError() << '\n';
		Expect(started, "RTL-SDR source starts");
		const DWORD waitStarted = GetTickCount();
		while (!source.lastIqCallbackTick() && GetTickCount() - waitStarted < 2000) Sleep(5);
		Expect(source.lastIqCallbackTick() != 0, "RTL-SDR exposes last IQ callback time");
		while (InterlockedCompareExchange(&sink.callbacks, 0, 0) == 0 &&
			GetTickCount() - waitStarted < 2000) Sleep(5);
		Expect(InterlockedCompareExchange(&sink.callbacks, 0, 0) > 0,
			"RTL-SDR callback reaches the audio sink");
		source.Stop();
		Expect(source.state() == RTL_TCP_STOPPED, "mock RTL-SDR stops cleanly");
	}

	std::cout << "RTL-TCP demodulator tests passed\n";
	return 0;
}
