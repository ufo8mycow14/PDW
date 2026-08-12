#include "multipart_message_reassembly_core.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace pdw
{
namespace multipart
{

namespace
{
	const std::size_t kNoSlot = (std::numeric_limits<std::size_t>::max)();

	struct Marker
	{
		unsigned int part;
		unsigned int total;
		std::size_t begin;
		std::size_t end;

		Marker() : part(0), total(0), begin(0), end(0) {}
	};

	bool IsWord(unsigned char value)
	{
		return std::isalnum(value) != 0 || value == '_';
	}

	bool EqualsAscii(const unsigned char* text, std::size_t length,
		std::size_t offset, const char* expected)
	{
		for (std::size_t index = 0; expected[index] != 0; ++index)
		{
			if (offset + index >= length) return false;
			if (std::tolower(text[offset + index]) !=
				std::tolower(static_cast<unsigned char>(expected[index]))) return false;
		}
		return true;
	}

	void SkipSpaces(const unsigned char* text, std::size_t length, std::size_t& offset)
	{
		while (offset < length && std::isspace(text[offset])) ++offset;
	}

	bool ReadNumber(const unsigned char* text, std::size_t length,
		std::size_t& offset, unsigned int& value)
	{
		const std::size_t start = offset;
		value = 0;
		while (offset < length && std::isdigit(text[offset]))
		{
			if (value > 999) return false;
			value = value * 10 + static_cast<unsigned int>(text[offset] - '0');
			++offset;
		}
		return offset != start;
	}

	bool ParseMarker(const unsigned char* text, std::size_t length, Marker& marker)
	{
		if (text == NULL || length == 0) return false;
		for (std::size_t start = 0; start + 4 <= length; ++start)
		{
			if (start != 0 && IsWord(text[start - 1])) continue;
			if (!EqualsAscii(text, length, start, "part")) continue;
			if (start + 4 < length && IsWord(text[start + 4])) continue;

			std::size_t offset = start + 4;
			SkipSpaces(text, length, offset);
			if (offset < length && text[offset] == '#')
			{
				++offset;
				SkipSpaces(text, length, offset);
			}
			unsigned int part = 0;
			if (!ReadNumber(text, length, offset, part)) continue;
			SkipSpaces(text, length, offset);
			if (!EqualsAscii(text, length, offset, "of")) continue;
			offset += 2;
			if (offset < length && IsWord(text[offset])) continue;
			SkipSpaces(text, length, offset);
			unsigned int total = 0;
			if (!ReadNumber(text, length, offset, total)) continue;
			if (offset < length && IsWord(text[offset])) continue;

			marker.part = part;
			marker.total = total;
			marker.begin = start;
			marker.end = offset;

			if (marker.begin != 0 &&
				(text[marker.begin - 1] == '[' || text[marker.begin - 1] == '(' ||
				 text[marker.begin - 1] == '{')) --marker.begin;
			if (marker.end < length &&
				(text[marker.end] == ']' || text[marker.end] == ')' ||
				 text[marker.end] == '}')) ++marker.end;
			return true;
		}
		return false;
	}

	bool IsSeparator(unsigned char value)
	{
		return value == ':' || value == '-' || value == '|' || value == '/';
	}

	void TrimRange(const unsigned char* text, std::size_t& begin, std::size_t& end)
	{
		while (begin < end && std::isspace(text[begin])) ++begin;
		while (end > begin && std::isspace(text[end - 1])) --end;
	}

	void PreparePart(const MultipartObservation& observation, const Marker& marker,
		std::string& preparedText, std::vector<std::uint8_t>& preparedColors)
	{
		std::size_t leftBegin = 0;
		std::size_t leftEnd = marker.begin;
		std::size_t rightBegin = marker.end;
		std::size_t rightEnd = observation.length;
		TrimRange(observation.text, leftBegin, leftEnd);
		TrimRange(observation.text, rightBegin, rightEnd);
		while (leftEnd > leftBegin && IsSeparator(observation.text[leftEnd - 1]))
		{
			--leftEnd;
			while (leftEnd > leftBegin && std::isspace(observation.text[leftEnd - 1])) --leftEnd;
		}
		while (rightBegin < rightEnd && IsSeparator(observation.text[rightBegin]))
		{
			++rightBegin;
			while (rightBegin < rightEnd && std::isspace(observation.text[rightBegin])) ++rightBegin;
		}

		if (leftEnd > leftBegin)
		{
			preparedText.append(reinterpret_cast<const char*>(observation.text + leftBegin),
				leftEnd - leftBegin);
			if (observation.colors != NULL)
				preparedColors.insert(preparedColors.end(), observation.colors + leftBegin,
					observation.colors + leftEnd);
		}
		if (rightEnd > rightBegin)
		{
			if (!preparedText.empty())
			{
				preparedText.push_back(' ');
				if (observation.colors != NULL)
					preparedColors.push_back(observation.colors[rightBegin]);
			}
			preparedText.append(reinterpret_cast<const char*>(observation.text + rightBegin),
				rightEnd - rightBegin);
			if (observation.colors != NULL)
				preparedColors.insert(preparedColors.end(), observation.colors + rightBegin,
					observation.colors + rightEnd);
		}
	}
}

MultipartObservation::MultipartObservation()
	: observedAtMs(0), text(NULL), colors(NULL), length(0)
{
}

MultipartResult::MultipartResult()
	: status(MULTIPART_NOT_RECOGNIZED), assembled(false), truncated(false),
	  partNumber(0), totalParts(0)
{
}

MultipartReassembler::Part::Part() : present(false) {}

MultipartReassembler::Slot::Slot()
	: active(false), totalParts(0), observedAtMs(0)
{
}

MultipartReassembler::MultipartReassembler(std::size_t maximumSlots,
	std::uint64_t timeoutMs, std::size_t maximumBytes, unsigned int maximumParts)
	: slots_(maximumSlots), timeoutMs_(timeoutMs), maximumBytes_(maximumBytes),
	  maximumParts_(maximumParts)
{
}

MultipartResult MultipartReassembler::Observe(const MultipartObservation& observation)
{
	MultipartResult result;
	Marker marker;
	if (!ParseMarker(observation.text, observation.length, marker)) return result;
	result.partNumber = marker.part;
	result.totalParts = marker.total;
	if (marker.total < 2 || marker.total > maximumParts_ || marker.part == 0 ||
		marker.part > marker.total || observation.address.empty())
	{
		result.status = MULTIPART_INVALID;
		return result;
	}

	Expire(observation.observedAtMs);
	Part incoming;
	incoming.present = true;
	incoming.address = observation.address;
	incoming.protocol = observation.protocol;
	incoming.messageType = observation.messageType;
	incoming.displayTime = observation.displayTime;
	incoming.displayDate = observation.displayDate;
	incoming.displayBitrate = observation.displayBitrate;
	PreparePart(observation, marker, incoming.text, incoming.colors);
	std::size_t slotIndex = Find(observation, marker.total);
	if (slotIndex == kNoSlot)
	{
		slotIndex = FindFree();
		if (slotIndex == kNoSlot)
		{
			result.status = MULTIPART_CAPACITY_REACHED;
			return result;
		}
		Slot& slot = slots_[slotIndex];
		slot.active = true;
		slot.address = observation.address;
		slot.protocol = observation.protocol;
		slot.messageType = observation.messageType;
		slot.totalParts = marker.total;
		slot.observedAtMs = observation.observedAtMs;
		slot.parts.assign(marker.total, Part());
	}

	Slot& slot = slots_[slotIndex];
	Part& existing = slot.parts[marker.part - 1];
	if (existing.present)
	{
		if (existing.text == incoming.text)
		{
			result.status = MULTIPART_DUPLICATE;
			return result;
		}
		if (marker.part != 1)
		{
			Clear(slotIndex);
			result.status = MULTIPART_CONFLICT;
			return result;
		}
		// A different Part 1 is the only safe signal that a new message has
		// replaced an incomplete chain with the same visible identity.
		Clear(slotIndex);
		Slot& replacement = slots_[slotIndex];
		replacement.active = true;
		replacement.address = observation.address;
		replacement.protocol = observation.protocol;
		replacement.messageType = observation.messageType;
		replacement.totalParts = marker.total;
		replacement.observedAtMs = observation.observedAtMs;
		replacement.parts.assign(marker.total, Part());
		replacement.parts[0] = incoming;
		result.status = MULTIPART_BUFFERED;
		return result;
	}

	existing = incoming;
	slot.observedAtMs = observation.observedAtMs;
	for (std::size_t index = 0; index < slot.parts.size(); ++index)
		if (!slot.parts[index].present)
		{
			result.status = MULTIPART_BUFFERED;
			return result;
		}
	return Complete(slotIndex);
}

void MultipartReassembler::Reset()
{
	for (std::size_t index = 0; index < slots_.size(); ++index) Clear(index);
}

std::size_t MultipartReassembler::ActiveCount() const
{
	std::size_t active = 0;
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (slots_[index].active) ++active;
	return active;
}

void MultipartReassembler::Expire(std::uint64_t nowMs)
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
	{
		if (!slots_[index].active) continue;
		if (nowMs >= slots_[index].observedAtMs &&
			nowMs - slots_[index].observedAtMs >= timeoutMs_) Clear(index);
	}
}

std::size_t MultipartReassembler::Find(const MultipartObservation& observation,
	unsigned int totalParts) const
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
	{
		const Slot& slot = slots_[index];
		if (slot.active && slot.address == observation.address &&
			slot.protocol == observation.protocol &&
			slot.messageType == observation.messageType &&
			slot.totalParts == totalParts) return index;
	}
	return kNoSlot;
}

std::size_t MultipartReassembler::FindFree() const
{
	for (std::size_t index = 0; index < slots_.size(); ++index)
		if (!slots_[index].active) return index;
	return kNoSlot;
}

void MultipartReassembler::Clear(std::size_t index)
{
	if (index < slots_.size()) slots_[index] = Slot();
}

MultipartResult MultipartReassembler::Complete(std::size_t index)
{
	MultipartResult result;
	if (index >= slots_.size() || !slots_[index].active)
	{
		result.status = MULTIPART_INVALID;
		return result;
	}
	const Slot& slot = slots_[index];
	result.totalParts = slot.totalParts;
	const Part& source = slot.parts[0];
	result.address = source.address;
	result.protocol = source.protocol;
	result.messageType = source.messageType;
	result.displayTime = source.displayTime;
	result.displayDate = source.displayDate;
	result.displayBitrate = source.displayBitrate;
	for (std::size_t partIndex = 0; partIndex < slot.parts.size(); ++partIndex)
	{
		const Part& part = slot.parts[partIndex];
		if (!result.text.empty() && !part.text.empty())
		{
			if (result.text.size() < maximumBytes_)
			{
				result.text.push_back(' ');
				const std::uint8_t color = !part.colors.empty() ? part.colors.front() :
					(!result.colors.empty() ? result.colors.back() : 0);
				result.colors.push_back(color);
			}
			else result.truncated = true;
		}
		const std::size_t available = result.text.size() < maximumBytes_ ?
			maximumBytes_ - result.text.size() : 0;
		const std::size_t copyLength = (std::min)(available, part.text.size());
		result.text.append(part.text.data(), copyLength);
		if (!part.colors.empty())
			result.colors.insert(result.colors.end(), part.colors.begin(),
				part.colors.begin() + (std::min)(copyLength, part.colors.size()));
		while (result.colors.size() < result.text.size())
			result.colors.push_back(result.colors.empty() ? 0 : result.colors.back());
		if (copyLength != part.text.size()) result.truncated = true;
	}
	Clear(index);
	result.status = MULTIPART_ASSEMBLED;
	result.assembled = true;
	return result;
}

} // namespace multipart
} // namespace pdw
