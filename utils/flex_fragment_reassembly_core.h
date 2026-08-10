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
	FRAGMENT_ASSEMBLED,
	FRAGMENT_ORPHAN,
	FRAGMENT_SEQUENCE_ERROR,
	FRAGMENT_CAPACITY_REACHED
};

struct FragmentObservation
{
	std::uint32_t address;
	unsigned int fragmentNumber;
	bool continuation;
	std::uint64_t observedAtMs;
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
	std::string text;
	std::vector<std::uint8_t> colors;

	FragmentResult();
};

// A bounded shadow reassembler for standard FLEX K/F/C alpha fragments.
// It never decides whether a legacy fragment is displayed or discarded; the
// caller remains responsible for preserving the established decoder path.
class FragmentReassembler
{
public:
	explicit FragmentReassembler(
		std::size_t maximumSlots = 16,
		std::uint64_t timeoutMs = 120000,
		std::size_t maximumBytes = 5119);

	FragmentResult Observe(const FragmentObservation& observation);
	void Reset();
	std::size_t ActiveCount() const;
	bool HasPending(std::uint32_t address) const;

private:
	struct Slot
	{
		bool active;
		std::uint32_t address;
		std::uint64_t observedAtMs;
		unsigned int nextExpectedFragment;
		bool truncated;
		std::string text;
		std::vector<std::uint8_t> colors;

		Slot();
	};

	void Expire(std::uint64_t nowMs);
	std::size_t Find(std::uint32_t address) const;
	std::size_t FindFree() const;
	void Clear(std::size_t index);
	void Append(Slot& slot, const FragmentObservation& observation);

	std::vector<Slot> slots_;
	std::uint64_t timeoutMs_;
	std::size_t maximumBytes_;
};

} // namespace flex
} // namespace pdw

#endif
