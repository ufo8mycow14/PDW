#include "signal_diagnostics.h"

#include <algorithm>
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
}

int main()
{
	using namespace pdw::signal;
	std::vector<float> clean(48000);
	for (std::size_t index = 0; index < clean.size(); ++index)
		clean[index] = ((index / 15) & 1) ? 0.62f : -0.54f;

	SignalDiagnostics diagnostics;
	diagnostics.Observe(&clean[0], clean.size());
	diagnostics.RecordDecodeResult(1, true, 0, 4, 20, 98.0f);
	diagnostics.RecordDecodeResult(2, true, -1, 4, 20, -1.0f);
	diagnostics.RecordDecodeResult(3, true, 3, 4, 21, 94.0f);
	diagnostics.RecordDecodeResult(1, false, 0, 4, 21, -1.0f);
	diagnostics.RecordDecodeResult(10, false, 3, 4, 21, -1.0f);
	diagnostics.RecordDecodeResult(0, false, 2, 4, 21, -1.0f);
	std::vector<float> waveform, history, spectrum, waterfall;
	const SignalMetrics metrics = diagnostics.Snapshot(&waveform, &history, &spectrum, &waterfall);
	Expect(metrics.rmsLevel > 0.5f, "RMS level measured");
	Expect(metrics.clippingPercent == 0.0f, "clean signal is not clipped");
	Expect(metrics.correctedBchBits == 3 && metrics.correctedBchWords == 2, "two repaired words, three corrected bits");
	Expect(metrics.uncorrectableBchWords == 1 && metrics.otherFlaggedChecks == 2,
		"protocol penalties must not masquerade as BCH results");
	Expect(metrics.phaseFlaggedWords[0] == 1 && metrics.phaseFlaggedWords[3] == 1,
		"count affected phase words, not penalty weights");
	Expect(!waveform.empty() && waveform.size() <= 512, "waveform history bounded");
	Expect(history.size() == 2, "quality history recorded");
	Expect(spectrum.size() == 64, "spectrum has bounded frequency bins");
	Expect(!waterfall.empty() && waterfall.size() % 64 == 0 && waterfall.size() <= 64 * 32,
		"waterfall has bounded rows");
	Expect(metrics.sampleCount == clean.size(), "all observed samples counted, including partial window");
	for (std::size_t chunk : {std::size_t(1), std::size_t(23), std::size_t(997),
		std::size_t(4096), std::size_t(32768)})
	{
		SignalDiagnostics split;
		for (std::size_t start = 0; start < clean.size(); start += chunk)
			split.Observe(clean.data() + start, (std::min)(chunk, clean.size() - start));
		std::vector<float> splitWave, splitSpectrum, splitWaterfall;
		const SignalMetrics splitMetrics = split.Snapshot(&splitWave, NULL, &splitSpectrum, &splitWaterfall);
		Expect(splitMetrics.sampleCount == metrics.sampleCount, "chunk-independent sample count");
		Expect(splitMetrics.signalQuality == metrics.signalQuality &&
			splitMetrics.rmsLevel == metrics.rmsLevel && splitMetrics.noiseLevel == metrics.noiseLevel &&
			splitMetrics.eyeOpening == metrics.eyeOpening && splitMetrics.dcOffset == metrics.dcOffset,
			"identical samples must produce identical audio metrics at every callback size");
		Expect(splitWave == waveform && splitSpectrum == spectrum && splitWaterfall == waterfall,
			"waveform and spectrum must not depend on callback size");
		split.Reset();
		Expect(split.Snapshot().sampleCount == 0 && split.Snapshot().signalQuality == 0,
			"reset discards measured and pending windows");
		split.Observe(clean.data(), clean.size());
		Expect(split.Snapshot().signalQuality == metrics.signalQuality, "reset replay reproduces score");
	}

	const CalibrationResult result = CalibrateLegacySlicer(clean);
	Expect(result.thresholdIndex >= 0 && result.thresholdIndex <= 9,
		"calibration threshold range");
	Expect(result.centeringIndex >= 0 && result.centeringIndex <= 9,
		"calibration centering range");
	Expect(result.resyncIndex >= 0 && result.resyncIndex <= 9,
		"calibration resync range");
	Expect(result.score > 50.0f, "clean recording produces useful calibration score");
	Expect(std::fabs(result.measuredDcOffset - 0.04f) < 0.02f,
		"calibration measures DC offset");

	std::cout << "Signal diagnostics tests passed\n";
	return 0;
}
