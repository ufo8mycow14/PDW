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
	: address(0), messageNumber(0), messageType(0), fragmentNumber(3),
	  continuation(false), observedAtMs(0),
	  text(NULL), colors(NULL), length(0)
{
}

FragmentResult::FragmentResult()
	: status(FRAGMENT_INVALID), assembled(false), truncated(false)
{
}

bool ShouldHoldOriginalFragment(FragmentStatus status)
{
	return status == FRAGMENT_BUFFERED_START ||
		status == FRAGMENT_BUFFERED_CONTINUATION ||
		status == FRAGMENT_BUFFERED_OUT_OF_ORDER ||
		status == FRAGMENT_DUPLICATE ||
		status == FRAGMENT_ASSEMBLED;
}

FragmentReassembler::Slot::Part::Part()
	: present(false), fragmentNumber(3), continuation(false), observedAtMs(0)
{
}

FragmentReassembler::Slot::Slot()
	: active(false), address(0), messageNumber(0), messageType(0),
	  observedAtMs(0), hasStart(false), nextExpectedFragment(0), truncated(false)
{
}

FragmentReassembler::Completion::Completion()
	: address(0), messageNumber(0), messageType(0), observedAtMs(0)
{
}

FragmentReassembler::FragmentReassembler(
	std::size_t maximumSlots,
	std::uint64_t timeoutMs,
	std::size_t maximumBytes,
	std::size_t maximumCompletedIdentities)
	: slots_(maximumSlots), completionQuarantineSaturated_(false),
	  maximumCompletedIdentities_(maximumCompletedIdentities),
	  timeoutMs_(timeoutMs), maximumBytes_(maximumBytes)
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

	// A completed FLEX identity remains quarantined for the fragment timeout.
	// Original fragments have already followed the legacy path; suppressing the
	// shadow observer here prevents trailing/replayed parts from poisoning a new
	// speculative chain or producing a second assembled copy.
	if (WasRecentlyCompleted(observation.address, observation.messageNumber,
		observation.messageType))
	{
		result.status = FRAGMENT_DUPLICATE;
		return result;
	}

	if (completionQuarantineSaturated_)
	{
		result.status = FRAGMENT_CAPACITY_REACHED;
		return result;
	}

	std::size_t slotIndex = Find(observation.address, observation.messageNumber,
		observation.messageType);

	// F=11,C=1 is the only valid start. Message number N is the protocol's
	// fragment identity, so interleaved traffic for an address cannot collide.
	if (observation.continuation && observation.fragmentNumber == 3)
	{
		if (slotIndex == kNoSlot) slotIndex = FindFree();
		if (slotIndex == kNoSlot)
		{
			result.status = FRAGMENT_CAPACITY_REACHED;
			return result;
		}

		Slot& slot = slots_[slotIndex];
		if (slot.active && slot.hasStart)
		{
			if (Matches(slot.start, observation))
			{
				result.status = FRAGMENT_DUPLICATE;
				return result;
			}
			Clear(slotIndex);
			Slot& replacement = slots_[slotIndex];
			replacement.active = true;
			replacement.address = observation.address;
			replacement.messageNumber = observation.messageNumber;
			replacement.messageType = observation.messageType;
			replacement.observedAtMs = observation.observedAtMs;
			replacement.hasStart = true;
			replacement.nextExpectedFragment = 0;
			replacement.displayAddress = observation.displayAddress;
			replacement.displayTime = observation.displayTime;
			replacement.displayDate = observation.displayDate;
			replacement.displayMode = observation.displayMode;
			replacement.displayMessageType = observation.displayMessageType;
			replacement.displayBitrate = observation.displayBitrate;
			const Slot::Part start = MakePart(observation);
			replacement.start = start;
			Append(replacement, start);
			result.status = FRAGMENT_CONFLICT;
			return result;
		}

		slot.active = true;
		slot.address = observation.address;
		slot.messageNumber = observation.messageNumber;
		slot.messageType = observation.messageType;
		slot.observedAtMs = observation.observedAtMs;
		slot.hasStart = true;
		slot.nextExpectedFragment = 0;
		slot.displayAddress = observation.displayAddress;
		slot.displayTime = observation.displayTime;
		slot.displayDate = observation.displayDate;
		slot.displayMode = observation.displayMode;
		slot.displayMessageType = observation.displayMessageType;
		slot.displayBitrate = observation.displayBitrate;
		const Slot::Part start = MakePart(observation);
		slot.start = start;
		Append(slot, start);

		while (slot.pending[slot.nextExpectedFragment].present)
		{
			const Slot::Part pending = slot.pending[slot.nextExpectedFragment];
			slot.pending[slot.nextExpectedFragment] = Slot::Part();
			slot.accepted[pending.fragmentNumber] = pending;
			Append(slot, pending);
			slot.observedAtMs = (std::max)(slot.observedAtMs, pending.observedAtMs);
			if (!pending.continuation)
			{
				if (HasPendingParts(slot))
				{
					Clear(slotIndex);
					result.status = FRAGMENT_CONFLICT;
					return result;
				}
				return Complete(slotIndex);
			}
			slot.nextExpectedFragment = (pending.fragmentNumber + 1) % 3;
		}
		result.status = FRAGMENT_BUFFERED_START;
		result.truncated = slot.truncated;
		return result;
	}

	if (slotIndex == kNoSlot)
	{
		slotIndex = FindFree();
		if (slotIndex == kNoSlot)
		{
			result.status = FRAGMENT_CAPACITY_REACHED;
			return result;
		}
		Slot& orphan = slots_[slotIndex];
		orphan.active = true;
		orphan.address = observation.address;
		orphan.messageNumber = observation.messageNumber;
		orphan.messageType = observation.messageType;
		orphan.observedAtMs = observation.observedAtMs;
		orphan.pending[observation.fragmentNumber] = MakePart(observation);
		result.status = FRAGMENT_BUFFERED_OUT_OF_ORDER;
		return result;
	}

	Slot& slot = slots_[slotIndex];
	if (Matches(slot.pending[observation.fragmentNumber], observation))
	{
		result.status = FRAGMENT_DUPLICATE;
		return result;
	}

	if (slot.pending[observation.fragmentNumber].present)
	{
		Clear(slotIndex);
		result.status = FRAGMENT_CONFLICT;
		return result;
	}

	if (slot.hasStart && observation.fragmentNumber == slot.nextExpectedFragment)
	{
		// Fragment numbers wrap 0,1,2 for long pages.  An in-sequence part can
		// legitimately have the same number and payload as an earlier cycle, so
		// accepted-part duplicate detection must not run on the expected slot.
		return AcceptAndDrain(slotIndex, observation);
	}

	if (WasAccepted(slot, observation))
	{
		result.status = FRAGMENT_DUPLICATE;
		return result;
	}

	slot.pending[observation.fragmentNumber] = MakePart(observation);
	slot.observedAtMs = (std::max)(slot.observedAtMs, observation.observedAtMs);
	result.status = FRAGMENT_BUFFERED_OUT_OF_ORDER;
	return result;
}

void FragmentReassembler::Reset()
{
	for (std::size_t index = 0; index < slots_.size(); ++index) Clear(index);
	completions_.clear();
	completionQuarantineSaturated_ = false;
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
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (slots_[index].active && slots_[index].address == address) return true;
	return false;
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
	completions_.erase(
		std::remove_if(completions_.begin(), completions_.end(),
			[nowMs, this](const Completion& completion)
			{
				return nowMs >= completion.observedAtMs &&
					nowMs - completion.observedAtMs > timeoutMs_;
			}),
		completions_.end());
	if (completions_.size() < maximumCompletedIdentities_)
		completionQuarantineSaturated_ = false;
}

std::size_t FragmentReassembler::Find(std::uint32_t address,
	unsigned int messageNumber, unsigned int messageType) const
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (slots_[index].active && slots_[index].address == address &&
			slots_[index].messageNumber == messageNumber &&
			slots_[index].messageType == messageType) return index;
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

void FragmentReassembler::Append(Slot& slot, const Slot::Part& part)
{
	const std::size_t remaining = slot.text.size() < maximumBytes_
		? maximumBytes_ - slot.text.size()
		: 0;
	const std::size_t appendLength = (std::min)(remaining, part.text.size());
	if (appendLength != 0)
	{
		slot.text.append(part.text.data(), appendLength);
		if (!part.colors.empty())
		{
			slot.colors.insert(slot.colors.end(), part.colors.begin(),
				part.colors.begin() + appendLength);
		}
		else
		{
			slot.colors.insert(slot.colors.end(), appendLength, 0);
		}
	}
	if (appendLength < part.text.size()) slot.truncated = true;
}

FragmentReassembler::Slot::Part FragmentReassembler::MakePart(
	const FragmentObservation& observation)
{
	Slot::Part part;
	part.present = true;
	part.fragmentNumber = observation.fragmentNumber;
	part.continuation = observation.continuation;
	part.observedAtMs = observation.observedAtMs;
	if (observation.length != 0)
	{
		part.text.assign(reinterpret_cast<const char*>(observation.text), observation.length);
		if (observation.colors != NULL)
			part.colors.assign(observation.colors, observation.colors + observation.length);
	}
	return part;
}

bool FragmentReassembler::Matches(const Slot::Part& part,
	const FragmentObservation& observation)
{
	if (!part.present || part.fragmentNumber != observation.fragmentNumber ||
		part.continuation != observation.continuation ||
		part.text.size() != observation.length) return false;
	return observation.length == 0 ||
		std::equal(part.text.begin(), part.text.end(),
			reinterpret_cast<const char*>(observation.text));
}

bool FragmentReassembler::HasPendingParts(const Slot& slot)
{
	return slot.pending[0].present || slot.pending[1].present ||
		slot.pending[2].present;
}

bool FragmentReassembler::WasAccepted(const Slot& slot,
	const FragmentObservation& observation)
{
	return observation.fragmentNumber < 3 &&
		Matches(slot.accepted[observation.fragmentNumber], observation);
}

bool FragmentReassembler::WasRecentlyCompleted(std::uint32_t address,
	unsigned int messageNumber, unsigned int messageType) const
{
	for (std::size_t index = 0; index < completions_.size(); ++index)
	{
		const Completion& completion = completions_[index];
		if (completion.address == address &&
			completion.messageNumber == messageNumber &&
			completion.messageType == messageType) return true;
	}
	return false;
}

bool FragmentReassembler::RememberCompletion(const Slot& slot)
{
	// Do not evict a still-quarantined identity. If the bounded completion
	// cache is full, fail closed and leave the original fragment stream as the
	// only output rather than emit an assembled copy that cannot be replay-safe.
	if (completions_.size() >= maximumCompletedIdentities_)
	{
		completionQuarantineSaturated_ = true;
		return false;
	}

	Completion completion;
	completion.address = slot.address;
	completion.messageNumber = slot.messageNumber;
	completion.messageType = slot.messageType;
	completion.observedAtMs = slot.observedAtMs;
	completions_.push_back(completion);
	return true;
}

FragmentResult FragmentReassembler::AcceptAndDrain(std::size_t slotIndex,
	const FragmentObservation& observation)
{
	FragmentResult result;
	Slot& slot = slots_[slotIndex];
	Slot::Part part = MakePart(observation);
	for (;;)
	{
		slot.accepted[part.fragmentNumber] = part;
		Append(slot, part);
		slot.observedAtMs = (std::max)(slot.observedAtMs, part.observedAtMs);
		if (!part.continuation)
		{
			if (HasPendingParts(slot))
			{
				Clear(slotIndex);
				result.status = FRAGMENT_CONFLICT;
				return result;
			}
			return Complete(slotIndex);
		}

		slot.nextExpectedFragment = (part.fragmentNumber + 1) % 3;
		if (!slot.pending[slot.nextExpectedFragment].present) break;
		part = slot.pending[slot.nextExpectedFragment];
		slot.pending[slot.nextExpectedFragment] = Slot::Part();
	}

	result.status = FRAGMENT_BUFFERED_CONTINUATION;
	result.truncated = slot.truncated;
	return result;
}

FragmentResult FragmentReassembler::Complete(std::size_t slotIndex)
{
	FragmentResult result;
	Slot& slot = slots_[slotIndex];
	if (!RememberCompletion(slot))
	{
		for (std::size_t index = 0; index < slots_.size(); ++index) Clear(index);
		result.status = FRAGMENT_CAPACITY_REACHED;
		return result;
	}
	result.status = FRAGMENT_ASSEMBLED;
	result.assembled = true;
	result.truncated = slot.truncated;
	result.displayAddress = slot.displayAddress;
	result.displayTime = slot.displayTime;
	result.displayDate = slot.displayDate;
	result.displayMode = slot.displayMode;
	result.displayMessageType = slot.displayMessageType;
	result.displayBitrate = slot.displayBitrate;
	result.text.swap(slot.text);
	result.colors.swap(slot.colors);
	Clear(slotIndex);
	return result;
}

} // namespace flex
} // namespace pdw
