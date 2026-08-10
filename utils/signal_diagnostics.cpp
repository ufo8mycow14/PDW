#include "signal_diagnostics.h"

#include "audio_signal_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pdw
{
namespace signal
{

namespace
{
	float Clamp01(float value)
	{
		return (std::max)(0.0f, (std::min)(1.0f, value));
	}

	const int LEGACY_THRESHOLDS[10] = {0, 1, 2, 5, 9, 14, 17, 24, 30, 44};
	const int LEGACY_OFFSETS[10] = {0, 1, -1, 2, -2, 3, -3, 4, -4, 5};
}

SignalMetrics::SignalMetrics()
	: rmsLevel(0.0f), peakLevel(0.0f), dcOffset(0.0f), noiseLevel(0.0f),
	  clippingPercent(0.0f), eyeOpening(0.0f), signalQuality(0.0f),
	  multipathRisk(0.0f), sampleCount(0), correctedErrors(0),
	  uncorrectableErrors(0), lastCycle(-1), lastFrame(-1)
{
	std::memset(phaseErrors, 0, sizeof(phaseErrors));
}

CalibrationResult::CalibrationResult()
	: thresholdIndex(0), centeringIndex(0), resyncIndex(0), score(0.0f),
	  measuredDcOffset(0.0f), measuredNoise(0.0f), clippingDetected(false)
{
}

SignalDiagnostics::SignalDiagnostics()
	: waveform_(512, 0.0f), waveformPosition_(0), waveformWrapped_(false)
{
	InitializeCriticalSection(&lock_);
}

SignalDiagnostics::~SignalDiagnostics()
{
	DeleteCriticalSection(&lock_);
}

void SignalDiagnostics::Reset()
{
	EnterCriticalSection(&lock_);
	metrics_ = SignalMetrics();
	std::fill(waveform_.begin(), waveform_.end(), 0.0f);
	waveformPosition_ = 0;
	waveformWrapped_ = false;
	qualityHistory_.clear();
	LeaveCriticalSection(&lock_);
}

void SignalDiagnostics::Observe(const float* samples, std::size_t sampleCount)
{
	if (!samples || sampleCount == 0) return;
	double sum = 0.0, squares = 0.0, differenceSquares = 0.0;
	float peak = 0.0f;
	std::size_t clipped = 0;
	for (std::size_t index = 0; index < sampleCount; ++index)
	{
		const float value = ClampNormalized(samples[index]);
		sum += value;
		squares += static_cast<double>(value) * value;
		peak = (std::max)(peak, std::fabs(value));
		if (std::fabs(value) >= 0.985f) clipped++;
		if (index) differenceSquares += static_cast<double>(value - samples[index - 1]) *
			(value - samples[index - 1]);
	}
	const float dc = static_cast<float>(sum / sampleCount);
	const float rms = static_cast<float>(std::sqrt((std::max)(0.0,
		squares / sampleCount - static_cast<double>(dc) * dc)));
	const float noise = sampleCount > 1 ? static_cast<float>(
		std::sqrt(differenceSquares / (2.0 * (sampleCount - 1)))) : 0.0f;
	const float clipping = static_cast<float>(clipped * 100.0 / sampleCount);
	const float eye = rms > 0.001f ? Clamp01((rms - noise * 0.65f) / rms) : 0.0f;
	const float levelScore = Clamp01((rms - 0.025f) / 0.30f);
	const float clippingScore = 1.0f - Clamp01(clipping / 2.0f);
	const float quality = 100.0f * Clamp01(levelScore * 0.35f + eye * 0.50f + clippingScore * 0.15f);
	const float multipath = 100.0f * Clamp01((noise / ((std::max)(rms, 0.01f)) - 0.45f) / 0.9f);

	EnterCriticalSection(&lock_);
	const float blend = metrics_.sampleCount ? 0.20f : 1.0f;
	metrics_.rmsLevel += blend * (rms - metrics_.rmsLevel);
	metrics_.peakLevel += blend * (peak - metrics_.peakLevel);
	metrics_.dcOffset += blend * (dc - metrics_.dcOffset);
	metrics_.noiseLevel += blend * (noise - metrics_.noiseLevel);
	metrics_.clippingPercent += blend * (clipping - metrics_.clippingPercent);
	metrics_.eyeOpening += blend * (eye * 100.0f - metrics_.eyeOpening);
	metrics_.signalQuality += blend * (quality - metrics_.signalQuality);
	metrics_.multipathRisk += blend * (multipath - metrics_.multipathRisk);
	metrics_.sampleCount += sampleCount;
	const std::size_t stride = (std::max)(static_cast<std::size_t>(1), sampleCount / 128);
	for (std::size_t index = 0; index < sampleCount; index += stride)
	{
		waveform_[waveformPosition_] = ClampNormalized(samples[index]);
		waveformPosition_ = (waveformPosition_ + 1) % waveform_.size();
		if (waveformPosition_ == 0) waveformWrapped_ = true;
	}
	LeaveCriticalSection(&lock_);
}

void SignalDiagnostics::RecordDecodeResult(int errors, int flexPhase, int cycle,
	int frame, float decoderQuality)
{
	EnterCriticalSection(&lock_);
	if (errors >= 3) metrics_.uncorrectableErrors++;
	else if (errors > 0) metrics_.correctedErrors += static_cast<std::uint64_t>(errors);
	if (errors > 0 && flexPhase >= 0 && flexPhase < 4)
		metrics_.phaseErrors[flexPhase] += static_cast<std::uint64_t>(errors);
	metrics_.lastCycle = cycle;
	metrics_.lastFrame = frame;
	if (decoderQuality >= 0.0f)
	{
		qualityHistory_.push_back((std::max)(0.0f, (std::min)(100.0f, decoderQuality)));
		if (qualityHistory_.size() > 180) qualityHistory_.erase(qualityHistory_.begin());
	}
	LeaveCriticalSection(&lock_);
}

SignalMetrics SignalDiagnostics::Snapshot(std::vector<float>* waveform,
	std::vector<float>* qualityHistory) const
{
	EnterCriticalSection(&lock_);
	const SignalMetrics copy = metrics_;
	if (waveform)
	{
		waveform->clear();
		if (waveformWrapped_)
		{
			waveform->insert(waveform->end(), waveform_.begin() + waveformPosition_, waveform_.end());
			waveform->insert(waveform->end(), waveform_.begin(), waveform_.begin() + waveformPosition_);
		}
		else waveform->insert(waveform->end(), waveform_.begin(), waveform_.begin() + waveformPosition_);
	}
	if (qualityHistory) *qualityHistory = qualityHistory_;
	LeaveCriticalSection(&lock_);
	return copy;
}

CalibrationResult CalibrateLegacySlicer(const std::vector<float>& samples)
{
	CalibrationResult best;
	if (samples.size() < 256) return best;
	const std::size_t stride = (std::max)(static_cast<std::size_t>(1), samples.size() / 50000);
	double sum = 0.0, differenceSquares = 0.0;
	float peak = 0.0f;
	std::size_t count = 0, clipped = 0;
	float previous = samples[0];
	for (std::size_t index = 0; index < samples.size(); index += stride)
	{
		const float value = ClampNormalized(samples[index]);
		sum += value;
		peak = (std::max)(peak, std::fabs(value));
		if (std::fabs(value) >= 0.985f) clipped++;
		if (count) differenceSquares += static_cast<double>(value - previous) * (value - previous);
		previous = value;
		count++;
	}
	const float dc = static_cast<float>(sum / count);
	const float noise = count > 1 ? static_cast<float>(std::sqrt(
		differenceSquares / (2.0 * (count - 1)))) : 0.0f;
	best.measuredDcOffset = dc;
	best.measuredNoise = noise;
	best.clippingDetected = clipped * 100 > count;

	float bestScore = -1.0f;
	for (int thresholdIndex = 0; thresholdIndex < 10; ++thresholdIndex)
	{
		const float threshold = LEGACY_THRESHOLDS[thresholdIndex] / 128.0f;
		for (int centeringIndex = 0; centeringIndex < 10; ++centeringIndex)
		{
			const float center = dc + LEGACY_OFFSETS[centeringIndex] / 128.0f;
			std::size_t positive = 0, negative = 0, ambiguous = 0, transitions = 0;
			int lastSign = 0;
			for (std::size_t index = 0; index < samples.size(); index += stride)
			{
				const float centered = samples[index] - center;
				int sign = 0;
				if (centered > threshold) { sign = 1; positive++; }
				else if (centered < -threshold) { sign = -1; negative++; }
				else ambiguous++;
				if (sign && lastSign && sign != lastSign) transitions++;
				if (sign) lastSign = sign;
			}
			const float confident = static_cast<float>(positive + negative) / count;
			const float balance = (positive + negative) ? 1.0f -
				std::fabs(static_cast<float>(positive) - static_cast<float>(negative)) /
				(static_cast<float>(positive + negative)) : 0.0f;
			const float transitionDensity = static_cast<float>(transitions) / count;
			const float noiseMargin = Clamp01((threshold - noise * 0.35f + 0.02f) / 0.12f);
			for (int resyncIndex = 0; resyncIndex < 10; ++resyncIndex)
			{
				const float resyncPenalty = std::fabs(static_cast<float>(LEGACY_OFFSETS[resyncIndex])) *
					(0.006f + transitionDensity * 0.01f);
				const float score = confident * 0.42f + balance * 0.22f + noiseMargin * 0.24f +
					Clamp01(transitionDensity * 8.0f) * 0.12f - resyncPenalty -
					static_cast<float>(ambiguous) / count * 0.08f;
				if (score > bestScore)
				{
					bestScore = score;
					best.thresholdIndex = thresholdIndex;
					best.centeringIndex = centeringIndex;
					best.resyncIndex = resyncIndex;
				}
			}
		}
	}
	best.score = Clamp01(bestScore) * 100.0f;
	return best;
}

} // namespace signal
} // namespace pdw
