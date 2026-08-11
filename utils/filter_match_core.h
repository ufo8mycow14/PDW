#ifndef PDW_FILTER_MATCH_CORE_H
#define PDW_FILTER_MATCH_CORE_H

namespace pdw
{
namespace filters
{

// Returns the number of '+'-separated terms found case-insensitively in any
// order, or zero when the expression is invalid or any term is absent.
int MatchRequiredTerms(const char* message, const char* expression,
	int* positions, int* lengths, int capacity);

} // namespace filters
} // namespace pdw

#endif
