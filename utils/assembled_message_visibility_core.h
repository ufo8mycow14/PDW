#ifndef PDW_ASSEMBLED_MESSAGE_VISIBILITY_CORE_H
#define PDW_ASSEMBLED_MESSAGE_VISIBILITY_CORE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdw
{
namespace assembled
{

class VisibilityGuard
{
public:
	explicit VisibilityGuard(std::size_t maximumEntries = 64,
		std::uint64_t timeoutMs = 120000);

	// Ordinary messages are remembered but never suppressed. An assembled
	// message is suppressed only when its exact payload and type were already
	// accepted recently, even if the repeat arrived on another capcode.
	bool ShouldSuppress(bool assembled, const std::string& text,
		const std::string& messageType, std::uint64_t observedAtMs);
	void Reset();

private:
	struct Entry
	{
		std::string text;
		std::string messageType;
		std::uint64_t observedAtMs;
	};

	void Expire(std::uint64_t nowMs);
	std::vector<Entry> entries_;
	std::size_t maximumEntries_;
	std::uint64_t timeoutMs_;
};

} // namespace assembled
} // namespace pdw

#endif
