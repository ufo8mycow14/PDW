#ifndef PDW_RADIO_PROCESSING_H
#define PDW_RADIO_PROCESSING_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdw { namespace signal {

constexpr int DefaultNfmBandwidthHz = 12500;

enum RadioDemodulation { RADIO_NFM = 0, RADIO_WFM = 1, RADIO_AM = 2 };

// Plain value type: copying PROFILE preserves pending/rollback settings.
struct RadioProcessingSettings
{
	int demodulation;
	int wfmCutoffHz;
	int amCutoffHz;
	int squelchEnabled;
	int squelchThresholdDbfs;
	int squelchHysteresisDb;
	int squelchHangMs;
	int deEmphasis;
	int speechEnabled;
	int speechHighPassHz;
	int speechLowPassHz;
	int noiseReductionEnabled;
	int noiseThresholdDbfs;
	int noiseReductionDb;
	int noiseAttackMs;
	int noiseReleaseMs;
	int dcBlockEnabled;
	int audioGainEnabled;
	int audioGainDb;
	int agcEnabled;
	int agcTargetDbfs;
	int agcMaxGainDb;
	int agcAttackMs;
	int agcReleaseMs;
	int limiterEnabled;
	int limiterCeilingDbfs;
};

struct RadioSetting
{
	const char* key;
	const char* label;
	int RadioProcessingSettings::* member;
	int minimum, maximum, defaultValue;
};

const RadioSetting* RadioSettings(std::size_t& count);
RadioProcessingSettings DefaultRadioProcessingSettings();
void NormalizeRadioProcessingSettings(RadioProcessingSettings& settings);
bool ValidateRadioProcessingSettings(const RadioProcessingSettings& settings,
	std::uint32_t audioRate, std::string& error);
const char* RadioDemodulationName(int mode);
bool RadioAudioProcessingEnabled(const RadioProcessingSettings& settings);

// Streaming, bounded-state post-demodulation processing. Off is an exact
// bypass; no automatic gain, gating or voice filtering is applied implicitly.
class RadioAudioProcessor
{
public:
	RadioAudioProcessor();
	void Configure(std::uint32_t sampleRate, const RadioProcessingSettings& settings);
	void Reset();
	void Process(std::vector<float>& audio);
private:
	RadioProcessingSettings settings_;
	float rate_, deAlpha_, hpAlpha_, lpAlpha_, dcAlpha_;
	float deState_, hpState_, lpState_, dcState_, envelope_, noiseGain_, agcGain_;
	float envelopeAlpha_, noiseAttack_, noiseRelease_, agcAttack_, agcRelease_;
	float noiseThreshold_, noiseFloorGain_, manualGain_, agcTarget_, agcMaxGain_, ceiling_;
};

// RF squelch uses pre-demodulation IQ power in dBFS, not the audio score.
// Update once per IQ pair. Hysteresis/hang prevent chatter; a closed gate
// emits silence rather than removing samples and changing decoder time.
class RadioSquelch
{
public:
	RadioSquelch();
	void Configure(std::uint32_t sampleRate, const RadioProcessingSettings& settings);
	void Reset();
	bool Observe(float i, float q);
private:
	bool enabled_, open_;
	float power_, alpha_, openPower_, closePower_;
	std::uint64_t hangSamples_, remaining_;
};

} }
#endif
