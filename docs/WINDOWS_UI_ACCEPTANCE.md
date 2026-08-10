# Windows UI acceptance

This checklist records the manual evidence still required before PDW v4.5 can
move from beta toward a stable release. A successful resource compile, startup
smoke, or unit suite does not prove that every legacy dialog remains readable
and operable at every Windows scale.

Do not use live pager traffic during UI acceptance. Use an empty profile,
synthetic messages, or the documented configuration-only test actions. Do not
include credentials, decoded addresses, message text, queues, or recordings in
screenshots or issue reports.

## Automated and static evidence already completed

- The Win32 resource compiler accepts all dialog templates.
- A static bounds pass found all non-combobox controls inside their declared
  dialog bounds.
- Every visible interactive resource button participates in keyboard tab
  order after the System Tray, Filter Options, and SMTP checkbox fixes.
- High Contrast state can refresh in both directions without replacing the
  selected System, Light, or Dark preference.
- Dialog centering uses and clamps to the owner monitor's working area.
- Delivery Health list columns scale from the active window DPI.
- A normal developer build places `PDW.pdf` beside the executable for F1 help.
- Manual and Windows `/startup` launches create the correctly titled,
  responsive main window.
- Native-window smoke automation keeps exactly one modeless Settings instance,
  moves between General, Appearance, and Signal & radio without closing it,
  routes a live-meter click to Signal & radio, and returns cleanly after a
  retained modal dialog closes.
- At 100% scale, the approved dark and light Settings layouts render at
  1000x720; dark compact/minimum relayout renders at 820x600 and 720x560 with
  all Signal & radio cards present and no stale text/button pixels.
- The main command bar remains 54 pixels high with all six icon labels visible,
  while Pane 1 begins at pixel 55, on the first settled frame, after a display
  change, and through repeated restore/maximize cycles.
- The merged General > Backup / Restore route keeps all 11 Settings
  destinations, opens an unclipped 769x440 dark dialog, disables Settings while
  modal, and restores Settings interaction when closed without changing data.

These checks reduce risk but do not mark the matrices below as passed.

## Display-scale matrix

Test each row at 100%, 125%, 150%, and 200%. Include a 1366 by 768 or smaller
effective workspace and, when available, movement between monitors with
different scale factors.

| Surface | 100% | 125% | 150% | 200% | Required result |
| --- | --- | --- | --- | --- | --- |
| Main panes, toolbar, signal indicators | [x] | [ ] | [ ] | [ ] | No overlap, missing text, inaccessible pane, or stale layout after monitor movement. |
| Settings hub | [x] | [ ] | [ ] | [ ] | Every category and bottom action remains visible and keyboard reachable. |
| Radio and Signal Sources | [x] | [ ] | [ ] | [ ] | Receiver list, source test, recording, replay, calibration, waveform and status remain usable. |
| Screen Options and FLEX assembly row | [ ] | [ ] | [ ] | [ ] | The full compatibility wording and Group Mode relationship remain visible. |
| Data Outputs, all tabs | [ ] | [ ] | [ ] | [ ] | Tab pages, enable/permission controls, fields, test actions and OK/Cancel remain reachable. |
| Delivery Health | [ ] | [ ] | [ ] | [ ] | Columns, history, acknowledgement and close controls remain readable and reachable. |
| Publishing, FTP, Apprise and SMTP | [ ] | [ ] | [ ] | [ ] | Privacy text wraps; masked fields, status and all actions remain visible. |
| Filters and filter options | [ ] | [ ] | [ ] | [ ] | List, search, edit, labels, output and duplicate controls remain recognizable. |
| Legacy input/custom audio and colour dialogs | [ ] | [ ] | [ ] | [ ] | Every legacy receiver, slicer and presentation option remains available. |

## Theme and contrast matrix

Repeat with at least one primary dialog left open while changing the state.

| State | Main window | Settings | Data Outputs | Delivery Health | High Contrast off recovery |
| --- | --- | --- | --- | --- | --- |
| Follow Windows / light | [ ] | [ ] | [ ] | [ ] | [ ] |
| Follow Windows / dark | [x] | [x] | [ ] | [ ] | [ ] |
| Explicit Light | [x] | [x] | [ ] | [ ] | [ ] |
| Explicit Dark | [x] | [x] | [ ] | [ ] | [ ] |
| High Contrast enabled | [ ] | [ ] | [ ] | [ ] | N/A |

Required result: readable text and focus indication, no theme-stuck controls,
and turning High Contrast off restores the selected PDW theme.

## Keyboard-only matrix

Starting from the main window, complete each workflow with Tab, Shift+Tab,
arrow keys, Space, Enter, Escape, menu accelerators and F1 only:

- [ ] Open and close every Settings category without changing a value.
- [ ] Toggle and restore the System Tray checkboxes.
- [ ] Toggle and restore Enable filter file output.
- [ ] Toggle and restore SMTP and SMTP authentication.
- [ ] Traverse every Data Outputs tab without entering or transmitting data.
- [ ] Reach Publishing, FTP, Apprise and Delivery Health status/actions.
- [ ] Open F1 help and Settings > Volume; confirm current Windows routing.
- [ ] Cancel each dialog and confirm settings remain unchanged.
- [ ] Save one reversible non-secret appearance setting and confirm it survives restart.

## Acceptance record

10 August 2026, Windows development machine, 100% scale: the current working
tree passed the main-window and Settings Light/Dark image review, repeated
1000x720 -> 720x560 -> 1000x720 -> 820x600 relayout, single-instance Settings,
page switching, live-meter navigation, and modal return smoke. Content-free
captures are stored locally under `out\ui-*.png`. This record does not cover
live radio input, High Contrast, keyboard-only completion, other dialogs, or
125-200% scaling.

10 August 2026 command-bar regression: initial and post-display/resize captures
show Source, Pause, Record, Filters, Clear, and Settings beneath their icons.
Geometry checks held the toolbar at 54 pixels and Pane 1 at y=55 through three
restore/maximize cycles. This closes the clipped-label/column-header overlap at
100% scale; other DPI levels remain open.

10 August 2026 merged-backup regression: General > Backup / Restore rendered
at 769x440 in the Windows-following dark palette with all explanatory text and
actions visible. Native-window checks counted all 11 Settings destinations and
confirmed correct modal disable/re-enable behavior. Export and restore were not
run during this visual smoke; cryptographic round-trip and failure behavior are
covered by the configuration-backup core suite.

For each run record Windows version/build, display resolution, scale, monitor
arrangement, PDW commit, result, and a content-free defect description. A
failure remains open until the same matrix cell is repeated successfully on a
fixed build. Never attach operator INI files or private runtime artifacts.
