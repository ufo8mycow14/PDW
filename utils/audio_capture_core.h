#ifndef PDW_AUDIO_CAPTURE_CORE_H
#define PDW_AUDIO_CAPTURE_CORE_H

#include <cstddef>
#include <cstdint>

namespace pdw
{
namespace signal
{

class AudioSampleSink
{
public:
	virtual ~AudioSampleSink() {}
	// Capture sources call this on their worker thread. Consumers must enqueue
	// quickly and must never call PDW's decoder UI routines directly.
	virtual void OnAudioSamples(const float* samples,
		std::size_t sampleCount,
		std::uint32_t sampleRate,
		bool discontinuity) = 0;
};

} // namespace signal
} // namespace pdw

#endif
