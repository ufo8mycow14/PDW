#include "filter_match_core.h"

#include <cctype>
#include <cstring>

namespace pdw
{
namespace filters
{

bool IsValidRequiredTermsExpression(const char* expression, int maximumTerms)
{
	if (!expression || maximumTerms < 2) return false;
	if (!std::strchr(expression, '+')) return true;
	int terms = 0;
	const char* start = expression;
	for (;;)
	{
		const char* separator = std::strchr(start, '+');
		const char* end = separator ? separator : start + std::strlen(start);
		while (start < end && std::isspace(static_cast<unsigned char>(*start))) ++start;
		while (end > start && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
		if (start == end || ++terms > maximumTerms) return false;
		if (!separator) break;
		start = separator + 1;
	}
	return terms >= 2;
}

bool ExpandLegacyLabel(const char* label, const char* capcode,
	char* output, std::size_t outputCapacity)
{
	if (!output || !outputCapacity) return false;
	output[0] = '\0';
	if (!label || !capcode) return false;
	const std::size_t capcodeLength = std::strlen(capcode);
	std::size_t outputLength = 0;
	bool complete = true;
	const char* position = label;
	while (*position)
	{
		if (*position == '%' && position[1] >= '1' && position[1] <= '9' &&
			static_cast<std::size_t>(position[1] - '1') < capcodeLength)
		{
			++position;
			while (*position >= '1' && *position <= '9')
			{
				const std::size_t capcodePosition =
					static_cast<std::size_t>(*position - '1');
				if (capcodePosition >= capcodeLength) break;
				if (outputLength + 1 < outputCapacity)
					output[outputLength++] = capcode[capcodePosition];
				else complete = false;
				++position;
			}
			continue;
		}
		if (outputLength + 1 < outputCapacity)
			output[outputLength++] = *position;
		else complete = false;
		++position;
	}
	output[outputLength] = '\0';
	return complete;
}

int MatchRequiredTerms(const char* message, const char* expression,
	int* positions, int* lengths, int capacity)
{
	if (!message || !expression || !positions || !lengths || capacity < 2 ||
		!std::strchr(expression, '+') ||
		!IsValidRequiredTermsExpression(expression, capacity)) return 0;
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
