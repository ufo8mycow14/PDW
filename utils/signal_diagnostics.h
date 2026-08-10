#ifndef PDW_SIGNAL_DIAGNOSTICS_H
#define PDW_SIGNAL_DIAGNOSTICS_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pdw
{
namespace signal
{

struct SignalMetrics
{
	float rmsLevel;
	float peakLevel;
	float dcOffset;
	float noiseLevel;
	float clippingPercent;
	float eyeOpening;
	float signalQuality;
	float multipathRisk;
	std::uint64_t sampleCount;
	std::uint64_t correctedErrors;
	std::uint64_t uncorrectableErrors;
	std::uint64_t phaseErrors[4];
	int lastCycle;
	int lastFrame;

	SignalMetrics();
};

struct CalibrationResult
{
	int thresholdIndex;
	int centeringIndex;
	int resyncIndex;
	float score;
	float measuredDcOffset;
	float measuredNoise;
	bool clippingDetected;

	CalibrationResult();
};

class SignalDiagnostics
{
public:
	SignalDiagnostics();
	~SignalDiagnostics();

	void Reset();
	void Observe(const float* samples, std::size_t sampleCount);
	void RecordDecodeResult(int errors, int flexPhase, int cycle, int frame,
		float decoderQuality);
	SignalMetrics Snapshot(std::vector<float>* waveform = NULL,
		std::vector<float>* qualityHistory = NULL) const;

private:
	SignalDiagnostics(const SignalDiagnostics&);
	SignalDiagnostics& operator=(const SignalDiagnostics&);

	mutable CRITICAL_SECTION lock_;
	SignalMetrics metrics_;
	std::vector<float> waveform_;
	std::size_t waveformPosition_;
	bool waveformWrapped_;
	std::vector<float> qualityHistory_;
};

// Evaluates all 1,000 legacy custom threshold/centering/resync combinations
// against a recording. It is signal-based and does not replace protocol-level
// A/B validation; the user explicitly chooses whether to apply the suggestion.
CalibrationResult CalibrateLegacySlicer(const std::vector<float>& samples);

} // namespace signal
} // namespace pdw

#endif
