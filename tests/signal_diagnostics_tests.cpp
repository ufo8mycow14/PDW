#include "signal_diagnostics.h"

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
	diagnostics.RecordDecodeResult(1, 0, 4, 20, 98.0f);
	diagnostics.RecordDecodeResult(3, 3, 4, 21, 94.0f);
	std::vector<float> waveform, history, spectrum, waterfall;
	const SignalMetrics metrics = diagnostics.Snapshot(&waveform, &history, &spectrum, &waterfall);
	Expect(metrics.rmsLevel > 0.5f, "RMS level measured");
	Expect(metrics.clippingPercent == 0.0f, "clean signal is not clipped");
	Expect(metrics.correctedErrors == 1, "corrected errors tracked");
	Expect(metrics.uncorrectableErrors == 1, "uncorrectable errors tracked");
	Expect(metrics.phaseErrors[0] == 1 && metrics.phaseErrors[3] == 3,
		"FLEX phase errors tracked");
	Expect(!waveform.empty() && waveform.size() <= 512, "waveform history bounded");
	Expect(history.size() == 2, "quality history recorded");
	Expect(spectrum.size() == 64, "spectrum has bounded frequency bins");
	Expect(!waterfall.empty() && waterfall.size() % 64 == 0 && waterfall.size() <= 64 * 32,
		"waterfall has bounded rows");

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
