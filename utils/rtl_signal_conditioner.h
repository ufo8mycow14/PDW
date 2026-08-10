#ifndef PDW_RTL_SIGNAL_CONDITIONER_H
#define PDW_RTL_SIGNAL_CONDITIONER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pdw
{
namespace signal
{

struct ComplexSample
{
	float i;
	float q;
};

// Streaming polyphase FIR resampler used by the optional RTL signal
// conditioner. The phase bank applies a windowed-sinc low-pass while changing
// sample rate, so decimation never falls back to unfiltered averaging.
class PolyphaseFirResampler
{
public:
	PolyphaseFirResampler();

	void Configure(std::uint32_t inputSampleRate, std::uint32_t outputSampleRate,
		float cutoffHz, float stopBandAttenuationDb = 60.0f,
		unsigned int phaseCount = 64);
	void Reset();
	void Process(const ComplexSample* samples, std::size_t sampleCount,
		std::vector<ComplexSample>& output);

	std::uint32_t inputSampleRate() const;
	std::uint32_t outputSampleRate() const;
	unsigned int tapCount() const;
	unsigned int phaseCount() const;

private:
	void BuildPhaseBank(float cutoffHz, float stopBandAttenuationDb);

	std::uint32_t inputSampleRate_;
	std::uint32_t outputSampleRate_;
	unsigned int tapCount_;
	unsigned int halfTapCount_;
	unsigned int phaseCount_;
	double inputSamplesPerOutput_;
	double nextOutputTime_;
	std::uint64_t bufferStartIndex_;
	std::uint64_t totalInputSamples_;
	std::vector<float> coefficients_;
	std::vector<ComplexSample> buffer_;
};

// Optional high-quality RTL front end. IQ is channel-filtered and resampled
// before the non-linear FM discriminator, then the discriminator stream is
// low-pass filtered and resampled to PDW's configured decoder-audio rate.
class RtlSignalConditioner
{
public:
	RtlSignalConditioner();
	RtlSignalConditioner(std::uint32_t iqSampleRate, std::uint32_t audioSampleRate,
		std::uint32_t nfmCutoffHz);

	void Configure(std::uint32_t iqSampleRate, std::uint32_t audioSampleRate,
		std::uint32_t nfmCutoffHz);
	void Reset();
	void ProcessUnsignedIq(const unsigned char* iqBytes, std::size_t byteCount,
		std::vector<float>& audio);

	std::uint32_t demodulatorSampleRate() const;
	unsigned int iqFilterTapCount() const;
	unsigned int audioFilterTapCount() const;

private:
	std::uint32_t iqSampleRate_;
	std::uint32_t audioSampleRate_;
	std::uint32_t nfmCutoffHz_;
	std::uint32_t demodulatorSampleRate_;
	PolyphaseFirResampler iqResampler_;
	PolyphaseFirResampler audioResampler_;
	ComplexSample previousIq_;
	bool havePreviousIq_;
};

} // namespace signal
} // namespace pdw

#endif
