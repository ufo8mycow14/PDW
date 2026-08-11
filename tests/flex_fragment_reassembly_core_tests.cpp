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
		std::uint8_t color = 5,
		unsigned int messageNumber = 0,
		unsigned int messageType = 5)
	{
		std::vector<std::uint8_t> colors(text.size(), color);
		pdw::flex::FragmentObservation observation;
		observation.address = address;
		observation.messageNumber = messageNumber;
		observation.messageType = messageType;
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

	Expect(!pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_STANDALONE),
		"standalone FLEX messages retain the direct display path");
	Expect(pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_BUFFERED_START) &&
		pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_BUFFERED_CONTINUATION) &&
		pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_BUFFERED_OUT_OF_ORDER) &&
		pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_DUPLICATE) &&
		pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_ASSEMBLED),
		"valid FLEX chain states hold the original fragment");
	Expect(!pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_CONFLICT) &&
		!pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_CAPACITY_REACHED) &&
		!pdw::flex::ShouldHoldOriginalFragment(FRAGMENT_INVALID),
		"unsafe FLEX states fall back to the direct display path");

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

	FragmentReassembler reordered;
	Observe(reordered, 42, 3, true, 1000, "start-");
	Expect(Observe(reordered, 42, 1, false, 1001, "last").status ==
		FRAGMENT_BUFFERED_OUT_OF_ORDER, "early final fragment is buffered");
	FragmentResult reorderedResult = Observe(reordered, 42, 0, true, 1002, "middle-");
	Expect(reorderedResult.assembled && reorderedResult.text == "start-middle-last",
		"out-of-order fragments assemble after the gap arrives");

	FragmentReassembler startLast;
	Expect(Observe(startLast, 43, 0, true, 1100, "middle-").status ==
		FRAGMENT_BUFFERED_OUT_OF_ORDER, "continuation may arrive before start");
	Expect(Observe(startLast, 43, 1, false, 1101, "last").status ==
		FRAGMENT_BUFFERED_OUT_OF_ORDER, "final may arrive before start");
	FragmentResult startLastResult = Observe(startLast, 43, 3, true, 1102, "start-");
	Expect(startLastResult.assembled && startLastResult.text == "start-middle-last",
		"late start drains a complete unambiguous sequence");

	FragmentReassembler restart;
	Observe(restart, 77, 3, true, 2000, "old-");
	Expect(Observe(restart, 77, 3, true, 2001, "new-").status == FRAGMENT_CONFLICT,
		"conflicting start replaces incomplete state without emitting it");
	FragmentResult restarted = Observe(restart, 77, 0, false, 2002, "last");
	Expect(restarted.text == "new-last", "new F=11 cleanly restarts the same address");

	FragmentReassembler timeout(16, 100, 5119);
	Observe(timeout, 88, 3, true, 3000, "old");
	FragmentResult late = Observe(timeout, 88, 0, false, 3101, "late");
	Expect(!late.assembled && late.status == FRAGMENT_BUFFERED_OUT_OF_ORDER,
		"expired state cannot create a stale assembly");

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

	FragmentReassembler interleaved;
	Observe(interleaved, 500, 3, true, 6100, "page-a-", 5, 10);
	Observe(interleaved, 500, 3, true, 6101, "page-b-", 5, 11);
	FragmentResult pageB = Observe(interleaved, 500, 0, false, 6102, "done", 5, 11);
	FragmentResult pageA = Observe(interleaved, 500, 0, false, 6103, "done", 5, 10);
	Expect(pageA.text == "page-a-done" && pageB.text == "page-b-done",
		"message numbers isolate interleaved pages for one capcode");

	FragmentReassembler typed;
	Observe(typed, 501, 3, true, 6200, "alpha-", 5, 12, 5);
	Observe(typed, 501, 3, true, 6201, "secure-", 5, 12, 0);
	FragmentResult alpha = Observe(typed, 501, 0, false, 6202, "done", 5, 12, 5);
	FragmentResult secure = Observe(typed, 501, 0, false, 6203, "done", 5, 12, 0);
	Expect(alpha.text == "alpha-done" && secure.text == "secure-done",
		"message type participates in fragment identity");

	FragmentReassembler duplicate;
	Observe(duplicate, 600, 3, true, 6300, "start-", 5, 3);
	Expect(Observe(duplicate, 600, 3, true, 6301, "start-", 5, 3).status ==
		FRAGMENT_DUPLICATE, "duplicate start does not restart the page");
	Observe(duplicate, 600, 1, false, 6302, "last", 5, 3);
	Expect(Observe(duplicate, 600, 1, false, 6303, "last", 5, 3).status ==
		FRAGMENT_DUPLICATE, "duplicate buffered fragment is ignored");
	FragmentResult duplicateResult = Observe(duplicate, 600, 0, true, 6304, "middle-", 5, 3);
	Expect(duplicateResult.text == "start-middle-last",
		"duplicates cannot corrupt the final assembly");

	FragmentReassembler wrapped;
	Observe(wrapped, 650, 3, true, 6350, "start-", 5, 7);
	Expect(Observe(wrapped, 650, 0, true, 6351, "same-", 5, 7).status ==
		FRAGMENT_BUFFERED_CONTINUATION, "first wrapped position is accepted");
	Expect(Observe(wrapped, 650, 0, true, 6352, "same-", 5, 7).status ==
		FRAGMENT_DUPLICATE, "replayed part outside the expected position is ignored");
	Observe(wrapped, 650, 1, true, 6353, "one-", 5, 7);
	Observe(wrapped, 650, 2, true, 6354, "two-", 5, 7);
	Expect(Observe(wrapped, 650, 0, true, 6355, "same-", 9, 7).status ==
		FRAGMENT_BUFFERED_CONTINUATION,
		"wrapped fragment with repeated payload is not mistaken for a duplicate");
	FragmentResult wrappedResult = Observe(wrapped, 650, 1, false, 6356, "done", 5, 7);
	Expect(wrappedResult.assembled &&
		wrappedResult.text == "start-same-one-two-same-done",
		"long page assembles through a repeated 0,1,2 fragment cycle");
	Expect(wrappedResult.colors.size() == wrappedResult.text.size() &&
		wrappedResult.colors[19] == 9,
		"color alignment survives a repeated wrapped fragment position");

	FragmentReassembler ambiguousWrap;
	Observe(ambiguousWrap, 651, 3, true, 6360, "start-", 5, 8);
	Observe(ambiguousWrap, 651, 0, true, 6361, "same-", 5, 8);
	Expect(Observe(ambiguousWrap, 651, 0, true, 6362, "same-", 5, 8).status ==
		FRAGMENT_DUPLICATE,
		"identical cross-cycle part received early is handled fail-closed");
	Observe(ambiguousWrap, 651, 1, true, 6363, "one-", 5, 8);
	Observe(ambiguousWrap, 651, 2, true, 6364, "two-", 5, 8);
	Expect(ambiguousWrap.HasPending(651),
		"ambiguous future-cycle part cannot create a guessed assembly");
	Expect(Observe(ambiguousWrap, 651, 0, true, 6365, "same-", 5, 8).status ==
		FRAGMENT_BUFFERED_CONTINUATION,
		"ambiguous wrapped part is accepted only when seen again in sequence");
	FragmentResult recoveredWrap =
		Observe(ambiguousWrap, 651, 1, false, 6366, "done", 5, 8);
	Expect(recoveredWrap.assembled &&
		recoveredWrap.text == "start-same-one-two-same-done",
		"fail-closed wrapped sequence recovers without guessing content");

	FragmentReassembler boundedWrap(1, 120000, 64);
	Observe(boundedWrap, 654, 3, true, 6600, "start-", 5, 11);
	std::uint64_t boundedTime = 6601;
	for (unsigned int cycle = 0; cycle < 2048; ++cycle)
	{
		Expect(Observe(boundedWrap, 654, 0, true, boundedTime++, "x", 5, 11).status ==
			FRAGMENT_BUFFERED_CONTINUATION, "long wrapped chain accepts position 0");
		Expect(Observe(boundedWrap, 654, 1, true, boundedTime++, "y", 5, 11).status ==
			FRAGMENT_BUFFERED_CONTINUATION, "long wrapped chain accepts position 1");
		Expect(Observe(boundedWrap, 654, 2, true, boundedTime++, "z", 5, 11).status ==
			FRAGMENT_BUFFERED_CONTINUATION, "long wrapped chain accepts position 2");
	}
	FragmentResult boundedWrapResult =
		Observe(boundedWrap, 654, 0, false, boundedTime, "end", 5, 11);
	Expect(boundedWrapResult.assembled && boundedWrapResult.truncated &&
		boundedWrapResult.text.size() == 64 && boundedWrapResult.colors.size() == 64,
		"very long wrapped chain retains bounded assembled text and color state");
	Expect(!boundedWrap.HasPending(654),
		"very long wrapped chain releases its fixed duplicate state");

	FragmentReassembler completedDuplicate;
	Observe(completedDuplicate, 652, 3, true, 6370, "start-", 5, 9);
	Expect(Observe(completedDuplicate, 652, 0, false, 6371, "done", 5, 9).assembled,
		"first complete chain is emitted");
	Expect(Observe(completedDuplicate, 652, 3, true, 6372, "start-", 5, 9).status ==
		FRAGMENT_DUPLICATE, "completed identity quarantines a replayed start");
	FragmentResult replayedCompletion =
		Observe(completedDuplicate, 652, 0, false, 6373, "done", 5, 9);
	Expect(!replayedCompletion.assembled &&
		replayedCompletion.status == FRAGMENT_DUPLICATE,
		"replayed complete chain does not emit a second assembled event");
	Expect(!completedDuplicate.HasPending(652),
		"replayed completion releases transient chain state");

	FragmentReassembler changedColorReplay;
	Observe(changedColorReplay, 655, 3, true, 6380, "start-", 5, 12);
	Expect(Observe(changedColorReplay, 655, 0, false, 6381, "done", 5, 12).assembled,
		"baseline chain for changed-color replay is emitted");
	Expect(Observe(changedColorReplay, 655, 3, true, 6382, "start-", 9, 12).status ==
		FRAGMENT_DUPLICATE,
		"rendering color changes cannot bypass completed-identity quarantine");
	Expect(Observe(changedColorReplay, 655, 0, false, 6383, "done", 9, 12).status ==
		FRAGMENT_DUPLICATE,
		"changed-color final cannot emit a second assembled event");

	FragmentReassembler changedContentReplay;
	Observe(changedContentReplay, 656, 3, true, 6390, "old-", 5, 13);
	Expect(Observe(changedContentReplay, 656, 0, false, 6391, "done", 5, 13).assembled,
		"baseline identity for changed-content replay is emitted");
	Expect(Observe(changedContentReplay, 656, 3, true, 6392, "new-", 5, 13).status ==
		FRAGMENT_DUPLICATE,
		"same identity with changed content remains quarantined before expiry");
	Expect(Observe(changedContentReplay, 656, 0, false, 6393, "other", 5, 13).status ==
		FRAGMENT_DUPLICATE,
		"changed-content final cannot bypass identity quarantine");
	Expect(!changedContentReplay.HasPending(656),
		"changed-content replay creates no speculative chain");

	FragmentReassembler lateFinal;
	Observe(lateFinal, 657, 3, true, 6400, "old-", 5, 14);
	Expect(Observe(lateFinal, 657, 0, false, 6401, "tail", 5, 14).assembled,
		"baseline identity for late-final regression is emitted");
	Expect(Observe(lateFinal, 657, 0, false, 6402, "tail", 5, 14).status ==
		FRAGMENT_DUPLICATE,
		"late final is quarantined instead of buffered before a start");
	Expect(!lateFinal.HasPending(657),
		"late final cannot poison pre-start fragment state");
	Expect(Observe(lateFinal, 657, 3, true, 6403, "new-", 5, 14).status ==
		FRAGMENT_DUPLICATE,
		"late-final identity cannot be reopened before quarantine expiry");
	Expect(Observe(lateFinal, 657, 0, false, 6404, "other", 5, 14).status ==
		FRAGMENT_DUPLICATE,
		"late final cannot be mixed with a different start into guessed output");

	FragmentReassembler reusedIdentity(16, 100, 5119);
	Observe(reusedIdentity, 653, 3, true, 6500, "same-", 5, 10);
	Expect(Observe(reusedIdentity, 653, 0, false, 6501, "done", 5, 10).assembled,
		"initial identity completes inside the reuse window");
	Observe(reusedIdentity, 653, 3, true, 6602, "new-", 9, 10);
	FragmentResult reusedIdentityResult =
		Observe(reusedIdentity, 653, 0, false, 6603, "content", 9, 10);
	Expect(reusedIdentityResult.assembled &&
		reusedIdentityResult.text == "new-content",
		"expired completion history permits clean identity reuse");

	FragmentReassembler completionCapacity(1, 100, 5119, 1);
	Observe(completionCapacity, 658, 3, true, 6700, "one-", 5, 15);
	Expect(Observe(completionCapacity, 658, 0, false, 6701, "done", 5, 15).assembled,
		"first completion occupies the bounded identity quarantine");
	Observe(completionCapacity, 659, 3, true, 6702, "two-", 5, 16);
	FragmentResult capacityCompletion =
		Observe(completionCapacity, 659, 0, false, 6703, "done", 5, 16);
	Expect(!capacityCompletion.assembled &&
		capacityCompletion.status == FRAGMENT_CAPACITY_REACHED,
		"full completion quarantine fails closed without evicting an identity");
	Observe(completionCapacity, 659, 3, true, 6802, "two-", 5, 16);
	Expect(Observe(completionCapacity, 659, 0, false, 6803, "done", 5, 16).assembled,
		"completion proceeds after the bounded quarantine entry expires");

	FragmentReassembler completionScale(1, 240000, 5119, 96);
	for (std::uint32_t index = 0; index < 96; ++index)
	{
		const std::uint32_t address = 7000 + index;
		Observe(completionScale, address, 3, true, 7000 + index * 2,
			"part-", 5, index % 32);
		Expect(Observe(completionScale, address, 0, false,
			7001 + index * 2, "done", 5, index % 32).assembled,
			"completion quarantine capacity is independent of active slots");
	}

	FragmentReassembler conflict;
	Observe(conflict, 700, 3, true, 6400, "start-", 5, 4);
	Observe(conflict, 700, 1, false, 6401, "last-a", 5, 4);
	Expect(Observe(conflict, 700, 1, false, 6402, "last-b", 5, 4).status ==
		FRAGMENT_CONFLICT, "conflicting copies are rejected as ambiguous");
	Expect(!conflict.HasPending(700), "ambiguous chain is discarded without output");

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
