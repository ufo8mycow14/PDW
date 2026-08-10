#ifndef PDW_DECODER_CANDIDATE_CORE_H
#define PDW_DECODER_CANDIDATE_CORE_H

#include <cstdint>
#include <string>
#include <vector>

namespace pdw
{
namespace decoding
{

enum CandidateOrigin
{
	CANDIDATE_ORIGIN_LEGACY = 1,
	CANDIDATE_ORIGIN_ENHANCED = 2
};

struct MessageCandidate
{
	std::string eventKey;
	std::string protocol;
	std::string address;
	std::string payload;
	std::uint64_t observedAtMs;
	unsigned int correctedBits;
	float confidence;
	CandidateOrigin origin;
};

struct ConsolidatedMessage
{
	MessageCandidate primary;
	std::vector<MessageCandidate> evidence;
	std::vector<MessageCandidate> alternatives;
	unsigned int originMask;
	bool ambiguous;
};

// Holds candidates briefly so legacy and enhanced decoders can contribute to
// one message stream. eventKey is assigned at the capture/frame boundary; PDW
// never guesses that two different transmissions are the same merely because
// their address and timing are similar.
class CandidateConsolidator
{
public:
	explicit CandidateConsolidator(std::uint64_t settleWindowMs = 750);

	void Submit(const MessageCandidate& candidate);
	std::vector<ConsolidatedMessage> DrainReady(std::uint64_t nowMs);
	std::vector<ConsolidatedMessage> Flush();
	std::size_t pendingCount() const;

private:
	struct PendingMessage
	{
		ConsolidatedMessage message;
		std::uint64_t lastObservedAtMs;
	};

	std::uint64_t settleWindowMs_;
	std::vector<PendingMessage> pending_;
};

} // namespace decoding
} // namespace pdw

#endif
