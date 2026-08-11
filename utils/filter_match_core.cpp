#include "filter_match_core.h"

#include <cctype>
#include <cstring>

namespace pdw
{
namespace filters
{

int MatchRequiredTerms(const char* message, const char* expression,
	int* positions, int* lengths, int capacity)
{
	if (!message || !expression || !positions || !lengths || capacity < 2 ||
		!std::strchr(expression, '+')) return 0;
	const int messageLength = static_cast<int>(std::strlen(message));
	int termCount = 0;
	const char* start = expression;
	for (;;)
	{
		const char* separator = std::strchr(start, '+');
		const char* end = separator ? separator : start + std::strlen(start);
		while (start < end && std::isspace(static_cast<unsigned char>(*start))) ++start;
		while (end > start && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
		const int termLength = static_cast<int>(end - start);
		if (!termLength || termCount >= capacity) return 0;
		int found = -1;
		for (int position = 0; position + termLength <= messageLength; ++position)
		{
			bool equal = true;
			for (int index = 0; index < termLength; ++index)
			{
				const unsigned char left = static_cast<unsigned char>(message[position + index]);
				const unsigned char right = static_cast<unsigned char>(start[index]);
				if (std::tolower(left) != std::tolower(right)) { equal = false; break; }
			}
			if (equal) { found = position; break; }
		}
		if (found < 0) return 0;
		positions[termCount] = found;
		lengths[termCount] = termLength;
		++termCount;
		if (!separator) break;
		start = separator + 1;
	}
	return termCount >= 2 ? termCount : 0;
}

} // namespace filters
} // namespace pdw
