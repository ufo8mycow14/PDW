#include "bounded_audio_decode_worker.h"

#include <algorithm>
#include <cstring>

namespace pdw
{
namespace signal
{

namespace
{
	const DWORD WORKER_STOP_TIMEOUT_MS = 5000;
	const DWORD DECODER_SERVICE_INTERVAL_MS = 100;
}

AudioDecodeWorkerMetrics::AudioDecodeWorkerMetrics()
	: queueDepth(0), queueCapacity(0), queueHighWater(0), maxSamplesPerBlock(0),
	  preallocatedBytes(0), enqueuedBlocks(0), enqueuedSamples(0), decodedBlocks(0),
	  decodedSamples(0), droppedBlocks(0), droppedSamples(0), resetDiscardedBlocks(0),
	  resetDiscardedSamples(0), callbacksWhileStopped(0), serviceTicks(0),
	  threadStarts(0), threadStops(0), stopTimeouts(0), handlesCreated(0),
	  handlesClosed(0), currentDecoderLagMs(0), maximumDecoderLagMs(0),
	  currentOwnedHandles(0), running(false), accepting(false),
	  priorityElevated(false), quarantined(false)
{
}

BoundedAudioDecodeWorker::Slot::Slot()
	: sampleCount(0), sampleRate(0), discontinuity(false), enqueuedTick(0)
{
}

BoundedAudioDecodeWorker::BoundedAudioDecodeWorker(
	std::size_t queueBlocks, std::size_t samplesPerBlock)
	: slots_((std::max)(static_cast<std::size_t>(2), queueBlocks)),
	  capacity_(slots_.size()),
	  samplesPerBlock_((std::max)(static_cast<std::size_t>(64), samplesPerBlock)),
	  head_(0), tail_(0), count_(0), processing_(false), pendingDiscontinuity_(true),
	  drainOnStop_(true), consumer_(NULL), stopEvent_(NULL), dataEvent_(NULL),
	  thread_(NULL)
{
	InitializeCriticalSection(&lock_);
	for (std::size_t index = 0; index < slots_.size(); ++index)
		slots_[index].samples.resize(samplesPerBlock_);
	metrics_.queueCapacity = capacity_;
	metrics_.maxSamplesPerBlock = samplesPerBlock_;
	metrics_.preallocatedBytes = capacity_ * samplesPerBlock_ * sizeof(float);
}

BoundedAudioDecodeWorker::~BoundedAudioDecodeWorker()
{
	if (!Stop(false)) FinalizeForShutdown();
	DeleteCriticalSection(&lock_);
}

bool BoundedAudioDecodeWorker::Start(AudioDecodeConsumer* consumer)
{
	if (!consumer || !Stop(false)) return false;

	HANDLE stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	HANDLE dataEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!stopEvent || !dataEvent)
	{
		if (dataEvent) CloseHandle(dataEvent);
		if (stopEvent) CloseHandle(stopEvent);
		return false;
	}

	EnterCriticalSection(&lock_);
	stopEvent_ = stopEvent;
	dataEvent_ = dataEvent;
	consumer_ = consumer;
	head_ = 0;
	tail_ = 0;
	count_ = 0;
	processing_ = false;
	pendingDiscontinuity_ = true;
	drainOnStop_ = true;
	metrics_.queueDepth = 0;
	metrics_.accepting = true;
	metrics_.running = true;
	metrics_.quarantined = false;
	metrics_.priorityElevated = false;
	metrics_.handlesCreated += 2;
	metrics_.currentOwnedHandles = 2;
	LeaveCriticalSection(&lock_);

	HANDLE thread = CreateThread(NULL, 0, ThreadEntry, this, 0, NULL);
	if (!thread)
	{
		EnterCriticalSection(&lock_);
		metrics_.accepting = false;
		metrics_.running = false;
		consumer_ = NULL;
		stopEvent_ = NULL;
		dataEvent_ = NULL;
		metrics_.handlesClosed += 2;
		metrics_.currentOwnedHandles = 0;
		LeaveCriticalSection(&lock_);
		CloseHandle(dataEvent);
		CloseHandle(stopEvent);
		return false;
	}

	EnterCriticalSection(&lock_);
	thread_ = thread;
	metrics_.threadStarts++;
	metrics_.handlesCreated++;
	metrics_.currentOwnedHandles = 3;
	LeaveCriticalSection(&lock_);
	return true;
}

bool BoundedAudioDecodeWorker::Stop(bool drainQueuedAudio)
{
	HANDLE thread = NULL;
	HANDLE stopEvent = NULL;
	HANDLE dataEvent = NULL;
	EnterCriticalSection(&lock_);
	thread = thread_;
	stopEvent = stopEvent_;
	dataEvent = dataEvent_;
	metrics_.accepting = false;
	drainOnStop_ = drainQueuedAudio;
	if (!drainQueuedAudio) ClearQueuedBlocksLocked(false);
	LeaveCriticalSection(&lock_);

	if (!thread)
	{
		if (dataEvent) CloseHandle(dataEvent);
		if (stopEvent) CloseHandle(stopEvent);
		EnterCriticalSection(&lock_);
		if (dataEvent) { metrics_.handlesClosed++; dataEvent_ = NULL; }
		if (stopEvent) { metrics_.handlesClosed++; stopEvent_ = NULL; }
		metrics_.currentOwnedHandles = 0;
		metrics_.running = false;
		consumer_ = NULL;
		LeaveCriticalSection(&lock_);
		return true;
	}

	if (stopEvent) SetEvent(stopEvent);
	if (dataEvent) SetEvent(dataEvent);
	const DWORD waitResult = WaitForSingleObject(thread, WORKER_STOP_TIMEOUT_MS);
	if (waitResult != WAIT_OBJECT_0)
	{
		EnterCriticalSection(&lock_);
		metrics_.stopTimeouts++;
		metrics_.quarantined = true;
		LeaveCriticalSection(&lock_);
		return false;
	}
	CleanupJoinedThread();
	return true;
}

void BoundedAudioDecodeWorker::FinalizeForShutdown()
{
	HANDLE thread = NULL;
	HANDLE stopEvent = NULL;
	HANDLE dataEvent = NULL;
	EnterCriticalSection(&lock_);
	metrics_.accepting = false;
	drainOnStop_ = false;
	ClearQueuedBlocksLocked(false);
	thread = thread_;
	stopEvent = stopEvent_;
	dataEvent = dataEvent_;
	LeaveCriticalSection(&lock_);
	if (stopEvent) SetEvent(stopEvent);
	if (dataEvent) SetEvent(dataEvent);
	if (thread) WaitForSingleObject(thread, INFINITE);
	CleanupJoinedThread();
}

void BoundedAudioDecodeWorker::CleanupJoinedThread()
{
	HANDLE thread = NULL;
	HANDLE stopEvent = NULL;
	HANDLE dataEvent = NULL;
	EnterCriticalSection(&lock_);
	thread = thread_;
	stopEvent = stopEvent_;
	dataEvent = dataEvent_;
	thread_ = NULL;
	stopEvent_ = NULL;
	dataEvent_ = NULL;
	consumer_ = NULL;
	metrics_.running = false;
	metrics_.accepting = false;
	metrics_.quarantined = false;
	metrics_.priorityElevated = false;
	metrics_.queueDepth = 0;
	if (thread) metrics_.threadStops++;
	if (thread) metrics_.handlesClosed++;
	if (stopEvent) metrics_.handlesClosed++;
	if (dataEvent) metrics_.handlesClosed++;
	metrics_.currentOwnedHandles = 0;
	LeaveCriticalSection(&lock_);
	if (thread) CloseHandle(thread);
	if (dataEvent) CloseHandle(dataEvent);
	if (stopEvent) CloseHandle(stopEvent);
}

void BoundedAudioDecodeWorker::ResetQueueForDiscontinuity()
{
	EnterCriticalSection(&lock_);
	ClearQueuedBlocksLocked(true);
	pendingDiscontinuity_ = true;
	LeaveCriticalSection(&lock_);
}

void BoundedAudioDecodeWorker::ClearQueuedBlocksLocked(bool countAsReset)
{
	std::size_t discardIndex = head_;
	std::size_t discardCount = count_;
	if (processing_ && discardCount)
	{
		discardIndex = (discardIndex + 1) % capacity_;
		discardCount--;
	}
	for (std::size_t index = 0; index < discardCount; ++index)
	{
		Slot& slot = slots_[(discardIndex + index) % capacity_];
		if (countAsReset)
		{
			metrics_.resetDiscardedBlocks++;
			metrics_.resetDiscardedSamples += slot.sampleCount;
		}
		slot.sampleCount = 0;
	}
	if (processing_ && count_)
	{
		count_ = 1;
		tail_ = (head_ + 1) % capacity_;
	}
	else
	{
		head_ = 0;
		tail_ = 0;
		count_ = 0;
	}
	metrics_.queueDepth = count_;
}

AudioDecodeWorkerMetrics BoundedAudioDecodeWorker::Snapshot() const
{
	EnterCriticalSection(&lock_);
	AudioDecodeWorkerMetrics snapshot = metrics_;
	snapshot.queueDepth = count_;
	if (count_)
	{
		const ULONGLONG enqueuedTick = slots_[head_].enqueuedTick;
		const ULONGLONG now = GetTickCount64();
		snapshot.currentDecoderLagMs = now >= enqueuedTick ? now - enqueuedTick : 0;
	}
	else snapshot.currentDecoderLagMs = 0;
	LeaveCriticalSection(&lock_);
	return snapshot;
}

void BoundedAudioDecodeWorker::OnAudioSamples(const float* samples,
	std::size_t sampleCount, std::uint32_t sampleRate, bool discontinuity)
{
	if (!samples || !sampleCount || !sampleRate) return;
	std::size_t offset = 0;
	bool signalWorker = false;
	while (offset < sampleCount)
	{
		const std::size_t chunk = (std::min)(samplesPerBlock_, sampleCount - offset);
		EnterCriticalSection(&lock_);
		if (!metrics_.accepting || !thread_)
		{
			metrics_.callbacksWhileStopped++;
			LeaveCriticalSection(&lock_);
			break;
		}
		if (count_ >= capacity_)
		{
			const std::size_t remaining = sampleCount - offset;
			metrics_.droppedBlocks += (remaining + samplesPerBlock_ - 1) / samplesPerBlock_;
			metrics_.droppedSamples += remaining;
			pendingDiscontinuity_ = true;
			LeaveCriticalSection(&lock_);
			break;
		}

		Slot& slot = slots_[tail_];
		std::memcpy(&slot.samples[0], samples + offset, chunk * sizeof(float));
		slot.sampleCount = chunk;
		slot.sampleRate = sampleRate;
		slot.discontinuity = pendingDiscontinuity_ || (offset == 0 && discontinuity);
		slot.enqueuedTick = GetTickCount64();
		pendingDiscontinuity_ = false;
		tail_ = (tail_ + 1) % capacity_;
		count_++;
		metrics_.queueDepth = count_;
		metrics_.queueHighWater = (std::max)(metrics_.queueHighWater, count_);
		metrics_.enqueuedBlocks++;
		metrics_.enqueuedSamples += chunk;
		signalWorker = true;
		LeaveCriticalSection(&lock_);
		offset += chunk;
	}
	if (signalWorker)
	{
		EnterCriticalSection(&lock_);
		if (dataEvent_) SetEvent(dataEvent_);
		LeaveCriticalSection(&lock_);
	}
}

DWORD WINAPI BoundedAudioDecodeWorker::ThreadEntry(LPVOID context)
{
	return static_cast<BoundedAudioDecodeWorker*>(context)->WorkerThread();
}

DWORD BoundedAudioDecodeWorker::WorkerThread()
{
	const bool priorityElevated = SetThreadPriority(GetCurrentThread(),
		THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
	EnterCriticalSection(&lock_);
	metrics_.priorityElevated = priorityElevated;
	LeaveCriticalSection(&lock_);

	ULONGLONG nextTick = GetTickCount64() + DECODER_SERVICE_INTERVAL_MS;
	for (;;)
	{
		while (ProcessOneBlock()) ServiceTickIfDue(nextTick);

		bool stopRequested = false;
		bool drain = false;
		std::size_t queued = 0;
		EnterCriticalSection(&lock_);
		stopRequested = stopEvent_ && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0;
		drain = drainOnStop_;
		queued = count_;
		LeaveCriticalSection(&lock_);
		if (stopRequested && (!drain || queued == 0)) break;

		const ULONGLONG now = GetTickCount64();
		const DWORD waitMs = now >= nextTick ? 0 : static_cast<DWORD>(
			(std::min)(static_cast<ULONGLONG>(DECODER_SERVICE_INTERVAL_MS), nextTick - now));
		HANDLE events[2] = { stopEvent_, dataEvent_ };
		WaitForMultipleObjects(2, events, FALSE, waitMs);
		ServiceTickIfDue(nextTick);
	}
	return 0;
}

bool BoundedAudioDecodeWorker::ProcessOneBlock()
{
	AudioDecodeConsumer* consumer = NULL;
	Slot* slot = NULL;
	EnterCriticalSection(&lock_);
	if (count_)
	{
		processing_ = true;
		slot = &slots_[head_];
		consumer = consumer_;
	}
	LeaveCriticalSection(&lock_);
	if (!slot || !consumer) return false;

	const ULONGLONG now = GetTickCount64();
	const ULONGLONG lag = now >= slot->enqueuedTick ? now - slot->enqueuedTick : 0;
	consumer->OnDecodeAudioBlock(&slot->samples[0], slot->sampleCount,
		slot->sampleRate, slot->discontinuity);

	EnterCriticalSection(&lock_);
	metrics_.decodedBlocks++;
	metrics_.decodedSamples += slot->sampleCount;
	metrics_.currentDecoderLagMs = lag;
	metrics_.maximumDecoderLagMs = (std::max)(metrics_.maximumDecoderLagMs, lag);
	slot->sampleCount = 0;
	head_ = (head_ + 1) % capacity_;
	count_--;
	processing_ = false;
	metrics_.queueDepth = count_;
	LeaveCriticalSection(&lock_);
	return true;
}

void BoundedAudioDecodeWorker::ServiceTickIfDue(ULONGLONG& nextTick)
{
	const ULONGLONG now = GetTickCount64();
	if (now < nextTick) return;
	AudioDecodeConsumer* consumer = NULL;
	EnterCriticalSection(&lock_);
	consumer = consumer_;
	metrics_.serviceTicks++;
	LeaveCriticalSection(&lock_);
	if (consumer) consumer->OnDecodeServiceTick();
	nextTick += DECODER_SERVICE_INTERVAL_MS;
	if (nextTick + 1000 < now) nextTick = now + DECODER_SERVICE_INTERVAL_MS;
}

} // namespace signal
} // namespace pdw
