#include "assembled_message_visibility_core.h"

namespace pdw
{
namespace assembled
{

VisibilityGuard::VisibilityGuard(std::size_t maximumEntries,
	std::uint64_t timeoutMs)
	: maximumEntries_(maximumEntries), timeoutMs_(timeoutMs)
{
}

bool VisibilityGuard::ShouldSuppress(bool assembled, const std::string& text,
	const std::string& messageType, std::uint64_t observedAtMs)
{
	Expire(observedAtMs);
	if (assembled)
	{
		for (std::size_t index = 0; index < entries_.size(); ++index)
		{
			const Entry& entry = entries_[index];
			if (entry.text == text && entry.messageType == messageType) return true;
		}
	}

	if (maximumEntries_ == 0) return false;
	if (entries_.size() >= maximumEntries_) entries_.erase(entries_.begin());
	Entry entry;
	entry.text = text;
	entry.messageType = messageType;
	entry.observedAtMs = observedAtMs;
	entries_.push_back(entry);
	return false;
}

void VisibilityGuard::Reset()
{
	entries_.clear();
}

void VisibilityGuard::Expire(std::uint64_t nowMs)
{
	std::vector<Entry>::iterator entry = entries_.begin();
	while (entry != entries_.end())
	{
		if (nowMs >= entry->observedAtMs &&
			nowMs - entry->observedAtMs >= timeoutMs_)
			entry = entries_.erase(entry);
		else ++entry;
	}
}

} // namespace assembled
} // namespace pdw
