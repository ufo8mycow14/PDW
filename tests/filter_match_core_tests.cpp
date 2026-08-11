#include "filter_match_core.h"

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
	Expect(pdw::filters::MatchRequiredTerms("PR1 Traffic", "PR1",
		positions, lengths, 10) == 0, "plain legacy text is not reinterpreted as multiword syntax");
	std::cout << "Filter match core tests passed\n";
	return 0;
}
