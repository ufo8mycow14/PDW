#include "assembled_message_visibility_core.h"

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
	pdw::assembled::VisibilityGuard guard(4, 100);
	Expect(!guard.ShouldSuppress(false, "alert", "ALPHA", 1),
		"an ordinary message is accepted");
	Expect(guard.ShouldSuppress(true, "alert", "ALPHA", 2),
		"an assembled copy of an already visible message is suppressed");
	Expect(!guard.ShouldSuppress(false, "alert", "ALPHA", 3),
		"ordinary repeated traffic remains visible");
	Expect(!guard.ShouldSuppress(true, "different", "ALPHA", 4),
		"a distinct assembled payload is accepted");
	Expect(guard.ShouldSuppress(true, "different", "ALPHA", 5),
		"the same assembled payload is emitted only once across capcodes");
	Expect(!guard.ShouldSuppress(true, "alert", "NUMERIC", 6),
		"message type keeps unrelated payload formats distinct");
	Expect(!guard.ShouldSuppress(true, "alert", "ALPHA", 104),
		"expired content does not suppress later traffic");
	guard.Reset();
	Expect(!guard.ShouldSuppress(true, "different", "ALPHA", 105),
		"reset clears remembered visible messages");

	std::cout << "Assembled message visibility core tests passed\n";
	return 0;
}
