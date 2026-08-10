#include "audio_signal_core.h"

#include <algorithm>
#include <cmath>

namespace pdw
{
namespace signal
{

namespace
{
float Clamp(float value, float minimum, float maximum)
{
	return std::max(minimum, std::min(maximum, value));
}
}

float NormalizePcm8(std::uint8_t sample)
{
	return ClampNormalized((static_cast<int>(sample) - 128) / 128.0f);
}

int LegacyPcm8Value(std::uint8_t sample)
{
	return static_cast<int>(sample) - 128;
}

unsigned char HybridFourLevelSymbol(unsigned char legacyTwoLevelSymbol,
	unsigned char enhancedFourLevelSymbol)
{
	const bool legacyPositive = legacyTwoLevelSymbol < 2;
	const bool enhancedOuter = enhancedFourLevelSymbol == 0 || enhancedFourLevelSymbol == 3;
	if (legacyPositive) return enhancedOuter ? 0 : 1;
	return enhancedOuter ? 3 : 2;
}

float NormalizePcm16(std::int16_t sample)
{
	return ClampNormalized(static_cast<float>(sample) / 32768.0f);
}

float ClampNormalized(float sample)
{
	return Clamp(sample, -1.0f, 1.0f);
}

AdaptiveSlicerConfig::AdaptiveSlicerConfig()
	: dcTrackingRate(0.001f),
	  envelopeAttackRate(0.08f),
	  envelopeReleaseRate(0.001f),
	  minimumEnvelope(0.02f)
{
}

AdaptiveSlicer::AdaptiveSlicer(const AdaptiveSlicerConfig& config)
	: config_(config)
{
	Reset();
}

void AdaptiveSlicer::Reset()
{
	state_.dcOffset = 0.0f;
	state_.envelope = config_.minimumEnvelope;
	state_.normalizedLevel = 0.0f;
	state_.confidence = 0.0f;
}

AdaptiveSlicerState AdaptiveSlicer::Observe(float sample)
{
	sample = ClampNormalized(sample);
	state_.dcOffset += config_.dcTrackingRate * (sample - state_.dcOffset);

	const float magnitude = std::fabs(sample - state_.dcOffset);
	const float rate = magnitude > state_.envelope
		? config_.envelopeAttackRate
		: config_.envelopeReleaseRate;
	state_.envelope += rate * (magnitude - state_.envelope);
	state_.envelope = std::max(config_.minimumEnvelope, state_.envelope);

	state_.normalizedLevel = Clamp((sample - state_.dcOffset) / state_.envelope, -1.5f, 1.5f);
	state_.confidence = Clamp((state_.envelope - config_.minimumEnvelope) /
		(0.25f - config_.minimumEnvelope), 0.0f, 1.0f);
	return state_;
}

unsigned char AdaptiveSlicer::SliceTwoLevel(float sample)
{
	Observe(sample);
	return CurrentTwoLevelSymbol();
}

unsigned char AdaptiveSlicer::SliceFourLevel(float sample)
{
	Observe(sample);
	return CurrentFourLevelSymbol();
}

unsigned char AdaptiveSlicer::CurrentTwoLevelSymbol() const
{
	return state_.normalizedLevel >= 0.0f ? 0 : 3;
}

unsigned char AdaptiveSlicer::CurrentFourLevelSymbol() const
{
	const float level = state_.normalizedLevel;
	if (level >= 2.0f / 3.0f) return 0;
	if (level >= 0.0f) return 1;
	if (level >= -2.0f / 3.0f) return 2;
	return 3;
}

const AdaptiveSlicerState& AdaptiveSlicer::state() const
{
	return state_;
}

} // namespace signal
} // namespace pdw
