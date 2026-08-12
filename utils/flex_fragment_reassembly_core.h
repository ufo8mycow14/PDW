#ifndef PDW_FLEX_FRAGMENT_REASSEMBLY_CORE_H
#define PDW_FLEX_FRAGMENT_REASSEMBLY_CORE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdw
{
namespace flex
{

enum FragmentStatus
{
	FRAGMENT_INVALID = 0,
	FRAGMENT_STANDALONE,
	FRAGMENT_BUFFERED_START,
	FRAGMENT_BUFFERED_CONTINUATION,
	FRAGMENT_BUFFERED_OUT_OF_ORDER,
	FRAGMENT_ASSEMBLED,
	FRAGMENT_DUPLICATE,
	FRAGMENT_CONFLICT,
	FRAGMENT_ORPHAN,
	FRAGMENT_SEQUENCE_ERROR,
	FRAGMENT_CAPACITY_REACHED
};

struct FragmentObservation
{
	std::uint32_t address;
	unsigned int messageNumber;
	unsigned int messageType;
	unsigned int fragmentNumber;
	bool continuation;
	std::uint64_t observedAtMs;
	std::string displayAddress;
	std::string displayTime;
	std::string displayDate;
	std::string displayMode;
	std::string displayMessageType;
	std::string displayBitrate;
	const unsigned char* text;
	const std::uint8_t* colors;
	std::size_t length;

	FragmentObservation();
};

struct FragmentResult
{
	FragmentStatus status;
	bool assembled;
	bool truncated;
	std::string displayAddress;
	std::string displayTime;
	std::string displayDate;
	std::string displayMode;
	std::string displayMessageType;
	std::string displayBitrate;
	std::string text;
	std::vector<std::uint8_t> colors;

	FragmentResult();
};

// When reassembly is enabled, these statuses represent a valid chain that is
// still pending, replayed, or complete. Other statuses retain the direct path.
bool ShouldHoldOriginalFragment(FragmentStatus status);

// A bounded reassembler for standard FLEX K/F/C alpha fragments. The caller
// decides whether each observation is held or retains the established direct
// decoder path by applying ShouldHoldOriginalFragment to the result status.
class FragmentReassembler
{
public:
	explicit FragmentReassembler(
		std::size_t maximumSlots = 16,
		std::uint64_t timeoutMs = 120000,
		std::size_t maximumBytes = 5119,
		std::size_t maximumCompletedIdentities = 256);

	FragmentResult Observe(const FragmentObservation& observation);
	void Reset();
	std::size_t ActiveCount() const;
	bool HasPending(std::uint32_t address) const;

private:
	struct Slot
	{
		bool active;
		struct Part
		{
			bool present;
			unsigned int fragmentNumber;
			bool continuation;
			std::uint64_t observedAtMs;
			std::string text;
			std::vector<std::uint8_t> colors;

			Part();
		};

		std::uint32_t address;
		unsigned int messageNumber;
		unsigned int messageType;
		std::uint64_t observedAtMs;
		bool hasStart;
		unsigned int nextExpectedFragment;
		bool truncated;
		std::string displayAddress;
		std::string displayTime;
		std::string displayDate;
		std::string displayMode;
		std::string displayMessageType;
		std::string displayBitrate;
		std::string text;
		std::vector<std::uint8_t> colors;
		Part start;
		Part accepted[3];
		Part pending[3];

		Slot();
	};
	struct Completion
	{
		std::uint32_t address;
		unsigned int messageNumber;
		unsigned int messageType;
		std::uint64_t observedAtMs;

		Completion();
	};

	void Expire(std::uint64_t nowMs);
	std::size_t Find(std::uint32_t address, unsigned int messageNumber,
		unsigned int messageType) const;
	std::size_t FindFree() const;
	void Clear(std::size_t index);
	void Append(Slot& slot, const Slot::Part& part);
	static Slot::Part MakePart(const FragmentObservation& observation);
	static bool Matches(const Slot::Part& part,
		const FragmentObservation& observation);
	static bool HasPendingParts(const Slot& slot);
	static bool WasAccepted(const Slot& slot,
		const FragmentObservation& observation);
	bool WasRecentlyCompleted(std::uint32_t address,
		unsigned int messageNumber, unsigned int messageType) const;
	bool RememberCompletion(const Slot& slot);
	FragmentResult AcceptAndDrain(std::size_t slotIndex,
		const FragmentObservation& observation);
	FragmentResult Complete(std::size_t slotIndex);

	std::vector<Slot> slots_;
	std::vector<Completion> completions_;
	bool completionQuarantineSaturated_;
	std::size_t maximumCompletedIdentities_;
	std::uint64_t timeoutMs_;
	std::size_t maximumBytes_;
};

} // namespace flex
} // namespace pdw

#endif
