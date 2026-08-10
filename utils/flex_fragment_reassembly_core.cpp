#include "flex_fragment_reassembly_core.h"

#include <algorithm>
#include <limits>

namespace pdw
{
namespace flex
{

namespace
{
	const std::size_t kNoSlot = (std::numeric_limits<std::size_t>::max)();
}

FragmentObservation::FragmentObservation()
	: address(0), fragmentNumber(3), continuation(false), observedAtMs(0),
	  text(NULL), colors(NULL), length(0)
{
}

FragmentResult::FragmentResult()
	: status(FRAGMENT_INVALID), assembled(false), truncated(false)
{
}

FragmentReassembler::Slot::Slot()
	: active(false), address(0), observedAtMs(0), nextExpectedFragment(0),
	  truncated(false)
{
}

FragmentReassembler::FragmentReassembler(
	std::size_t maximumSlots,
	std::uint64_t timeoutMs,
	std::size_t maximumBytes)
	: slots_(maximumSlots), timeoutMs_(timeoutMs), maximumBytes_(maximumBytes)
{
}

FragmentResult FragmentReassembler::Observe(const FragmentObservation& observation)
{
	FragmentResult result;
	if (observation.fragmentNumber > 3 ||
		(observation.length != 0 && observation.text == NULL))
	{
		return result;
	}

	Expire(observation.observedAtMs);

	// K: a complete message. An unrelated valid message for the same address
	// does not destroy a pending chain; this matches the FLEX classification and
	// avoids making the additive feature suppress any later valid assembly.
	if (!observation.continuation && observation.fragmentNumber == 3)
	{
		result.status = FRAGMENT_STANDALONE;
		return result;
	}

	std::size_t slotIndex = Find(observation.address);

	// F=11,C=1 is the only valid start. A repeat cleanly restarts that address.
	if (observation.continuation && observation.fragmentNumber == 3)
	{
		if (slotIndex == kNoSlot) slotIndex = FindFree();
		if (slotIndex == kNoSlot)
		{
			result.status = FRAGMENT_CAPACITY_REACHED;
			return result;
		}

		Clear(slotIndex);
		Slot& slot = slots_[slotIndex];
		slot.active = true;
		slot.address = observation.address;
		slot.observedAtMs = observation.observedAtMs;
		slot.nextExpectedFragment = 0;
		Append(slot, observation);
		result.status = FRAGMENT_BUFFERED_START;
		result.truncated = slot.truncated;
		return result;
	}

	if (slotIndex == kNoSlot)
	{
		result.status = FRAGMENT_ORPHAN;
		return result;
	}

	Slot& slot = slots_[slotIndex];
	if (observation.fragmentNumber != slot.nextExpectedFragment)
	{
		Clear(slotIndex);
		result.status = FRAGMENT_SEQUENCE_ERROR;
		return result;
	}

	Append(slot, observation);
	slot.observedAtMs = observation.observedAtMs;

	if (observation.continuation)
	{
		slot.nextExpectedFragment = (observation.fragmentNumber + 1) % 3;
		result.status = FRAGMENT_BUFFERED_CONTINUATION;
		result.truncated = slot.truncated;
		return result;
	}

	result.status = FRAGMENT_ASSEMBLED;
	result.assembled = true;
	result.truncated = slot.truncated;
	result.text.swap(slot.text);
	result.colors.swap(slot.colors);
	Clear(slotIndex);
	return result;
}

void FragmentReassembler::Reset()
{
	for (std::size_t index = 0; index < slots_.size(); ++index) Clear(index);
}

std::size_t FragmentReassembler::ActiveCount() const
{
	std::size_t active = 0;
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (slots_[index].active) ++active;
	return active;
}

bool FragmentReassembler::HasPending(std::uint32_t address) const
{
	return Find(address) != kNoSlot;
}

void FragmentReassembler::Expire(std::uint64_t nowMs)
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
	{
		const Slot& slot = slots_[index];
		if (slot.active && nowMs >= slot.observedAtMs &&
			nowMs - slot.observedAtMs > timeoutMs_)
		{
			Clear(index);
		}
	}
}

std::size_t FragmentReassembler::Find(std::uint32_t address) const
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (slots_[index].active && slots_[index].address == address) return index;
	return kNoSlot;
}

std::size_t FragmentReassembler::FindFree() const
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (!slots_[index].active) return index;
	return kNoSlot;
}

void FragmentReassembler::Clear(std::size_t index)
{
	if (index >= slots_.size()) return;
	slots_[index] = Slot();
}

void FragmentReassembler::Append(Slot& slot, const FragmentObservation& observation)
{
	const std::size_t remaining = slot.text.size() < maximumBytes_
		? maximumBytes_ - slot.text.size()
		: 0;
	const std::size_t appendLength = (std::min)(remaining, observation.length);
	if (appendLength != 0)
	{
		slot.text.append(reinterpret_cast<const char*>(observation.text), appendLength);
		if (observation.colors != NULL)
		{
			slot.colors.insert(slot.colors.end(), observation.colors,
				observation.colors + appendLength);
		}
		else
		{
			slot.colors.insert(slot.colors.end(), appendLength, 0);
		}
	}
	if (appendLength < observation.length) slot.truncated = true;
}

} // namespace flex
} // namespace pdw
