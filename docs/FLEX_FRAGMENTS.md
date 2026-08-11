# Optional FLEX fragment assembly

PDW v5.4 2026 Release can add one assembled copy after receiving a complete standard
FLEX alpha or secure fragment chain. The feature is deliberately additive and
is disabled by default.

Enable it under **Settings > Display and behavior > Screen and columns** with
**Join split FLEX alpha/secure messages (original fragments remain)**.

## Compatibility guarantee

Every fragment goes through PDW's established display, filter, duplicate,
logging, SMTP, Apprise, publishing, and optional-output path first. The shadow
reassembler cannot hide, replace, or delay that output. When a valid chain
completes, PDW adds one compact message marked **[Joined FLEX]**. That event
is marked `fragmented=true` and `assembled=true` for structured outputs.

Fragments are paired by capcode, the protocol's six-bit message number, and
message type. Reordered fragments are buffered until the sequence is complete;
duplicates are ignored. In-sequence fragment numbers may wrap through `0`,
`1`, and `2` more than once, including when a later cycle repeats the same
payload. An identical future-cycle fragment received before its position is
ambiguous with a retransmission, so it is handled fail-closed and must be seen
again in sequence. Conflicting or incomplete chains never produce a guessed
message. After a chain completes, its capcode/message-number/message-type
identity is quarantined for the retention window. Trailing fragments, changed
rendering colors, and changed-content replays for that identity cannot create a
second or mixed assembled copy; the identity becomes reusable after the window
expires. The original fragment behavior is unchanged.

## Sequence and limits

The implementation uses the standard FLEX K/F/C header fields:

- fragment number `3` with continuation clear is a standalone K message;
- fragment number `3` with continuation set starts or restarts a chain;
- following fragment numbers must progress `0`, `1`, `2`, then wrap as needed;
- a non-`3` fragment with continuation clear completes the chain only when it
  is the expected next fragment.

At most 64 message chains and 4,096 completed identities are retained. Duplicate
tracking uses one fixed start record plus the last accepted and pending record
for each `0`, `1`, and `2` position; it does not grow with the number of wrapped
fragments. Incomplete chains and completed-identity quarantine entries expire
after four minutes as required by FLEX, and assembled text is bounded by PDW's
existing message limit. If the bounded completion quarantine is full, PDW emits
no new assembled copy until an entry expires; original fragments remain
unaffected. Resetting the decoder, changing input mode, disabling the option, or
enabling FLEX Group Mode clears transient fragment state.

## FLEX Group Mode

FLEX Group Mode is intentionally excluded from the added reassembler. Its
assignment, missed-call, conversion, logging, and duplicate state are tightly
coupled in the legacy decoder. Changing that path without representative replay
fixtures could lose a behavior relied on by existing operators, so PDW v5.4 2026 Release
keeps it exactly on the established path.

## Automated evidence

`flex-fragment-reassembly` tests standalone messages, normal chains, reordered
and start-last delivery, duplicates, wrapped and repeated fragment positions,
long-running wrap cycles, ambiguous cross-cycle ordering, completed-identity
quarantine, changed-color and changed-content replay, late-final poisoning,
identity reuse, conflicts, message-number and type isolation, restarts,
timeouts, bounded active/completion capacity, color alignment, truncation,
independent active/completion capacities, addresses, and reset behavior. Live-network acceptance still
requires licensed, synthetic, or redacted recordings; private pager traffic
must not be committed.
