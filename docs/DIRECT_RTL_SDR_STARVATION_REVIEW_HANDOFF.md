# Direct RTL-SDR starvation correction - frozen review handoff

**FROZEN FOR INDEPENDENT REVIEW - DO NOT INSTALL ON THE LOUNGE PC**

- Prepared: 14 August 2026
- Repository: `C:\PDW Update\PDW-source`
- Baseline: `4843c8a9dbc75e74a976eb48670b5c96cbe8ab06`
- Release identity retained: `PDW v5.5.2 2026 Release`

I retain the following 14 August review as historical evidence. At that time,
the diff over the baseline above was uncommitted and had not been packaged,
signed, installed, deployed, or pushed. The locally built executables correctly
carried `state=dirty` provenance and were review artifacts only.

## Source update - 7 September 2026

I have incorporated the subsequent audit repairs into this source update:
bounded legacy message processing, spreadsheet-safe live CSV, decoder-state
synchronisation and rate adaptation, odd-byte IQ handling, bounded SigMF
parsing, fragment visibility, publishing queue ownership, transactional outbox
sequencing and restore, temporary-file cleanup, mapping preservation, and tray
and About-dialog fixes.

I verified the updated development builds with 37/37 CTest tests on each of
x64 and Win32, plus native startup, tray, About-text, exit and isolated local
publishing checks on both. I also compiled both optional audio-device smoke
targets. These results supersede the earlier 36-test counts below; the earlier
hashes identify only the historical review binaries.

I retain the installation freeze pending physical receiver and remote-session
soak acceptance. The development evidence does not establish stable-release
acceptance. I preserve the historical scope and results below without applying
their original diff-scope statements to the later audit repairs.

## Diagnosis

The direct RTL-SDR callback remained fresh while accepted pager rows could
stop. The prior modern-source path copied callback audio into a 32-block queue,
then relied on the main Windows timer to drain at most eight blocks every
100 ms. Protocol decoding therefore ran on the UI event loop. Chrome Remote
Desktop rendering and the 40 ms live meter could delay that service long enough
to fill/drop the queue; the resulting discontinuity reset decoder acquisition.

This change treats the incident as timing/resource starvation. It makes no
claim that a memory leak was present.

## Implemented correction

### Capture and decode ownership

- All modern sources (direct RTL-SDR, RTL-TCP, and event-driven WASAPI) feed a
  dedicated decoder worker instead of the Windows UI timer.
- The worker uses 64 fixed slots of at most 4,096 floats. Its one MiB sample
  store is allocated before capture. The callback only copies into available
  slots and signals an event; there is no unbounded queue.
- Queue-full policy drops the new remainder, counts every dropped block/sample,
  and marks the next accepted block discontinuous. Silent loss is not allowed.
- The worker drains independently, services existing protocol timers at the
  established 100 ms cadence, and runs at above-normal (not time-critical)
  priority. Direct and RTL-TCP capture threads use the same bounded priority.
- Stop order is source join, bounded worker drain/join, then resource release.
  A timeout preserves ownership, enters quarantine, and blocks unsafe reuse.
- Direct RTL-SDR reuses its callback output buffer. Enhanced conditioning now
  reuses its intermediate vectors after capacity is established instead of
  recreating them for every IQ callback.

No POCSAG, FLEX, ReFLEX, BCH, slicer, message interpretation, filtering,
message-content, output-selection, credential, or network-protocol rule was
changed.

### Review surface

- Worker and test: `utils/bounded_audio_decode_worker.h`,
  `utils/bounded_audio_decode_worker.cpp`, and
  `tests/bounded_audio_decode_worker_tests.cpp`.
- Capture/decoder integration: `sound_in.cpp`, `Headers/sound_in.h`,
  `utils/rtl_tcp_source.*`, and `utils/rtl_signal_conditioner.*`.
- UI isolation/telemetry: `Misc.cpp`, `PDW.cpp`, `Initapp.cpp`,
  `live_signal_meter.cpp`, `Headers/live_signal_meter.h`, `Headers/misc.h`, and
  `Headers/pdw.h`.
- Build registration: the root and `utils` CMake lists.
- Operator/reviewer guidance: `docs/SIGNAL_SOURCES.md`,
  `docs/DIRECT_RTL_SDR_SOAK_ACCEPTANCE.md`, and this handoff.

`Pocsag.cpp`, `Flex.cpp`, decoder interpretation tables, filters, destinations,
and persisted configuration defaults are outside the diff.

### UI isolation

- A decoder worker updates bounded pane backing data only. Scrollbar refresh
  and repaint are coalesced onto the owning window thread.
- Painting copies one bounded line at a time and performs all GDI work after
  releasing the pane-data lock. Clipboard, selection, clear, and scrollback
  replacement use the same snapshot/replace boundary.
- Window-title changes from decode are posted to the UI thread.
- The live meter now starts at 100 ms rather than 40 ms, moves to 250 ms for a
  Windows remote session or decoder pressure, moves to one second while hidden
  or minimised, skips unchanged frames, and reuses one memory DC/bitmap per
  meter size. Tooltip text is updated only when it changes.

### Soak telemetry

The Signal & radio diagnostics surface now reports:

- queue depth/capacity/high-water/fixed bytes, drops, reset discards, decoded
  samples, and current/maximum decoder lag;
- worker running/priority/quarantine state, stop timeouts, starts/stops, and
  handle creation/closure/current ownership;
- accepted-row count and latest accepted-row age;
- process handle/GDI/USER counts; and
- meter paint/skip/suspend/tooltip/backbuffer counters.

Accepted-row telemetry increments only after `ShowMessage` actually commits a
row to at least one visible pane. It is deliberately stronger than IQ or
process activity, but it must still be correlated with an independent trusted
feed during known traffic.

## Deterministic tests added

`bounded-audio-decode-worker` covers:

- 60 simulated seconds of the fixed 1.024 Msps to 48 kHz callback ratio with
  the UI completely absent: every sample arrives in order, drops are zero, and
  the only discontinuity is startup;
- explicit queue pressure and counted backpressure/discontinuity;
- 25 clean start/drain/stop cycles with balanced worker and process handles;
  and
- independent protocol-service ticks with no UI timer or audio input.

Existing POCSAG fixture, audio/FLEX slicing, FLEX fragment/reassembly, receiver,
capture-cleanup, output, persistence, and settings tests remain part of the
complete suite.

## Validation evidence

All commands ran from the repository above on 14 August 2026.
Both build trees used Visual Studio 18 2026 with MSVC toolset `v145` and the
Windows 10.0.26100 SDK.

| Gate | x64 | Win32/x86 |
| --- | --- | --- |
| Complete Release target build | Passed | Passed |
| Complete CTest suite | 36/36 passed, 3.85 s | 36/36 passed, 4.23 s |
| New bounded worker test | Passed | Passed |
| Bounded worker repeat-until-fail stress | 20/20 passed | 20/20 passed |
| Optional WASAPI smoke target compile | Passed | Passed |
| Optional WinMM smoke target compile | Passed | Passed |
| PE machine | `0x8664` valid | `0x014C` valid |
| File version | `5.5.2.0` | `5.5.2.0` |
| Product version | `5.5.2 2026 Release` | `5.5.2 2026 Release` |

- Static release audit: passed for x64 and Win32/x86.
- Whitespace/error audit: `git diff --check` passed (line-ending notices only).

Review-artifact hashes (not installation approvals):

- x64 SHA-256:
  `4974DA6EAFBFBC7D66EB7B336E435A8AEF028F9B46C170CAE8CB9C428935840F`
- Win32 SHA-256:
  `518F9416DBC0B52D8E26CF146284B6B173D0DEAB1DC93F5A1C641434DD5E22AA`

## Independent review focus

1. Verify the fixed ring's full/drop/discontinuity behavior and the source-first
   shutdown/quarantine boundary.
2. Verify modern protocol timers and decoder state remain single-owner while
   legacy serial/WinMM behavior stays on its established UI path.
3. Verify no worker path calls GDI or synchronously paints a window.
4. Verify pane/title handoff preserves exact displayed rows and does not alter
   output routing or filter decisions.
5. Verify meter GDI create/delete accounting across resize, theme, settings,
   minimise/restore, and destruction.
6. Re-run both complete suites from a fresh clone/review branch, then perform
   the physical acceptance in
   [`DIRECT_RTL_SDR_SOAK_ACCEPTANCE.md`](DIRECT_RTL_SDR_SOAK_ACCEPTANCE.md).

## Intentionally outstanding

- No physical RTL-SDR was attached in this review workspace.
- No Chrome Remote Desktop hardware soak or trusted-feed correlation was run.
- Optional audio-device smoke executables were compiled, not executed.
- No signed-in/real-traffic UI visual acceptance was performed.
- No clean-commit provenance build, portable package, installer, signature, or
  Lounge installation was produced.

The change remains blocked from Lounge installation until independent review
and the accepted-row/trusted-feed physical health gate both pass. Process
uptime, IQ freshness, or a moving signal meter alone must never be used as the
runtime approval signal.
