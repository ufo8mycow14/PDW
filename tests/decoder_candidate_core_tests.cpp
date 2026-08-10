#include "decoder_candidate_core.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

pdw::decoding::MessageCandidate Candidate(
	const char* eventKey,
	const char* payload,
	std::uint64_t observedAtMs,
	pdw::decoding::CandidateOrigin origin,
	float confidence,
	unsigned int correctedBits = 0)
{
	pdw::decoding::MessageCandidate candidate;
	candidate.eventKey = eventKey;
	candidate.protocol = "POCSAG";
	candidate.address = "1234567";
	candidate.payload = payload;
	candidate.observedAtMs = observedAtMs;
	candidate.correctedBits = correctedBits;
	candidate.confidence = confidence;
	candidate.origin = origin;
	return candidate;
}
}

int main()
{
	using namespace pdw::decoding;

	CandidateConsolidator agreement(100);
	agreement.Submit(Candidate("frame-1", "TEST", 1000, CANDIDATE_ORIGIN_LEGACY, 0.8f, 1));
	agreement.Submit(Candidate("frame-1", "TEST", 1020, CANDIDATE_ORIGIN_ENHANCED, 0.9f, 0));
	Expect(agreement.DrainReady(1119).empty(), "settle window is respected");
	std::vector<ConsolidatedMessage> agreed = agreement.DrainReady(1120);
	Expect(agreed.size() == 1, "agreement emits one message");
	Expect(!agreed[0].ambiguous, "agreement is not ambiguous");
	Expect(agreed[0].alternatives.empty(), "agreement has no alternate");
	Expect(agreed[0].originMask == 3, "both decoder origins retained");
	Expect(agreed[0].evidence.size() == 2, "both agreeing evidence records retained");
	Expect(agreed[0].primary.origin == CANDIDATE_ORIGIN_ENHANCED,
		"stronger identical evidence becomes primary");

	CandidateConsolidator legacyOnly(50);
	legacyOnly.Submit(Candidate("frame-2", "LEGACY", 2000, CANDIDATE_ORIGIN_LEGACY, 0.7f));
	std::vector<ConsolidatedMessage> only = legacyOnly.DrainReady(2050);
	Expect(only.size() == 1, "legacy-only success is retained");
	Expect(only[0].originMask == CANDIDATE_ORIGIN_LEGACY, "legacy origin retained");

	CandidateConsolidator conflict(100);
	conflict.Submit(Candidate("frame-3", "LEGACY READ", 3000, CANDIDATE_ORIGIN_LEGACY, 0.9f));
	conflict.Submit(Candidate("frame-3", "ENHANCED READ", 3010, CANDIDATE_ORIGIN_ENHANCED, 0.8f));
	std::vector<ConsolidatedMessage> conflicted = conflict.Flush();
	Expect(conflicted.size() == 1, "conflict remains one stream item");
	Expect(conflicted[0].ambiguous, "conflict is marked ambiguous");
	Expect(conflicted[0].primary.payload == "LEGACY READ", "legacy candidate is not discarded");
	Expect(conflicted[0].alternatives.size() == 1, "conflicting candidate is retained");
	Expect(conflicted[0].evidence.size() == 2, "both conflicting evidence records retained");
	Expect(conflicted[0].alternatives[0].payload == "ENHANCED READ", "alternate text retained");

	CandidateConsolidator distinct;
	distinct.Submit(Candidate("frame-4", "FIRST", 4000, CANDIDATE_ORIGIN_LEGACY, 0.8f));
	distinct.Submit(Candidate("frame-5", "SECOND", 4001, CANDIDATE_ORIGIN_ENHANCED, 0.8f));
	Expect(distinct.pendingCount() == 2, "different transmissions never merge by timing alone");

	CandidateConsolidator unidentified;
	unidentified.Submit(Candidate("", "FIRST", 5000, CANDIDATE_ORIGIN_LEGACY, 0.8f));
	unidentified.Submit(Candidate("", "SECOND", 5001, CANDIDATE_ORIGIN_ENHANCED, 0.8f));
	Expect(unidentified.pendingCount() == 2, "missing transmission IDs fail safe without merging");

	std::cout << "decoder candidate core tests passed\n";
	return 0;
}
