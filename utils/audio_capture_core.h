#ifndef PDW_AUDIO_CAPTURE_CORE_H
#define PDW_AUDIO_CAPTURE_CORE_H

#include <cstddef>
#include <cstdint>

namespace pdw
{
namespace signal
{

enum CaptureShutdownDisposition
{
	CAPTURE_SHUTDOWN_RELEASE_SHARED_STATE,
	CAPTURE_SHUTDOWN_TERMINATE_WITHOUT_TEARDOWN
};

inline CaptureShutdownDisposition DecideCaptureShutdownDisposition(
	bool captureFinalized, bool serialReleased)
{
	return captureFinalized && serialReleased ?
		CAPTURE_SHUTDOWN_RELEASE_SHARED_STATE :
		CAPTURE_SHUTDOWN_TERMINATE_WITHOUT_TEARDOWN;
}

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
