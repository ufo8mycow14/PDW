#include "flex_fragment_reassembly_core.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (condition) return;
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}

	pdw::flex::FragmentResult Observe(
		pdw::flex::FragmentReassembler& reassembler,
		std::uint32_t address,
		unsigned int fragment,
		bool continuation,
		std::uint64_t time,
		const std::string& text,
		std::uint8_t color = 5)
	{
		std::vector<std::uint8_t> colors(text.size(), color);
		pdw::flex::FragmentObservation observation;
		observation.address = address;
		observation.fragmentNumber = fragment;
		observation.continuation = continuation;
		observation.observedAtMs = time;
		observation.text = reinterpret_cast<const unsigned char*>(text.data());
		observation.colors = colors.empty() ? NULL : &colors[0];
		observation.length = text.size();
		return reassembler.Observe(observation);
	}
}

int main()
{
	using namespace pdw::flex;

	FragmentReassembler normal;
	Expect(Observe(normal, 1234567, 3, false, 100, "standalone").status ==
		FRAGMENT_STANDALONE, "K-type message remains standalone");
	Expect(normal.ActiveCount() == 0, "standalone message creates no chain");
	Expect(Observe(normal, 1234567, 3, true, 200, "one-").status ==
		FRAGMENT_BUFFERED_START, "F=11 starts a chain");
	Expect(Observe(normal, 1234567, 0, true, 210, "two-").status ==
		FRAGMENT_BUFFERED_CONTINUATION, "expected continuation is buffered");
	FragmentResult assembled = Observe(normal, 1234567, 1, false, 220, "three", 8);
	Expect(assembled.assembled && assembled.status == FRAGMENT_ASSEMBLED,
		"expected C-type fragment completes a chain");
	Expect(assembled.text == "one-two-three", "fragment text retains order");
	Expect(assembled.colors.size() == assembled.text.size(), "color data follows text");
	Expect(assembled.colors.back() == 8, "final fragment colors are retained");
	Expect(normal.ActiveCount() == 0, "completed chain releases its slot");

	FragmentReassembler strict;
	Observe(strict, 42, 3, true, 1000, "start");
	Expect(Observe(strict, 42, 1, true, 1001, "gap").status ==
		FRAGMENT_SEQUENCE_ERROR, "missing fragment aborts a chain");
	Expect(!strict.HasPending(42), "sequence error removes ambiguous state");
	Expect(Observe(strict, 42, 0, false, 1002, "last").status ==
		FRAGMENT_ORPHAN, "orphan final fragment is not assembled");
	Expect(Observe(strict, 42, 2, true, 1003, "middle").status ==
		FRAGMENT_ORPHAN, "orphan continuation is not assembled");

	FragmentReassembler restart;
	Observe(restart, 77, 3, true, 2000, "old-");
	Observe(restart, 77, 3, true, 2001, "new-");
	FragmentResult restarted = Observe(restart, 77, 0, false, 2002, "last");
	Expect(restarted.text == "new-last", "new F=11 cleanly restarts the same address");

	FragmentReassembler timeout(16, 100, 5119);
	Observe(timeout, 88, 3, true, 3000, "old");
	Expect(Observe(timeout, 88, 0, false, 3101, "late").status ==
		FRAGMENT_ORPHAN, "expired state cannot create a stale assembly");

	FragmentReassembler capacity(2, 120000, 5119);
	Observe(capacity, 1, 3, true, 4000, "a");
	Observe(capacity, 2, 3, true, 4000, "b");
	Expect(Observe(capacity, 3, 3, true, 4000, "c").status ==
		FRAGMENT_CAPACITY_REACHED, "bounded capacity fails without evicting valid chains");
	Expect(capacity.ActiveCount() == 2 && capacity.HasPending(1) && capacity.HasPending(2),
		"capacity pressure preserves existing chains");

	FragmentReassembler truncated(16, 120000, 5);
	Observe(truncated, 99, 3, true, 5000, "abc");
	FragmentResult shortResult = Observe(truncated, 99, 0, false, 5001, "def");
	Expect(shortResult.assembled && shortResult.truncated, "oversized assembly is marked truncated");
	Expect(shortResult.text == "abcde" && shortResult.colors.size() == 5,
		"oversized assembly stays within the configured bound");

	FragmentReassembler independent;
	Observe(independent, 100, 3, true, 6000, "first-");
	Observe(independent, 200, 3, true, 6000, "other-");
	FragmentResult first = Observe(independent, 100, 0, false, 6001, "done");
	FragmentResult second = Observe(independent, 200, 0, false, 6002, "done");
	Expect(first.text == "first-done" && second.text == "other-done",
		"different addresses remain independent");

	FragmentReassembler preserved;
	Observe(preserved, 300, 3, true, 7000, "pending-");
	Observe(preserved, 300, 3, false, 7001, "separate");
	Expect(preserved.HasPending(300), "standalone traffic does not erase a valid chain");
	Expect(Observe(preserved, 300, 0, false, 7002, "done").text == "pending-done",
		"pending chain can finish after unrelated standalone traffic");

	preserved.Reset();
	Expect(preserved.ActiveCount() == 0, "reset clears every pending chain");
	Expect(Observe(preserved, 1, 4, true, 8000, "bad").status == FRAGMENT_INVALID,
		"out-of-range fragment number is rejected");

	std::cout << "FLEX fragment reassembly core tests passed\n";
	return 0;
}
