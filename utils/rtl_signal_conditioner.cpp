#include "rtl_signal_conditioner.h"

#include "audio_signal_core.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdw
{
namespace signal
{

namespace
{
const double PI = 3.1415926535897932384626433832795;

double BesselI0(double value)
{
	double sum = 1.0;
	double term = 1.0;
	const double quarterValueSquared = value * value * 0.25;
	for (unsigned int order = 1; order < 32; ++order)
	{
		term *= quarterValueSquared /
			(static_cast<double>(order) * static_cast<double>(order));
		sum += term;
		if (term <= sum * 1.0e-14) break;
	}
	return sum;
}

double Sinc(double value)
{
	if (std::fabs(value) < 1.0e-12) return 1.0;
	const double angle = PI * value;
	return std::sin(angle) / angle;
}

float ClampCutoff(float cutoffHz, std::uint32_t inputSampleRate,
	std::uint32_t outputSampleRate)
{
	const float limitingRate = static_cast<float>((std::min)(inputSampleRate,
		outputSampleRate));
	const float maximumCutoff = limitingRate * 0.45f;
	return (std::max)(10.0f, (std::min)(cutoffHz, maximumCutoff));
}

unsigned int EstimateTapCount(std::uint32_t inputSampleRate,
	std::uint32_t outputSampleRate, float cutoffHz, float attenuationDb)
{
	const double stopBandEdge = 0.5 * static_cast<double>((std::min)(
		inputSampleRate, outputSampleRate));
	const double transitionHz = (std::max)(100.0,
		stopBandEdge - static_cast<double>(cutoffHz));
	const double estimate = (static_cast<double>(attenuationDb) - 8.0) *
		static_cast<double>(inputSampleRate) / (14.36 * transitionHz);
	unsigned int taps = static_cast<unsigned int>(std::ceil((std::max)(31.0, estimate)));
	if (taps > 511) taps = 511;
	if ((taps & 1U) == 0) ++taps;
	if (taps > 511) taps = 511;
	return taps;
}

std::uint32_t ChooseDemodulatorRate(std::uint32_t iqSampleRate,
	std::uint32_t audioSampleRate, std::uint32_t nfmCutoffHz)
{
	std::uint64_t requested = 96000;
	requested = (std::max)(requested, static_cast<std::uint64_t>(audioSampleRate));
	requested = (std::max)(requested, static_cast<std::uint64_t>(nfmCutoffHz) * 4U);
	requested = (std::min)(requested, static_cast<std::uint64_t>(iqSampleRate));
	return static_cast<std::uint32_t>(requested);
}
}

PolyphaseFirResampler::PolyphaseFirResampler()
	: inputSampleRate_(1), outputSampleRate_(1), tapCount_(31),
	  halfTapCount_(15), phaseCount_(64), inputSamplesPerOutput_(1.0),
	  nextOutputTime_(0.0), bufferStartIndex_(0), totalInputSamples_(0)
{
	BuildPhaseBank(0.45f, 60.0f);
}

void PolyphaseFirResampler::Configure(std::uint32_t inputSampleRate,
	std::uint32_t outputSampleRate, float cutoffHz,
	float stopBandAttenuationDb, unsigned int phaseCount)
{
	inputSampleRate_ = inputSampleRate ? inputSampleRate : 1;
	outputSampleRate_ = outputSampleRate ? outputSampleRate : inputSampleRate_;
	phaseCount_ = (std::max)(static_cast<unsigned int>(8),
		(std::min)(phaseCount, static_cast<unsigned int>(256)));
	stopBandAttenuationDb = (std::max)(30.0f,
		(std::min)(stopBandAttenuationDb, 100.0f));
	cutoffHz = ClampCutoff(cutoffHz, inputSampleRate_, outputSampleRate_);
	tapCount_ = EstimateTapCount(inputSampleRate_, outputSampleRate_, cutoffHz,
		stopBandAttenuationDb);
	halfTapCount_ = tapCount_ / 2;
	inputSamplesPerOutput_ = static_cast<double>(inputSampleRate_) /
		static_cast<double>(outputSampleRate_);
	BuildPhaseBank(cutoffHz, stopBandAttenuationDb);
	Reset();
}

void PolyphaseFirResampler::BuildPhaseBank(float cutoffHz,
	float stopBandAttenuationDb)
{
	coefficients_.assign(static_cast<std::size_t>(phaseCount_) * tapCount_, 0.0f);
	const double normalizedCutoff = static_cast<double>(cutoffHz) /
		static_cast<double>(inputSampleRate_);
	double beta = 0.0;
	if (stopBandAttenuationDb > 50.0f)
		beta = 0.1102 * (static_cast<double>(stopBandAttenuationDb) - 8.7);
	else if (stopBandAttenuationDb >= 21.0f)
		beta = 0.5842 * std::pow(static_cast<double>(stopBandAttenuationDb) - 21.0, 0.4) +
			0.07886 * (static_cast<double>(stopBandAttenuationDb) - 21.0);
	const double windowScale = BesselI0(beta);

	for (unsigned int phase = 0; phase < phaseCount_; ++phase)
	{
		const double fraction = static_cast<double>(phase) /
			static_cast<double>(phaseCount_);
		double coefficientSum = 0.0;
		for (unsigned int tap = 0; tap < tapCount_; ++tap)
		{
			const double offset = static_cast<double>(static_cast<int>(tap) -
				static_cast<int>(halfTapCount_)) - fraction;
			const double windowPosition = tapCount_ > 1
				? (2.0 * static_cast<double>(tap) /
					static_cast<double>(tapCount_ - 1) - 1.0)
				: 0.0;
			const double windowArgument = (std::max)(0.0,
				1.0 - windowPosition * windowPosition);
			const double window = BesselI0(beta * std::sqrt(windowArgument)) /
				windowScale;
			const double coefficient = 2.0 * normalizedCutoff *
				Sinc(2.0 * normalizedCutoff * offset) * window;
			coefficients_[static_cast<std::size_t>(phase) * tapCount_ + tap] =
				static_cast<float>(coefficient);
			coefficientSum += coefficient;
		}
		if (std::fabs(coefficientSum) > std::numeric_limits<double>::epsilon())
		{
			for (unsigned int tap = 0; tap < tapCount_; ++tap)
				coefficients_[static_cast<std::size_t>(phase) * tapCount_ + tap] =
					static_cast<float>(coefficients_[static_cast<std::size_t>(phase) *
						tapCount_ + tap] / coefficientSum);
		}
	}
}

void PolyphaseFirResampler::Reset()
{
	nextOutputTime_ = 0.0;
	bufferStartIndex_ = 0;
	totalInputSamples_ = 0;
	buffer_.clear();
}

void PolyphaseFirResampler::Process(const ComplexSample* samples,
	std::size_t sampleCount, std::vector<ComplexSample>& output)
{
	if (!samples || sampleCount == 0) return;
	if (buffer_.empty()) bufferStartIndex_ = totalInputSamples_;
	buffer_.insert(buffer_.end(), samples, samples + sampleCount);
	totalInputSamples_ += sampleCount;

	const std::int64_t lastAvailableIndex = static_cast<std::int64_t>(
		bufferStartIndex_ + buffer_.size() - 1);
	for (;;)
	{
		std::int64_t centerIndex = static_cast<std::int64_t>(
			std::floor(nextOutputTime_));
		if (centerIndex + static_cast<std::int64_t>(halfTapCount_) >
			lastAvailableIndex) break;

		const double fractionalPart = nextOutputTime_ -
			static_cast<double>(centerIndex);
		unsigned int phase = static_cast<unsigned int>(std::floor(
			fractionalPart * phaseCount_ + 0.5));
		if (phase >= phaseCount_)
		{
			phase = 0;
			++centerIndex;
			if (centerIndex + static_cast<std::int64_t>(halfTapCount_) >
				lastAvailableIndex) break;
		}

		double sumI = 0.0;
		double sumQ = 0.0;
		const std::size_t coefficientBase = static_cast<std::size_t>(phase) *
			tapCount_;
		for (unsigned int tap = 0; tap < tapCount_; ++tap)
		{
			const std::int64_t sampleIndex = centerIndex +
				static_cast<std::int64_t>(tap) -
				static_cast<std::int64_t>(halfTapCount_);
			if (sampleIndex < 0 || sampleIndex <
				static_cast<std::int64_t>(bufferStartIndex_) ||
				sampleIndex > lastAvailableIndex) continue;
			const ComplexSample& sample = buffer_[static_cast<std::size_t>(
				sampleIndex - static_cast<std::int64_t>(bufferStartIndex_))];
			const float coefficient = coefficients_[coefficientBase + tap];
			sumI += static_cast<double>(sample.i) * coefficient;
			sumQ += static_cast<double>(sample.q) * coefficient;
		}
		ComplexSample filtered;
		filtered.i = static_cast<float>(sumI);
		filtered.q = static_cast<float>(sumQ);
		output.push_back(filtered);
		nextOutputTime_ += inputSamplesPerOutput_;
	}

	const std::int64_t nextCenter = static_cast<std::int64_t>(
		std::floor(nextOutputTime_));
	const std::int64_t earliestNeeded = nextCenter -
		static_cast<std::int64_t>(halfTapCount_) - 2;
	if (earliestNeeded > static_cast<std::int64_t>(bufferStartIndex_))
	{
		const std::uint64_t removable = static_cast<std::uint64_t>(earliestNeeded) -
			bufferStartIndex_;
		const std::size_t removeCount = static_cast<std::size_t>((std::min)(
			removable, static_cast<std::uint64_t>(buffer_.size())));
		buffer_.erase(buffer_.begin(), buffer_.begin() + removeCount);
		bufferStartIndex_ += removeCount;
	}
}

std::uint32_t PolyphaseFirResampler::inputSampleRate() const
{
	return inputSampleRate_;
}

std::uint32_t PolyphaseFirResampler::outputSampleRate() const
{
	return outputSampleRate_;
}

unsigned int PolyphaseFirResampler::tapCount() const
{
	return tapCount_;
}

unsigned int PolyphaseFirResampler::phaseCount() const
{
	return phaseCount_;
}

RtlSignalConditioner::RtlSignalConditioner()
	: iqSampleRate_(1024000), audioSampleRate_(48000), nfmCutoffHz_(12000),
	  demodulatorSampleRate_(96000), havePreviousIq_(false)
{
	previousIq_.i = 0.0f;
	previousIq_.q = 0.0f;
	Configure(iqSampleRate_, audioSampleRate_, nfmCutoffHz_);
}

RtlSignalConditioner::RtlSignalConditioner(std::uint32_t iqSampleRate,
	std::uint32_t audioSampleRate, std::uint32_t nfmCutoffHz)
	: iqSampleRate_(iqSampleRate), audioSampleRate_(audioSampleRate),
	  nfmCutoffHz_(nfmCutoffHz), demodulatorSampleRate_(96000),
	  havePreviousIq_(false)
{
	previousIq_.i = 0.0f;
	previousIq_.q = 0.0f;
	Configure(iqSampleRate, audioSampleRate, nfmCutoffHz);
}

void RtlSignalConditioner::Configure(std::uint32_t iqSampleRate,
	std::uint32_t audioSampleRate, std::uint32_t nfmCutoffHz)
{
	iqSampleRate_ = iqSampleRate ? iqSampleRate : 1024000;
	audioSampleRate_ = audioSampleRate && audioSampleRate <= iqSampleRate_
		? audioSampleRate : 48000;
	nfmCutoffHz_ = (std::max)(static_cast<std::uint32_t>(5000),
		(std::min)(static_cast<std::uint32_t>(25000), nfmCutoffHz));
	demodulatorSampleRate_ = ChooseDemodulatorRate(iqSampleRate_,
		audioSampleRate_, nfmCutoffHz_);

	const float iqCutoff = (std::min)(static_cast<float>(nfmCutoffHz_),
		static_cast<float>(demodulatorSampleRate_) * 0.40f);
	iqResampler_.Configure(iqSampleRate_, demodulatorSampleRate_, iqCutoff);
	const std::uint32_t limitingAudioRate = (std::min)(demodulatorSampleRate_,
		audioSampleRate_);
	const float audioCutoff = (std::min)(static_cast<float>(nfmCutoffHz_),
		static_cast<float>(limitingAudioRate) * 0.42f);
	audioResampler_.Configure(demodulatorSampleRate_, audioSampleRate_, audioCutoff);
	Reset();
}

void RtlSignalConditioner::Reset()
{
	iqResampler_.Reset();
	audioResampler_.Reset();
	previousIq_.i = 0.0f;
	previousIq_.q = 0.0f;
	havePreviousIq_ = false;
}

void RtlSignalConditioner::ProcessUnsignedIq(const unsigned char* iqBytes,
	std::size_t byteCount, std::vector<float>& audio)
{
	audio.clear();
	if (!iqBytes || byteCount < 2) return;

	std::vector<ComplexSample> input;
	input.reserve(byteCount / 2);
	for (std::size_t byte = 0; byte + 1 < byteCount; byte += 2)
	{
		ComplexSample sample;
		sample.i = (static_cast<int>(iqBytes[byte]) - 127.5f) / 128.0f;
		sample.q = (static_cast<int>(iqBytes[byte + 1]) - 127.5f) / 128.0f;
		input.push_back(sample);
	}

	std::vector<ComplexSample> filteredIq;
	filteredIq.reserve(static_cast<std::size_t>(input.size()) *
		demodulatorSampleRate_ / iqSampleRate_ + 2);
	iqResampler_.Process(&input[0], input.size(), filteredIq);
	if (filteredIq.empty()) return;

	std::vector<ComplexSample> discriminator;
	discriminator.reserve(filteredIq.size());
	for (std::vector<ComplexSample>::const_iterator sample = filteredIq.begin();
		sample != filteredIq.end(); ++sample)
	{
		if (havePreviousIq_)
		{
			const float cross = previousIq_.i * sample->q - previousIq_.q * sample->i;
			const float dot = previousIq_.i * sample->i + previousIq_.q * sample->q;
			ComplexSample demodulated;
			// Express discriminator output as a fraction of the configured NFM
			// cutoff. Unlike the legacy per-IQ-sample phase value, this keeps
			// decoder level stable when the operator changes the IQ sample rate.
			const double frequencyScale = static_cast<double>(demodulatorSampleRate_) /
				(2.0 * static_cast<double>(nfmCutoffHz_));
			demodulated.i = static_cast<float>(
				std::atan2(cross, dot) / PI * frequencyScale);
			demodulated.q = 0.0f;
			discriminator.push_back(demodulated);
		}
		previousIq_ = *sample;
		havePreviousIq_ = true;
	}
	if (discriminator.empty()) return;

	std::vector<ComplexSample> resampledAudio;
	resampledAudio.reserve(static_cast<std::size_t>(discriminator.size()) *
		audioSampleRate_ / demodulatorSampleRate_ + 2);
	audioResampler_.Process(&discriminator[0], discriminator.size(), resampledAudio);
	audio.reserve(resampledAudio.size());
	for (std::vector<ComplexSample>::const_iterator sample = resampledAudio.begin();
		sample != resampledAudio.end(); ++sample)
		audio.push_back(ClampNormalized(sample->i));
}

std::uint32_t RtlSignalConditioner::demodulatorSampleRate() const
{
	return demodulatorSampleRate_;
}

unsigned int RtlSignalConditioner::iqFilterTapCount() const
{
	return iqResampler_.tapCount();
}

unsigned int RtlSignalConditioner::audioFilterTapCount() const
{
	return audioResampler_.tapCount();
}

} // namespace signal
} // namespace pdw
