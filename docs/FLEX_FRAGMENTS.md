# Optional FLEX fragment assembly

PDW can wait for a complete standard FLEX alpha or secure fragment chain and
then emit one assembled message. Introduced in PDW v5.4 2026 Release as an
additive compatibility mode, the enabled option now avoids displaying and
routing each valid fragment before the joined result. It remains disabled by
default.

Enable it under **Settings > Display and behavior > Screen and columns** with
**Wait for complete split FLEX alpha/secure message (show joined only)**.

## Compatibility guarantee

When the option is enabled, a valid fragment start or continuation is held in
the bounded reassembler instead of entering PDW's display, filter, logging,
email, notification, publishing, or data-output path. When the chain completes,
one compact message marked **[Joined FLEX]** enters that established path. That
event is marked `fragmented=true` and `assembled=true` for structured outputs.
Standalone FLEX messages and invalid, conflicting, or capacity-rejected
observations retain the established direct path so PDW never guesses a join.

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
expires.

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
existing message limit. If the bounded completion quarantine is full, new
observations fall back to the established direct path rather than being guessed
or silently joined. Resetting the decoder, changing input mode, disabling the
option, or enabling FLEX Group Mode clears transient fragment state.

## Text marked Part X of Y

PDW also recognises explicit message text such as **Part 1 of 2** (case
insensitive, with optional `#` and brackets). It buffers 2-32 parts for the
same capcode, protocol, message type, and advertised total, accepts reordered
parts, removes the markers, and emits one ordinary message when every part is
present. The display does not add a multipart status label. This applies to
paging messages independently of the FLEX header option because the sender has
explicitly advertised the part sequence.

The text marker does not contain a unique message identifier. PDW therefore
allows only one active chain for the same visible identity. Identical repeats
are ignored, a changed Part 1 starts a new chain, and conflicting later parts
are not combined. Chains are bounded to 64 active messages, 32 parts, the
existing message-size limit, and ten minutes. Incomplete expired chains are
discarded rather than being presented as a complete message.

## FLEX Group Mode

FLEX Group Mode is intentionally excluded from the added reassembler. Its
assignment, missed-call, conversion, logging, and duplicate state are tightly
coupled in the legacy decoder. Changing that path without representative replay
fixtures could lose a behavior relied on by existing operators, so current PDW
keeps it exactly on the established path.

## Automated evidence

`flex-fragment-reassembly` tests standalone messages, normal chains, reordered
and start-last delivery, duplicates, wrapped and repeated fragment positions,
long-running wrap cycles, ambiguous cross-cycle ordering, completed-identity
quarantine, changed-color and changed-content replay, late-final poisoning,
identity reuse, conflicts, message-number and type isolation, restarts,
timeouts, bounded active/completion capacity, color alignment, truncation,
independent active/completion capacities, addresses, and reset behavior.
`multipart-message-reassembly` covers explicit marker parsing, ordered and
reordered parts, duplicates, identity isolation, conflicts, expiry, capacity,
truncation, invalid counts, and reset. Live-network acceptance still
requires licensed, synthetic, or redacted recordings; private pager traffic
must not be committed.
