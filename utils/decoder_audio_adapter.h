#ifndef PDW_DECODER_AUDIO_ADAPTER_H
#define PDW_DECODER_AUDIO_ADAPTER_H
#include "rtl_signal_conditioner.h"
#include <algorithm>

namespace pdw { namespace signal {
// Legacy acquisition windows are calibrated to 44.1 kHz. Modern capture and
// replay can use other rates without changing those established bit paths.
class DecoderAudioAdapter
{
public:
	static const std::uint32_t ReferenceRate = 44100;
	static bool SupportsRate(std::uint32_t rate) { return rate >= 8000 && rate <= 192000; }
	bool Process(const float* samples, std::size_t count, std::uint32_t rate,
		bool discontinuity, std::vector<float>& output, bool& resetDecoder)
	{
		output.clear();
		resetDecoder = discontinuity || rate != rate_;
		if (!samples || !count || !SupportsRate(rate)) { rate_ = 0; return false; }
		if (resetDecoder)
		{
			rate_ = rate;
			totalInput_ = totalOutput_ = 0;
			resampler_.Configure(rate, ReferenceRate,
				static_cast<float>((std::min)(rate, ReferenceRate)) * 0.40f);
		}
		totalInput_ += count;
		if (rate == ReferenceRate) { output.assign(samples, samples + count); totalOutput_ += count; return true; }
		input_.resize(count);
		for (std::size_t i = 0; i < count; ++i) input_[i] = {samples[i], 0.0f};
		resampled_.clear();
		resampler_.Process(input_.data(), input_.size(), resampled_);
		output.reserve(resampled_.size());
		for (const ComplexSample& sample : resampled_) output.push_back(sample.i);
		totalOutput_ += output.size();
		return true;
	}
	void Flush(std::vector<float>& output)
	{
		output.clear();
		if (!rate_) return;
		const std::uint64_t expected = (totalInput_ * ReferenceRate + rate_ - 1) / rate_;
		const std::size_t remaining = static_cast<std::size_t>(expected - totalOutput_);
		if (rate_ != ReferenceRate && remaining)
		{
			// The existing FIR has at most 511 taps. Supply only its finite tail,
			// then discard padding beyond the original recording duration.
			input_.assign(512, ComplexSample{0.0f, 0.0f});
			resampled_.clear();
			resampler_.Process(input_.data(), input_.size(), resampled_);
			for (std::size_t i = 0; i < (std::min)(remaining, resampled_.size()); ++i)
				output.push_back(resampled_[i].i);
		}
		rate_ = 0;
	}
private:
	std::uint32_t rate_ = 0;
	std::uint64_t totalInput_ = 0, totalOutput_ = 0;
	PolyphaseFirResampler resampler_;
	std::vector<ComplexSample> input_, resampled_;
};
} }
#endif
