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

	std::cout << "audio signal core tests passed\n";
	return 0;
}
