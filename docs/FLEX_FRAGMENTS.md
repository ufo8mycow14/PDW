# Optional FLEX fragment assembly

PDW v5.2 2026 Release can add one assembled copy after receiving a complete standard
FLEX alpha or secure fragment chain. The feature is deliberately additive and
is disabled by default.

Enable it under **Settings > Display and behavior > Screen and columns** with
**Also show assembled FLEX alpha copy (original fragments remain)**.

## Compatibility guarantee

Every fragment goes through PDW's established display, filter, duplicate,
logging, SMTP, Apprise, publishing, and optional-output path first. The shadow
reassembler cannot hide, replace, or delay that output. When a valid chain
completes, PDW adds a message marked **[Assembled FLEX fragments]**. That event
is marked `fragmented=true` and `assembled=true` for structured outputs.

If a fragment is missing, late, out of order, orphaned, too large, or cannot be
buffered, PDW simply adds no assembled copy. The original fragment behavior is
unchanged.

## Sequence and limits

The implementation uses the standard FLEX K/F/C header fields:

- fragment number `3` with continuation clear is a standalone K message;
- fragment number `3` with continuation set starts or restarts a chain;
- following fragment numbers must progress `0`, `1`, `2`, then wrap as needed;
- a non-`3` fragment with continuation clear completes the chain only when it
  is the expected next fragment.

At most 16 addresses are buffered, incomplete chains expire after two minutes,
and assembled text is bounded by PDW's existing message limit. Resetting the
decoder, changing input mode, disabling the option, or enabling FLEX Group Mode
clears transient fragment state.

## FLEX Group Mode

FLEX Group Mode is intentionally excluded from the added reassembler. Its
assignment, missed-call, conversion, logging, and duplicate state are tightly
coupled in the legacy decoder. Changing that path without representative replay
fixtures could lose a behavior relied on by existing operators, so PDW v5.2 2026 Release
keeps it exactly on the established path.

## Automated evidence

`flex-fragment-reassembly` tests standalone messages, normal chains, strict
ordering, orphans, restarts, timeouts, bounded capacity, truncation, independent
addresses, and reset behavior. Live-network acceptance still requires licensed,
synthetic, or redacted recordings; private pager traffic must not be committed.
