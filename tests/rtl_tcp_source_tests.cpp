#include "rtl_tcp_source.h"

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
	const std::uint32_t iqRate = 960000;
	const std::uint32_t audioRate = 48000;
	RtlFmDemodulator demodulator(iqRate, audioRate);
	std::vector<unsigned char> iq;
	const std::size_t complexSamples = 96000;
	iq.reserve(complexSamples * 2);
	double phase = 0.0;
	const double phaseStep = 0.08;
	for (std::size_t index = 0; index < complexSamples; ++index)
	{
		const int i = static_cast<int>(127.5 + 100.0 * std::cos(phase));
		const int q = static_cast<int>(127.5 + 100.0 * std::sin(phase));
		iq.push_back(static_cast<unsigned char>(i));
		iq.push_back(static_cast<unsigned char>(q));
		phase += phaseStep;
	}
	std::vector<float> audio;
	demodulator.ProcessUnsignedIq(&iq[0], iq.size(), audio);
	Expect(audio.size() >= 4798 && audio.size() <= 4801, "RTL-TCP resampling ratio");
	double average = 0.0;
	for (std::size_t index = 10; index < audio.size(); ++index) average += audio[index];
	average /= static_cast<double>(audio.size() - 10);
	Expect(std::fabs(average - phaseStep / 3.14159265358979323846) < 0.001,
		"FM discriminator preserves phase direction and magnitude");

	demodulator.Reset();
	demodulator.ProcessUnsignedIq(NULL, 0, audio);
	Expect(audio.empty(), "empty IQ input is safe");

	std::cout << "RTL-TCP demodulator tests passed\n";
	return 0;
}
