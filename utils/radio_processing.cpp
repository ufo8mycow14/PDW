#include "radio_processing.h"
#include "audio_signal_core.h"
#include <algorithm>
#include <cmath>

namespace pdw { namespace signal {
namespace {
const RadioSetting fields[] = {
	{ "RtlDemodulation", "Demodulation", &RadioProcessingSettings::demodulation, 0, 2, 0 },
	{ "RtlWfmCutoffHz", "WFM cutoff", &RadioProcessingSettings::wfmCutoffHz, 50000, 100000, 100000 },
	{ "RtlAmCutoffHz", "AM cutoff", &RadioProcessingSettings::amCutoffHz, 2000, 15000, 6000 },
	{ "RtlSquelch", "Squelch", &RadioProcessingSettings::squelchEnabled, 0, 1, 0 },
	{ "RtlSquelchThresholdDbfs", "Squelch threshold", &RadioProcessingSettings::squelchThresholdDbfs, -100, 0, -50 },
	{ "RtlSquelchHysteresisDb", "Squelch hysteresis", &RadioProcessingSettings::squelchHysteresisDb, 0, 20, 3 },
	{ "RtlSquelchHangMs", "Squelch hang", &RadioProcessingSettings::squelchHangMs, 0, 2000, 150 },
	{ "RtlDeEmphasis", "De-emphasis", &RadioProcessingSettings::deEmphasis, 0, 2, 0 },
	{ "RtlSpeechFilter", "Speech filter", &RadioProcessingSettings::speechEnabled, 0, 1, 0 },
	{ "RtlSpeechHighPassHz", "Speech high-pass", &RadioProcessingSettings::speechHighPassHz, 50, 2000, 300 },
	{ "RtlSpeechLowPassHz", "Speech low-pass", &RadioProcessingSettings::speechLowPassHz, 1000, 10000, 3000 },
	{ "RtlNoiseReduction", "Noise reduction", &RadioProcessingSettings::noiseReductionEnabled, 0, 1, 0 },
	{ "RtlNoiseThresholdDbfs", "Noise threshold", &RadioProcessingSettings::noiseThresholdDbfs, -100, 0, -45 },
	{ "RtlNoiseReductionDb", "Noise reduction", &RadioProcessingSettings::noiseReductionDb, 1, 60, 12 },
	{ "RtlNoiseAttackMs", "Noise attack", &RadioProcessingSettings::noiseAttackMs, 1, 200, 5 },
	{ "RtlNoiseReleaseMs", "Noise release", &RadioProcessingSettings::noiseReleaseMs, 10, 2000, 100 },
	{ "RtlDcBlock", "DC removal", &RadioProcessingSettings::dcBlockEnabled, 0, 1, 0 },
	{ "RtlAudioGainEnabled", "Audio gain", &RadioProcessingSettings::audioGainEnabled, 0, 1, 0 },
	{ "RtlAudioGainDb", "Audio gain", &RadioProcessingSettings::audioGainDb, -24, 24, 0 },
	{ "RtlAudioAgc", "Audio AGC", &RadioProcessingSettings::agcEnabled, 0, 1, 0 },
	{ "RtlAudioAgcTargetDbfs", "AGC target", &RadioProcessingSettings::agcTargetDbfs, -30, -3, -12 },
	{ "RtlAudioAgcMaxGainDb", "AGC maximum gain", &RadioProcessingSettings::agcMaxGainDb, 0, 40, 20 },
	{ "RtlAudioAgcAttackMs", "AGC attack", &RadioProcessingSettings::agcAttackMs, 1, 200, 10 },
	{ "RtlAudioAgcReleaseMs", "AGC release", &RadioProcessingSettings::agcReleaseMs, 10, 2000, 250 },
	{ "RtlAudioLimiter", "Audio limiter", &RadioProcessingSettings::limiterEnabled, 0, 1, 0 },
	{ "RtlAudioLimiterCeilingDbfs", "Limiter ceiling", &RadioProcessingSettings::limiterCeilingDbfs, -12, 0, -1 },
};
float Db(float db) { return std::pow(10.0f, db / 20.0f); }
float TimeAlpha(float rate, int ms) { return 1.0f - std::exp(-1000.0f / (rate * ms)); }
float FilterAlpha(float rate, float hz) { return 1.0f - std::exp(-6.28318530718f * hz / rate); }
}

const RadioSetting* RadioSettings(std::size_t& count)
{
	count = sizeof(fields) / sizeof(fields[0]);
	return fields;
}
RadioProcessingSettings DefaultRadioProcessingSettings()
{
	RadioProcessingSettings result = {};
	for (const auto& field : fields) result.*(field.member) = field.defaultValue;
	return result;
}
void NormalizeRadioProcessingSettings(RadioProcessingSettings& settings)
{
	for (const auto& field : fields)
	{
		int& value = settings.*(field.member);
		if (value < field.minimum || value > field.maximum) value = field.defaultValue;
	}
	if (settings.speechHighPassHz >= settings.speechLowPassHz)
	{
		settings.speechHighPassHz = 300;
		settings.speechLowPassHz = 3000;
	}
}
bool ValidateRadioProcessingSettings(const RadioProcessingSettings& settings,
	std::uint32_t audioRate, std::string& error)
{
	for (const auto& field : fields)
	{
		const int value = settings.*(field.member);
		if (value < field.minimum || value > field.maximum)
		{
			error = std::string(field.label) + " is outside its supported range.";
			return false;
		}
	}
	if (settings.speechEnabled && (settings.speechHighPassHz >= settings.speechLowPassHz ||
		static_cast<std::uint32_t>(settings.speechLowPassHz) * 2 >= audioRate))
	{
		error = "Speech low-pass must be above the high-pass and below half the audio sample rate.";
		return false;
	}
	if (settings.demodulation == RADIO_WFM && audioRate < 32000)
	{
		error = "WFM mono needs an audio sample rate of at least 32000 Hz.";
		return false;
	}
	error.clear();
	return true;
}
const char* RadioDemodulationName(int mode)
{
	return mode == RADIO_WFM ? "WFM (mono)" : mode == RADIO_AM ? "AM" : "NFM";
}
bool RadioAudioProcessingEnabled(const RadioProcessingSettings& s)
{
	return s.deEmphasis || s.speechEnabled || s.noiseReductionEnabled ||
		s.dcBlockEnabled || s.audioGainEnabled || s.agcEnabled || s.limiterEnabled;
}
RadioAudioProcessor::RadioAudioProcessor() { Configure(48000, DefaultRadioProcessingSettings()); }
void RadioAudioProcessor::Configure(std::uint32_t rate, const RadioProcessingSettings& s)
{
	settings_ = s;
	NormalizeRadioProcessingSettings(settings_);
	rate_ = static_cast<float>(rate ? rate : 48000);
	const float tau = settings_.deEmphasis == 1 ? 50.0e-6f : 75.0e-6f;
	deAlpha_ = 1.0f - std::exp(-1.0f / (rate_ * tau));
	hpAlpha_ = FilterAlpha(rate_, static_cast<float>(settings_.speechHighPassHz));
	lpAlpha_ = FilterAlpha(rate_, (std::min)(static_cast<float>(settings_.speechLowPassHz), rate_ * 0.45f));
	dcAlpha_ = FilterAlpha(rate_, 20.0f);
	envelopeAlpha_ = TimeAlpha(rate_, 5);
	noiseAttack_ = TimeAlpha(rate_, settings_.noiseAttackMs);
	noiseRelease_ = TimeAlpha(rate_, settings_.noiseReleaseMs);
	agcAttack_ = TimeAlpha(rate_, settings_.agcAttackMs);
	agcRelease_ = TimeAlpha(rate_, settings_.agcReleaseMs);
	noiseThreshold_ = Db(static_cast<float>(settings_.noiseThresholdDbfs));
	noiseFloorGain_ = Db(-static_cast<float>(settings_.noiseReductionDb));
	manualGain_ = Db(static_cast<float>(settings_.audioGainDb));
	agcTarget_ = Db(static_cast<float>(settings_.agcTargetDbfs));
	agcMaxGain_ = Db(static_cast<float>(settings_.agcMaxGainDb));
	ceiling_ = Db(static_cast<float>(settings_.limiterCeilingDbfs));
	Reset();
}
void RadioAudioProcessor::Reset()
{
	deState_ = hpState_ = lpState_ = dcState_ = envelope_ = 0.0f;
	noiseGain_ = agcGain_ = 1.0f;
}
void RadioAudioProcessor::Process(std::vector<float>& audio)
{
	if (!RadioAudioProcessingEnabled(settings_)) return;
	for (float& sample : audio)
	{
		float value = std::isfinite(sample) ? sample : 0.0f;
		if (settings_.dcBlockEnabled) { dcState_ += dcAlpha_ * (value - dcState_); value -= dcState_; }
		if (settings_.deEmphasis) { deState_ += deAlpha_ * (value - deState_); value = deState_; }
		if (settings_.speechEnabled)
		{
			hpState_ += hpAlpha_ * (value - hpState_);
			value -= hpState_;
			lpState_ += lpAlpha_ * (value - lpState_);
			value = lpState_;
		}
		envelope_ += envelopeAlpha_ * (value * value - envelope_);
		const float rms = std::sqrt((std::max)(0.0f, envelope_));
		if (settings_.noiseReductionEnabled)
		{
			// Soft downward gate. It attenuates quiet passages, not in-band
			// noise during speech, and is deliberately labelled as such in UI.
			const float ratio = (std::min)(1.0f, rms / noiseThreshold_);
			const float target = noiseFloorGain_ + (1.0f - noiseFloorGain_) * ratio * ratio;
			noiseGain_ += (target > noiseGain_ ? noiseAttack_ : noiseRelease_) * (target - noiseGain_);
			value *= noiseGain_;
		}
		if (settings_.agcEnabled)
		{
			const float target = (std::min)(agcMaxGain_, agcTarget_ / (std::max)(rms, 1.0e-6f));
			agcGain_ += (target < agcGain_ ? agcAttack_ : agcRelease_) * (target - agcGain_);
			value *= agcGain_;
		}
		if (settings_.audioGainEnabled) value *= manualGain_;
		if (settings_.limiterEnabled) value = (std::max)(-ceiling_, (std::min)(ceiling_, value));
		sample = ClampNormalized(value);
	}
}
RadioSquelch::RadioSquelch() { Configure(1024000, DefaultRadioProcessingSettings()); }
void RadioSquelch::Configure(std::uint32_t rate, const RadioProcessingSettings& s)
{
	enabled_ = s.squelchEnabled != 0;
	alpha_ = TimeAlpha(static_cast<float>(rate ? rate : 1024000), 2);
	openPower_ = Db(static_cast<float>(s.squelchThresholdDbfs));
	openPower_ *= openPower_;
	closePower_ = Db(static_cast<float>(s.squelchThresholdDbfs - s.squelchHysteresisDb));
	closePower_ *= closePower_;
	hangSamples_ = static_cast<std::uint64_t>(rate) * s.squelchHangMs / 1000;
	Reset();
}
void RadioSquelch::Reset() { open_ = !enabled_; power_ = 0.0f; remaining_ = 0; }
bool RadioSquelch::Observe(float i, float q)
{
	if (!enabled_) return true;
	power_ += alpha_ * (i * i + q * q - power_);
	if (!open_ && power_ >= openPower_) open_ = true;
	if (open_)
	{
		if (power_ >= closePower_) remaining_ = hangSamples_;
		else if (remaining_) --remaining_;
		else open_ = false;
	}
	return open_;
}
} }
