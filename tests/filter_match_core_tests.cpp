#include "filter_match_core.h"

#include <cstring>
#include <cstdlib>
#include <iostream>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (condition) return;
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

int main()
{
	int positions[10] = {};
	int lengths[10] = {};
	Expect(pdw::filters::MatchRequiredTerms("Traffic assignment for PR1",
		"PR1+Traffic", positions, lengths, 10) == 2,
		"plus terms match case-insensitively in any order");
	Expect(positions[0] == 23 && lengths[0] == 3 && positions[1] == 0 && lengths[1] == 7,
		"term positions and lengths are reported for legacy highlighting");
	Expect(pdw::filters::MatchRequiredTerms("traffic assignment for pr1",
		" PR1 + Traffic ", positions, lengths, 10) == 2,
		"surrounding term whitespace is ignored");
	Expect(pdw::filters::MatchRequiredTerms("PR1 only", "PR1+Traffic",
		positions, lengths, 10) == 0, "every plus term is required");
	Expect(pdw::filters::MatchRequiredTerms("PR1 Traffic", "PR1++Traffic",
		positions, lengths, 10) == 0, "empty plus terms are rejected");
	Expect(!pdw::filters::IsValidRequiredTermsExpression("+PR1", 10) &&
		!pdw::filters::IsValidRequiredTermsExpression("PR1+", 10) &&
		!pdw::filters::IsValidRequiredTermsExpression("PR1++Traffic", 10),
		"leading, trailing and empty required terms fail validation");
	Expect(!pdw::filters::IsValidRequiredTermsExpression(
		"1+2+3+4+5+6+7+8+9+10+11", 10),
		"required-term expressions cannot exceed the matcher capacity");
	Expect(pdw::filters::MatchRequiredTerms("PR1 Traffic", "PR1",
		positions, lengths, 10) == 0, "plain legacy text is not reinterpreted as multiword syntax");
	char label[64] = {};
	Expect(pdw::filters::ExpandLegacyLabel("Unit %123", "7654321", label, sizeof(label)) &&
		std::strcmp(label, "Unit 765") == 0, "valid legacy capcode label templates still expand");
	Expect(pdw::filters::ExpandLegacyLabel("100% Response", "7654321", label, sizeof(label)) &&
		std::strcmp(label, "100% Response") == 0,
		"ordinary percent signs in display names remain literal");
	Expect(pdw::filters::ExpandLegacyLabel("50%", "7654321", label, sizeof(label)) &&
		std::strcmp(label, "50%") == 0, "a trailing percent sign remains literal and bounded");
	Expect(pdw::filters::ExpandLegacyLabel("Position %8", "7654321", label, sizeof(label)) &&
		std::strcmp(label, "Position %8") == 0,
		"an out-of-range template position remains literal");
	std::cout << "Filter match core tests passed\n";
	return 0;
}
