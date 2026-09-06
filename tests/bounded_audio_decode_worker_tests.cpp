#include "bounded_audio_decode_worker.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (condition) return;
		std::fprintf(stderr, "FAILED: %s\n", message);
		std::exit(1);
	}

	class CountingConsumer : public pdw::signal::AudioDecodeConsumer
	{
	public:
		CountingConsumer()
			: samples_(0), blocks_(0), discontinuities_(0), ticks_(0),
			  sequenceValid_(true), expectedSequence_(0)
		{
			InitializeCriticalSection(&lock_);
		}

		~CountingConsumer()
		{
			DeleteCriticalSection(&lock_);
		}

		void OnDecodeAudioBlock(const float* samples, std::size_t sampleCount,
			std::uint32_t sampleRate, bool discontinuity)
		{
			EnterCriticalSection(&lock_);
			if (sampleRate != 48000) sequenceValid_ = false;
			for (std::size_t index = 0; index < sampleCount; ++index)
			{
				const float expected = static_cast<float>(expectedSequence_ % 997u) / 997.0f;
				if (std::fabs(samples[index] - expected) > 0.000001f)
					sequenceValid_ = false;
				expectedSequence_++;
			}
			samples_ += sampleCount;
			blocks_++;
			if (discontinuity) discontinuities_++;
			LeaveCriticalSection(&lock_);
		}

		void OnDecodeServiceTick()
		{
			EnterCriticalSection(&lock_);
			ticks_++;
			LeaveCriticalSection(&lock_);
		}

		std::uint64_t samples() const
		{
			EnterCriticalSection(&lock_);
			const std::uint64_t value = samples_;
			LeaveCriticalSection(&lock_);
			return value;
		}

		std::uint64_t discontinuities() const
		{
			EnterCriticalSection(&lock_);
			const std::uint64_t value = discontinuities_;
			LeaveCriticalSection(&lock_);
			return value;
		}

		std::uint64_t ticks() const
		{
			EnterCriticalSection(&lock_);
			const std::uint64_t value = ticks_;
			LeaveCriticalSection(&lock_);
			return value;
		}

		bool sequenceValid() const
		{
			EnterCriticalSection(&lock_);
			const bool value = sequenceValid_;
			LeaveCriticalSection(&lock_);
			return value;
		}

	private:
		mutable CRITICAL_SECTION lock_;
		std::uint64_t samples_;
		std::uint64_t blocks_;
		std::uint64_t discontinuities_;
		std::uint64_t ticks_;
		bool sequenceValid_;
		std::uint64_t expectedSequence_;
	};

	class BlockingConsumer : public pdw::signal::AudioDecodeConsumer
	{
	public:
		BlockingConsumer()
			: entered_(CreateEvent(NULL, TRUE, FALSE, NULL)),
			  release_(CreateEvent(NULL, TRUE, FALSE, NULL)), discontinuities_(0)
		{
		}

		~BlockingConsumer()
		{
			CloseHandle(release_);
			CloseHandle(entered_);
		}

		void OnDecodeAudioBlock(const float*, std::size_t,
			std::uint32_t, bool discontinuity)
		{
			if (discontinuity) InterlockedIncrement(&discontinuities_);
			SetEvent(entered_);
			WaitForSingleObject(release_, 5000);
		}

		bool WaitUntilEntered() const
		{
			return WaitForSingleObject(entered_, 2000) == WAIT_OBJECT_0;
		}

		void Release() { SetEvent(release_); }
		LONG discontinuities() const
		{
			return InterlockedCompareExchange(
				const_cast<volatile LONG*>(&discontinuities_), 0, 0);
		}

	private:
		HANDLE entered_;
		HANDLE release_;
		volatile LONG discontinuities_;
	};

	void FillSequence(std::vector<float>& samples, std::uint64_t first)
	{
		for (std::size_t index = 0; index < samples.size(); ++index)
			samples[index] = static_cast<float>((first + index) % 997u) / 997.0f;
	}

	void ExerciseSustainedRtlRateWithoutUiService()
	{
		// A 32,768-byte RTL callback contains 16,384 IQ pairs. At
		// 1.024 Msps -> 48 kHz it yields exactly 768 audio samples.
		const std::size_t callbackSamples = 768;
		const std::uint64_t simulatedSeconds = 60;
		const std::uint64_t totalSamples = simulatedSeconds * 48000u;
		pdw::signal::BoundedAudioDecodeWorker worker(64, 4096);
		CountingConsumer consumer;
		Require(worker.Start(&consumer), "sustained worker starts");

		std::vector<float> block(callbackSamples);
		std::uint64_t produced = 0;
		while (produced < totalSamples)
		{
			const std::size_t count = static_cast<std::size_t>((std::min)(
				static_cast<std::uint64_t>(callbackSamples), totalSamples - produced));
			block.resize(count);
			FillSequence(block, produced);
			worker.OnAudioSamples(&block[0], block.size(), 48000, false);
			produced += count;

			// This is producer pacing, not UI service. The worker remains the only
			// queue consumer while the simulated UI is absent for the full run.
			while (worker.Snapshot().queueDepth > 16) Sleep(1);
		}
		Require(worker.Stop(true), "sustained worker drains and stops");
		const pdw::signal::AudioDecodeWorkerMetrics metrics = worker.Snapshot();
		Require(metrics.droppedBlocks == 0, "sustained rate has no dropped blocks");
		Require(metrics.droppedSamples == 0, "sustained rate has no dropped samples");
		Require(metrics.decodedSamples == totalSamples, "all sustained samples decoded");
		Require(consumer.samples() == totalSamples, "consumer received every sustained sample");
		Require(consumer.sequenceValid(), "sample order and values are unchanged");
		Require(consumer.discontinuities() == 1, "only startup marks a discontinuity");
		Require(metrics.queueHighWater <= metrics.queueCapacity, "queue remains bounded");
		Require(metrics.preallocatedBytes == 64u * 4096u * sizeof(float),
			"sample memory is fixed and accounted");
		Require(metrics.currentOwnedHandles == 0, "worker owns no handles after stop");
		Require(metrics.handlesCreated == metrics.handlesClosed,
			"worker handle accounting balances after stop");
	}

	void ExerciseExplicitBackpressureAndDiscontinuity()
	{
		pdw::signal::BoundedAudioDecodeWorker worker(4, 64);
		BlockingConsumer consumer;
		Require(worker.Start(&consumer), "backpressure worker starts");
		std::vector<float> block(64, 0.25f);
		worker.OnAudioSamples(&block[0], block.size(), 48000, false);
		Require(consumer.WaitUntilEntered(), "consumer blocks on its first block");

		for (int index = 0; index < 6; ++index)
			worker.OnAudioSamples(&block[0], block.size(), 48000, false);
		pdw::signal::AudioDecodeWorkerMetrics full = worker.Snapshot();
		Require(full.queueDepth <= full.queueCapacity, "full queue never exceeds capacity");
		Require(full.droppedBlocks > 0 && full.droppedSamples > 0,
			"full queue reports explicit drops");

		consumer.Release();
		while (worker.Snapshot().queueDepth >= worker.Snapshot().queueCapacity) Sleep(1);
		worker.OnAudioSamples(&block[0], block.size(), 48000, false);
		Require(worker.Stop(true), "backpressure worker drains and stops");
		Require(consumer.discontinuities() >= 2,
			"first block after a drop is explicitly discontinuous");
	}

	void ExerciseCleanRestartAndResourceBounds()
	{
		DWORD handlesBefore = 0;
		GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
		pdw::signal::BoundedAudioDecodeWorker worker(8, 256);
		std::vector<float> block(256);
		std::uint64_t sequence = 0;
		for (int cycle = 0; cycle < 25; ++cycle)
		{
			CountingConsumer consumer;
			Require(worker.Start(&consumer), "restart cycle starts");
			FillSequence(block, 0);
			worker.OnAudioSamples(&block[0], block.size(), 48000, false);
			Require(worker.Stop(true), "restart cycle stops cleanly");
			Require(consumer.samples() == block.size(), "restart cycle drains its block");
			sequence += block.size();
		}
		const pdw::signal::AudioDecodeWorkerMetrics metrics = worker.Snapshot();
		Require(metrics.threadStarts == 25 && metrics.threadStops == 25,
			"thread lifecycle balances across restarts");
		Require(metrics.handlesCreated == metrics.handlesClosed,
			"event and thread handles balance across restarts");
		Require(metrics.currentOwnedHandles == 0, "restart leaves no owned handles");
		Require(metrics.decodedSamples == sequence, "restart accounting retains decoded total");

		DWORD handlesAfter = 0;
		GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
		Require(handlesAfter <= handlesBefore + 1,
			"process handle count remains bounded after restart soak");
	}

	void ExerciseIndependentServiceClock()
	{
		pdw::signal::BoundedAudioDecodeWorker worker(4, 64);
		CountingConsumer consumer;
		Require(worker.Start(&consumer), "service-clock worker starts");
		Sleep(260);
		Require(worker.Stop(true), "service-clock worker stops");
		Require(consumer.ticks() >= 2,
			"decoder service clock advances without any UI timer or audio callback");
	}
}

int main()
{
	ExerciseSustainedRtlRateWithoutUiService();
	ExerciseExplicitBackpressureAndDiscontinuity();
	ExerciseCleanRestartAndResourceBounds();
	ExerciseIndependentServiceClock();
	std::puts("Bounded audio decode worker tests passed");
	return 0;
}
