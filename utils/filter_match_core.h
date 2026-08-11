#ifndef PDW_FILTER_MATCH_CORE_H
#define PDW_FILTER_MATCH_CORE_H

#include <cstddef>

namespace pdw
{
namespace filters
{

// Returns the number of '+'-separated terms found case-insensitively in any
// order, or zero when the expression is invalid or any term is absent.
int MatchRequiredTerms(const char* message, const char* expression,
	int* positions, int* lengths, int capacity);

// Validates the '+' syntax used by MatchRequiredTerms without requiring a
// decoded message. Plain single-keyword expressions are valid.
bool IsValidRequiredTermsExpression(const char* expression, int maximumTerms);

// Expands legacy %123 capcode-position templates while preserving ordinary
// percent signs in modern display names. Output is always NUL terminated when
// outputCapacity is non-zero; false reports truncation or invalid arguments.
bool ExpandLegacyLabel(const char* label, const char* capcode,
	char* output, std::size_t outputCapacity);

} // namespace filters
} // namespace pdw

#endif
