#include "audio_signal_core.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
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

void Warm(pdw::signal::AdaptiveSlicer& slicer, float value, int count = 200)
{
	for (int i = 0; i < count; ++i) slicer.Observe(value);
}

// Exercise symbol-duration runs, rather than one sample per alternating level.
// Tracking the random data's short-term mean as DC corrupts the inner/outer
// decision even when the waveform has no noise and the sign bit is correct.
unsigned FourLevelSequenceErrors(unsigned baud, float dc)
{
	pdw::signal::AdaptiveSlicer slicer;
	const double samplesPerSymbol = 44100.0 / baud;
	const float levels[] = { 0.375f, 0.125f, -0.125f, -0.375f };
	std::uint32_t random = 554;
	unsigned errors = 0;
	for (unsigned symbol = 0; symbol < 4352; ++symbol)
	{
		random = random * 1664525u + 1013904223u;
		const unsigned expected = symbol < 256 ? (symbol & 1 ? 0 : 3) : random >> 30;
		const unsigned start = static_cast<unsigned>(symbol * samplesPerSymbol);
		const unsigned end = static_cast<unsigned>((symbol + 1) * samplesPerSymbol);
		const float value = levels[expected] + dc;
		for (unsigned sample = start; sample < end; ++sample)
		{
			slicer.Observe(value);
			if (symbol >= 256 && sample == (start + end) / 2 &&
				slicer.CurrentFourLevelSymbol() != expected) ++errors;
		}
	}
	return errors;
}
}

int main()
{
	using pdw::signal::HybridFourLevelSymbol;
	Expect(HybridFourLevelSymbol(0, 0) == 0, "legacy positive plus enhanced outer maps to symbol 0");
	Expect(HybridFourLevelSymbol(0, 1) == 1, "legacy positive plus enhanced inner maps to symbol 1");
	Expect(HybridFourLevelSymbol(3, 2) == 2, "legacy negative plus enhanced inner maps to symbol 2");
	Expect(HybridFourLevelSymbol(3, 3) == 3, "legacy negative plus enhanced outer maps to symbol 3");
	for (unsigned char enhanced = 0; enhanced < 4; ++enhanced)
	{
		Expect(HybridFourLevelSymbol(0, enhanced) < 2,
			"hybrid symbol must preserve the positive legacy phase bit");
		Expect(HybridFourLevelSymbol(3, enhanced) >= 2,
			"hybrid symbol must preserve the negative legacy phase bit");
	}
	using namespace pdw::signal;

	Expect(ResyncSymbolClock(29, 15, 28, false) == 29,
		"legacy and acquisition timing retain immediate edge placement");
	Expect(ResyncSymbolClock(29, -1, 28, true) == 29,
		"an uninitialised clock acquires directly");
	Expect(std::fabs(ResyncSymbolClock(43, 15, 28, true) - 15) < 1e-9,
		"a whole-symbol phase difference must not skip a symbol");
	Expect(std::fabs(ResyncSymbolClock(47, 15, 28, true) -
		ResyncSymbolClock(19, 15, 28, true)) < 1e-9,
		"locked timing correction is independent of the edge's symbol number");
	Expect(ResyncSymbolClock(26, 15, 28, true) > 15 &&
		ResyncSymbolClock(26, 15, 28, true) < 16,
		"a noisy late edge must not pull the sample outside the established eye");

	Expect(std::fabs(NormalizePcm8(0) + 1.0f) < 0.0001f, "PCM8 minimum");
	Expect(std::fabs(NormalizePcm8(128)) < 0.0001f, "PCM8 midpoint");
	Expect(NormalizePcm8(255) > 0.99f, "PCM8 maximum");
	Expect(std::fabs(NormalizePcm16(-32768) + 1.0f) < 0.0001f, "PCM16 minimum");
	Expect(NormalizePcm16(32767) > 0.999f, "PCM16 maximum");
	Expect(ClampNormalized(5.0f) == 1.0f, "positive clamp");
	Expect(ClampNormalized(-5.0f) == -1.0f, "negative clamp");
	for (int sample = 0; sample <= 255; ++sample)
		Expect(LegacyPcm8Value(static_cast<std::uint8_t>(sample)) == sample - 128,
			"legacy PCM8 conversion is exhaustive and deterministic");

	AdaptiveSlicer twoLevel;
	for (int i = 0; i < 300; ++i)
	{
		const float dc = 0.18f;
		const float level = (i & 1) ? dc + 0.45f : dc - 0.45f;
		const unsigned char expected = (i & 1) ? 0 : 3;
		Expect(twoLevel.SliceTwoLevel(level) == expected,
			"two-level slicing with DC offset");
	}
	Expect(twoLevel.state().confidence > 0.5f, "signal confidence rises");

	AdaptiveSlicer fourLevel;
	// Train amplitude/DC tracking using the outer levels, then verify all four
	// equally spaced FLEX symbols. The legacy decoder's 0/3 polarity is kept.
	for (int i = 0; i < 500; ++i)
		fourLevel.Observe((i & 1) ? 0.9f : -0.7f);
	Expect(fourLevel.SliceFourLevel(0.9f) == 0, "four-level outer positive");
	Expect(fourLevel.SliceFourLevel(0.37f) == 1, "four-level inner positive");
	Expect(fourLevel.SliceFourLevel(-0.17f) == 2, "four-level inner negative");
	Expect(fourLevel.SliceFourLevel(-0.7f) == 3, "four-level outer negative");

	AdaptiveSlicer resetTest;
	Warm(resetTest, 0.8f);
	resetTest.Reset();
	Expect(resetTest.state().dcOffset == 0.0f, "reset DC offset");
	Expect(resetTest.state().confidence == 0.0f, "reset confidence");
	for (unsigned baud : { 1600u, 3200u })
		for (float offset : { -0.05f, 0.0f, 0.05f })
		{
			const unsigned errors = FourLevelSequenceErrors(baud, offset);
			if (errors) std::cerr << "Four-level clean sequence: baud=" << baud
				<< " DC=" << offset << " symbol errors=" << errors << '\n';
			Expect(errors == 0, "clean four-level data must not move its own decision thresholds");
		}

	std::cout << "audio signal core tests passed\n";
	return 0;
}
