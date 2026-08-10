#include "decoder_candidate_core.h"

#include <algorithm>

namespace pdw
{
namespace decoding
{

namespace
{
unsigned int OriginBit(CandidateOrigin origin)
{
	return static_cast<unsigned int>(origin);
}

bool SameReading(const MessageCandidate& left, const MessageCandidate& right)
{
	return left.protocol == right.protocol &&
		left.address == right.address &&
		left.payload == right.payload;
}

bool BetterEvidence(const MessageCandidate& candidate, const MessageCandidate& current)
{
	if (candidate.confidence != current.confidence)
		return candidate.confidence > current.confidence;
	if (candidate.correctedBits != current.correctedBits)
		return candidate.correctedBits < current.correctedBits;
	// Preserve the field-tested path as the deterministic tie-breaker.
	return candidate.origin == CANDIDATE_ORIGIN_LEGACY &&
		current.origin != CANDIDATE_ORIGIN_LEGACY;
}
}

CandidateConsolidator::CandidateConsolidator(std::uint64_t settleWindowMs)
	: settleWindowMs_(settleWindowMs)
{
}

void CandidateConsolidator::Submit(const MessageCandidate& candidate)
{
	for (std::vector<PendingMessage>::iterator pending = pending_.begin();
		pending != pending_.end(); ++pending)
	{
		if (candidate.eventKey.empty() ||
			pending->message.primary.eventKey != candidate.eventKey)
			continue;

		pending->lastObservedAtMs = std::max(pending->lastObservedAtMs, candidate.observedAtMs);
		pending->message.originMask |= OriginBit(candidate.origin);
		pending->message.evidence.push_back(candidate);

		if (SameReading(pending->message.primary, candidate))
		{
			if (BetterEvidence(candidate, pending->message.primary))
				pending->message.primary = candidate;
			return;
		}

		for (std::vector<MessageCandidate>::iterator alternate =
			pending->message.alternatives.begin();
			alternate != pending->message.alternatives.end(); ++alternate)
		{
			if (SameReading(*alternate, candidate))
			{
				if (BetterEvidence(candidate, *alternate)) *alternate = candidate;
				pending->message.ambiguous = true;
				return;
			}
		}

		pending->message.alternatives.push_back(candidate);
		pending->message.ambiguous = true;
		return;
	}

	PendingMessage pending;
	pending.message.primary = candidate;
	pending.message.evidence.push_back(candidate);
	pending.message.originMask = OriginBit(candidate.origin);
	pending.message.ambiguous = false;
	pending.lastObservedAtMs = candidate.observedAtMs;
	pending_.push_back(pending);
}

std::vector<ConsolidatedMessage> CandidateConsolidator::DrainReady(std::uint64_t nowMs)
{
	std::vector<ConsolidatedMessage> ready;
	std::vector<PendingMessage>::iterator pending = pending_.begin();
	while (pending != pending_.end())
	{
		if (nowMs >= pending->lastObservedAtMs &&
			nowMs - pending->lastObservedAtMs >= settleWindowMs_)
		{
			ready.push_back(pending->message);
			pending = pending_.erase(pending);
		}
		else
		{
			++pending;
		}
	}
	return ready;
}

std::vector<ConsolidatedMessage> CandidateConsolidator::Flush()
{
	std::vector<ConsolidatedMessage> ready;
	for (std::vector<PendingMessage>::const_iterator pending = pending_.begin();
		pending != pending_.end(); ++pending)
		ready.push_back(pending->message);
	pending_.clear();
	return ready;
}

std::size_t CandidateConsolidator::pendingCount() const
{
	return pending_.size();
}

} // namespace decoding
} // namespace pdw
