#include "rtl_tcp_source.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace pdw::signal;
namespace {
const double PI = 3.14159265358979323846;
void Check(bool ok, const char* message)
{
	if (!ok) { std::cerr << message << "\n"; std::exit(1); }
}
std::vector<float> Tone(double hz, float amplitude, unsigned int rate = 48000)
{
	std::vector<float> result(rate);
	for (std::size_t n = 0; n < result.size(); ++n)
		result[n] = amplitude * static_cast<float>(std::sin(2 * PI * hz * n / rate));
	return result;
}
double Rms(const std::vector<float>& values)
{
	double sum = 0;
	for (std::size_t n = values.size() / 2; n < values.size(); ++n) sum += values[n] * values[n];
	return std::sqrt(sum / (values.size() - values.size() / 2));
}
double FilteredLevel(const RadioProcessingSettings& settings, double frequency)
{
	auto audio = Tone(frequency, 0.2f);
	RadioAudioProcessor processor;
	processor.Configure(48000, settings);
	processor.Process(audio);
	return Rms(audio);
}
std::vector<unsigned char> IqTone(int mode)
{
	const unsigned int rate = 240000;
	std::vector<unsigned char> result(rate);
	double phase = 0;
	for (std::size_t n = 0; n < result.size() / 2; ++n)
	{
		const double modulation = std::sin(2 * PI * 1000 * n / rate);
		phase += 2 * PI * (mode == RADIO_AM ? 1000 : modulation * (mode == RADIO_WFM ? 50000 : 2500)) / rate;
		const double amplitude = mode == RADIO_AM ? 0.5 + 0.25 * modulation : 0.7;
		result[n * 2] = static_cast<unsigned char>(127.5 + 120 * amplitude * std::cos(phase));
		result[n * 2 + 1] = static_cast<unsigned char>(127.5 + 120 * amplitude * std::sin(phase));
	}
	return result;
}
double ToneFraction(const std::vector<float>& audio)
{
	double sine = 0, cosine = 0, power = 0;
	const std::size_t start = audio.size() / 2;
	for (std::size_t n = start; n < audio.size(); ++n)
	{
		sine += audio[n] * std::sin(2 * PI * 1000 * n / 48000);
		cosine += audio[n] * std::cos(2 * PI * 1000 * n / 48000);
		power += audio[n] * audio[n];
	}
	return 2 * (sine * sine + cosine * cosine) / ((audio.size() - start) * power);
}
}
int main()
{
	auto settings = DefaultRadioProcessingSettings();
	Check(RtlTcpConfig().nfmBandwidthHz == 12500, "Receiver default must be 12500 Hz");
	Check(settings.demodulation == RADIO_NFM && !settings.squelchEnabled &&
		!RadioAudioProcessingEnabled(settings), "Defaults must be NFM and all effects off");
	auto audio = Tone(1000, 0.3f);
	const auto original = audio;
	RadioAudioProcessor processor;
	processor.Process(audio);
	Check(audio == original, "All-off must be an exact audio bypass");
	std::string error;
	settings.speechEnabled = 1;
	settings.speechHighPassHz = 2000;
	settings.speechLowPassHz = 1000;
	Check(!ValidateRadioProcessingSettings(settings, 48000, error), "Reject inverted speech band");
	settings.speechLowPassHz = 10000;
	Check(!ValidateRadioProcessingSettings(settings, 8000, error), "Reject speech filter above Nyquist");
	settings = DefaultRadioProcessingSettings();
	settings.demodulation = RADIO_WFM;
	Check(!ValidateRadioProcessingSettings(settings, 8000, error), "Reject undersampled WFM audio");
	settings.demodulation = 99;
	NormalizeRadioProcessingSettings(settings);
	Check(settings.demodulation == RADIO_NFM, "Unknown saved mode falls back to NFM");
	settings = DefaultRadioProcessingSettings();
	settings.deEmphasis = 1;
	Check(FilteredLevel(settings, 10000) < FilteredLevel(settings, 1000) * 0.5, "50 us de-emphasis attenuates high frequencies");
	const double emphasis50 = FilteredLevel(settings, 10000);
	settings.deEmphasis = 2;
	Check(FilteredLevel(settings, 10000) < emphasis50, "75 us de-emphasis has the stronger rolloff");
	settings = DefaultRadioProcessingSettings();
	settings.speechEnabled = 1;
	const double mid = FilteredLevel(settings, 1000);
	Check(FilteredLevel(settings, 50) < mid * 0.25 && FilteredLevel(settings, 12000) < mid * 0.4,
		"Speech filter removes bass and high-frequency energy");
	settings = DefaultRadioProcessingSettings();
	settings.dcBlockEnabled = 1;
	audio.assign(48000, 0.4f);
	processor.Configure(48000, settings); processor.Process(audio);
	Check(Rms(audio) < 0.0001, "DC removal settles on constant input");
	settings = DefaultRadioProcessingSettings();
	settings.audioGainEnabled = 1; settings.audioGainDb = 6;
	Check(std::fabs(FilteredLevel(settings, 1000) / (0.2 / std::sqrt(2.0)) - std::pow(10.0, 6.0 / 20)) < 0.001,
		"Manual audio gain applies the selected dB value");
	settings.limiterEnabled = 1; settings.limiterCeilingDbfs = -6;
	audio = Tone(1000, 0.8f);
	processor.Configure(48000, settings); processor.Process(audio);
	for (float sample : audio) Check(std::fabs(sample) <= std::pow(10.0f, -6.0f / 20.0f) + 1e-6f, "Limiter ceiling");
	settings = DefaultRadioProcessingSettings();
	settings.noiseReductionEnabled = 1;
	audio = Tone(1000, 0.00001f);
	processor.Configure(48000, settings); processor.Process(audio);
	Check(Rms(audio) < 0.000003, "Noise gate attenuates quiet passages");
	audio = Tone(1000, 0.2f);
	processor.Configure(48000, settings); processor.Process(audio);
	Check(Rms(audio) > 0.13, "Noise gate opens for signal above threshold");
	settings = DefaultRadioProcessingSettings();
	settings.agcEnabled = 1;
	audio.assign(96000, 0.05f);
	processor.Configure(48000, settings); processor.Process(audio);
	Check(std::fabs(Rms(audio) - std::pow(10.0, -12.0 / 20)) < 0.015, "Audio AGC reaches its target");
	settings.agcMaxGainDb = 0;
	audio.assign(48000, 0.01f);
	processor.Configure(48000, settings); processor.Process(audio);
	Check(Rms(audio) <= 0.01001, "Audio AGC respects maximum gain");
	settings = DefaultRadioProcessingSettings();
	settings.squelchEnabled = 1; settings.squelchThresholdDbfs = -20;
	settings.squelchHangMs = 100; settings.squelchHysteresisDb = 6;
	RadioSquelch squelch; squelch.Configure(48000, settings);
	bool open = true;
	for (int n = 0; n < 4800; ++n) open = squelch.Observe(0.001f, 0);
	Check(!open, "RF squelch stays closed below threshold");
	for (int n = 0; n < 4800; ++n) open = squelch.Observe(0.2f, 0);
	Check(open, "RF squelch opens above threshold");
	for (int n = 0; n < 9600; ++n) open = squelch.Observe(0.075f, 0);
	Check(open, "RF squelch hysteresis holds between thresholds");
	for (int n = 0; n < 2400; ++n) open = squelch.Observe(0, 0);
	Check(open, "RF squelch hang retains a short gap");
	for (int n = 0; n < 9600; ++n) open = squelch.Observe(0, 0);
	Check(!open, "RF squelch closes after hang");
	for (int mode = RADIO_NFM; mode <= RADIO_AM; ++mode)
	{
		const auto iq = IqTone(mode);
		for (int enhanced = 0; enhanced <= 1; ++enhanced)
		{
			settings = DefaultRadioProcessingSettings(); settings.demodulation = mode;
			RtlFmDemodulator whole(240000, 48000, 12000, enhanced != 0, settings);
			std::vector<float> result; whole.ProcessUnsignedIq(iq.data(), iq.size(), result);
			Check(result.size() > 23000 && result.size() < 24001, "Demodulator preserves duration");
			Check(ToneFraction(result) > 0.95, "Demodulator recovers synthetic modulating tone");
			RtlFmDemodulator chunked(240000, 48000, 12000, enhanced != 0, settings);
			std::vector<float> combined, part;
			for (std::size_t n = 0; n < iq.size();)
			{
				const std::size_t count = (std::min)(std::size_t(997), iq.size() - n);
				chunked.ProcessUnsignedIq(iq.data() + n, count, part);
				combined.insert(combined.end(), part.begin(), part.end()); n += count;
			}
			Check(combined == result, "Odd IQ block boundaries must not change demodulation");
			whole.Reset(); whole.ProcessUnsignedIq(iq.data(), iq.size(), part);
			Check(part == result, "Reset must clear demodulator and processor state");
			settings.squelchEnabled = 1; settings.squelchThresholdDbfs = 0;
			RtlFmDemodulator muted(240000, 48000, 12000, enhanced != 0, settings);
			muted.ProcessUnsignedIq(iq.data(), iq.size(), part);
			Check(part.size() == result.size() && Rms(part) == 0, "Closed RF squelch emits silence without losing time");
		}
	}
	settings = DefaultRadioProcessingSettings();
	settings.deEmphasis = 1; settings.noiseReductionEnabled = 1; settings.speechEnabled = 1;
	settings.dcBlockEnabled = 1; settings.agcEnabled = 1; settings.audioGainEnabled = 1;
	settings.limiterEnabled = 1;
	processor.Configure(48000, settings);
	audio = original; processor.Process(audio);
	RadioAudioProcessor pieces; pieces.Configure(48000, settings);
	std::vector<float> combined;
	for (std::size_t n = 0; n < original.size(); n += 137)
	{
		std::vector<float> part(original.begin() + n, original.begin() + (std::min)(n + 137, original.size()));
		pieces.Process(part); combined.insert(combined.end(), part.begin(), part.end());
	}
	Check(combined == audio, "Audio effects must be independent of callback size");
	std::cout << "Radio processing: defaults, filters, gain, gate, squelch, AM/FM and streaming checks passed\n";
}
