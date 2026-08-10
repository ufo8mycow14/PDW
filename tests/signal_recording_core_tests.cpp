#include "audio_signal_core.h"
#include "signal_recording_core.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}
}

int main()
{
	using namespace pdw::signal;
	const std::string wavPath = "pdw-synthetic-pocsag.wav";
	const std::string sigmfBase = "pdw-synthetic-pocsag";
	std::remove(wavPath.c_str());
	std::remove((sigmfBase + ".sigmf-meta").c_str());
	std::remove((sigmfBase + ".sigmf-data").c_str());

	SignalRecording generated;
	generated.sampleRate = 48000;
	const int samplesPerSymbol = 40; // 1200 baud
	const int symbolCount = 240;
	for (int symbol = 0; symbol < symbolCount; ++symbol)
	{
		const float level = (symbol & 1) ? 0.62f : -0.38f; // includes +0.12 DC
		for (int sample = 0; sample < samplesPerSymbol; ++sample)
		{
			const float noise = static_cast<float>(((symbol * 17 + sample * 13) % 11) - 5) * 0.002f;
			generated.samples.push_back(level + noise);
		}
	}

	std::string error;
	Expect(WriteWav16Mono(wavPath, generated, error), error.c_str());
	SignalRecording wavRoundTrip;
	Expect(ReadWavMono(wavPath, wavRoundTrip, error), error.c_str());
	Expect(wavRoundTrip.sampleRate == generated.sampleRate, "WAV sample rate round trip");
	Expect(wavRoundTrip.samples.size() == generated.samples.size(), "WAV sample count round trip");
	for (std::size_t index = 0; index < generated.samples.size(); ++index)
		Expect(std::fabs(wavRoundTrip.samples[index] - generated.samples[index]) < 0.0001f,
			"WAV16 sample tolerance");

	AdaptiveSlicer slicer;
	for (int symbol = 0; symbol < symbolCount; ++symbol)
	{
		unsigned char sliced = 0;
		for (int sample = 0; sample < samplesPerSymbol; ++sample)
			sliced = slicer.SliceTwoLevel(wavRoundTrip.samples[symbol * samplesPerSymbol + sample]);
		if (symbol > 10)
			Expect(sliced == ((symbol & 1) ? 0 : 3), "synthetic POCSAG preamble slicing");
	}

	Expect(WriteSigMfReal32(sigmfBase, wavRoundTrip, error), error.c_str());
	SignalRecording sigmfRoundTrip;
	Expect(ReadSigMfReal32(sigmfBase, sigmfRoundTrip, error), error.c_str());
	Expect(sigmfRoundTrip.sampleRate == generated.sampleRate, "SigMF sample rate round trip");
	Expect(sigmfRoundTrip.samples.size() == generated.samples.size(), "SigMF sample count round trip");
	for (std::size_t index = 0; index < generated.samples.size(); ++index)
		Expect(sigmfRoundTrip.samples[index] == wavRoundTrip.samples[index], "SigMF float sample round trip");

	AdaptiveSlicer flexSlicer;
	for (int symbol = 0; symbol < 320; ++symbol)
	{
		const unsigned char expected = static_cast<unsigned char>(symbol % 4);
		const float levels[4] = { 0.90f, 0.36f, -0.18f, -0.72f };
		unsigned char sliced = 0;
		for (int sample = 0; sample < 15; ++sample) // 3200 symbols/s at 48 kHz
		{
			const float noise = static_cast<float>(((symbol * 7 + sample * 5) % 9) - 4) * 0.0015f;
			sliced = flexSlicer.SliceFourLevel(levels[expected] + noise);
		}
		if (symbol > 40) Expect(sliced == expected, "synthetic four-level FLEX slicing");
	}

	std::remove(wavPath.c_str());
	std::remove((sigmfBase + ".sigmf-meta").c_str());
	std::remove((sigmfBase + ".sigmf-data").c_str());
	std::cout << "signal recording core tests passed\n";
	return 0;
}
