#ifndef PDW_AUDIO_SIGNAL_CORE_H
#define PDW_AUDIO_SIGNAL_CORE_H

#include <cstddef>
#include <cstdint>

namespace pdw
{
namespace signal
{

// Canonical sample conversion for modern capture backends. The legacy WinMM
// path intentionally remains unchanged until recording-based comparisons exist.
float NormalizePcm8(std::uint8_t sample);
float NormalizePcm16(std::int16_t sample);
float ClampNormalized(float sample);
int LegacyPcm8Value(std::uint8_t sample);

// Combines PDW's field-tested two-level sign decision with the enhanced
// slicer's inner/outer decision. This keeps legacy FLEX phases A/C on their
// original bit path while enabling four-level phases B/D alongside them.
unsigned char HybridFourLevelSymbol(unsigned char legacyTwoLevelSymbol,
	unsigned char enhancedFourLevelSymbol);

struct AdaptiveSlicerConfig
{
	float dcTrackingRate;
	float envelopeAttackRate;
	float envelopeReleaseRate;
	float minimumEnvelope;

	AdaptiveSlicerConfig();
};

struct AdaptiveSlicerState
{
	float dcOffset;
	float envelope;
	float normalizedLevel;
	float confidence;
};

// Tracks slow DC drift and signal amplitude independently of the protocol
// parser. Symbol numbering matches PDW's existing FLEX frame input: positive
// outer level is 0 and negative outer level is 3.
class AdaptiveSlicer
{
public:
	explicit AdaptiveSlicer(const AdaptiveSlicerConfig& config = AdaptiveSlicerConfig());

	void Reset();
	AdaptiveSlicerState Observe(float sample);
	unsigned char SliceTwoLevel(float sample);
	unsigned char SliceFourLevel(float sample);
	unsigned char CurrentTwoLevelSymbol() const;
	unsigned char CurrentFourLevelSymbol() const;
	const AdaptiveSlicerState& state() const;

private:
	AdaptiveSlicerConfig config_;
	AdaptiveSlicerState state_;
};

} // namespace signal
} // namespace pdw

#endif
