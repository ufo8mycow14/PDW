#ifndef PDW_BOUNDED_AUDIO_DECODE_WORKER_H
#define PDW_BOUNDED_AUDIO_DECODE_WORKER_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio_capture_core.h"

namespace pdw
{
namespace signal
{

// Modern capture callbacks must never depend on the Windows message loop. This
// consumer is invoked only by BoundedAudioDecodeWorker's dedicated thread.
class AudioDecodeConsumer
{
public:
	virtual ~AudioDecodeConsumer() {}
	virtual void OnDecodeAudioBlock(const float* samples, std::size_t sampleCount,
		std::uint32_t sampleRate, bool discontinuity) = 0;
	virtual void OnDecodeServiceTick() {}
};

struct AudioDecodeWorkerMetrics
{
	std::size_t queueDepth;
	std::size_t queueCapacity;
	std::size_t queueHighWater;
	std::size_t maxSamplesPerBlock;
	std::size_t preallocatedBytes;
	std::uint64_t enqueuedBlocks;
	std::uint64_t enqueuedSamples;
	std::uint64_t decodedBlocks;
	std::uint64_t decodedSamples;
	std::uint64_t droppedBlocks;
	std::uint64_t droppedSamples;
	std::uint64_t resetDiscardedBlocks;
	std::uint64_t resetDiscardedSamples;
	std::uint64_t callbacksWhileStopped;
	std::uint64_t serviceTicks;
	std::uint64_t threadStarts;
	std::uint64_t threadStops;
	std::uint64_t stopTimeouts;
	std::uint64_t handlesCreated;
	std::uint64_t handlesClosed;
	std::uint64_t currentDecoderLagMs;
	std::uint64_t maximumDecoderLagMs;
	unsigned int currentOwnedHandles;
	bool running;
	bool accepting;
	bool priorityElevated;
	bool quarantined;

	AudioDecodeWorkerMetrics();
};

// A fixed-capacity single-consumer queue. All sample storage is allocated in
// the constructor, before a receiver callback can run. Full queues drop new
// blocks, count the loss, and mark the next accepted block discontinuous.
class BoundedAudioDecodeWorker : public AudioSampleSink
{
public:
	static const std::size_t DEFAULT_QUEUE_BLOCKS = 64;
	static const std::size_t DEFAULT_SAMPLES_PER_BLOCK = 4096;

	explicit BoundedAudioDecodeWorker(
		std::size_t queueBlocks = DEFAULT_QUEUE_BLOCKS,
		std::size_t samplesPerBlock = DEFAULT_SAMPLES_PER_BLOCK);
	~BoundedAudioDecodeWorker();

	bool Start(AudioDecodeConsumer* consumer);
	bool Stop(bool drainQueuedAudio = true);
	void FinalizeForShutdown();
	void ResetQueueForDiscontinuity();
	AudioDecodeWorkerMetrics Snapshot() const;

	void OnAudioSamples(const float* samples, std::size_t sampleCount,
		std::uint32_t sampleRate, bool discontinuity);

private:
	BoundedAudioDecodeWorker(const BoundedAudioDecodeWorker&);
	BoundedAudioDecodeWorker& operator=(const BoundedAudioDecodeWorker&);

	struct Slot
	{
		std::vector<float> samples;
		std::size_t sampleCount;
		std::uint32_t sampleRate;
		bool discontinuity;
		ULONGLONG enqueuedTick;

		Slot();
	};

	static DWORD WINAPI ThreadEntry(LPVOID context);
	DWORD WorkerThread();
	bool ProcessOneBlock();
	void ServiceTickIfDue(ULONGLONG& nextTick);
	void CleanupJoinedThread();
	void ClearQueuedBlocksLocked(bool countAsReset);

	mutable CRITICAL_SECTION lock_;
	std::vector<Slot> slots_;
	const std::size_t capacity_;
	const std::size_t samplesPerBlock_;
	std::size_t head_;
	std::size_t tail_;
	std::size_t count_;
	bool processing_;
	bool pendingDiscontinuity_;
	bool drainOnStop_;
	AudioDecodeConsumer* consumer_;
	HANDLE stopEvent_;
	HANDLE dataEvent_;
	HANDLE thread_;
	AudioDecodeWorkerMetrics metrics_;
};

} // namespace signal
} // namespace pdw

#endif
