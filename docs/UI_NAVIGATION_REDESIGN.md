# PDW navigation and settings redesign

Updated: 10 August 2026

## Implementation status

Implemented locally on 10 August 2026 against the approved redesign images.
The Release build and all 23 automated tests pass. Native-window smoke verifies
one persistent Settings window, navigation, live-meter routing, retained modal
handoff, Light/Dark rendering, and resize-safe layouts at 1000x720, 820x600,
and the 720x560 minimum. Live-radio movement, High Contrast, keyboard-only
completion, and 125-200% DPI remain acceptance gates.

## Decision

PDW should retain a compact native Windows desktop shell, but replace the
current chain of modal settings dialogs with one persistent, modeless Settings
Center. The top of the main window should use a restrained command bar for
frequent actions and a truthful live signal meter. The traditional menu remains
for complete keyboard-accessible command discovery.

This design preserves the current v4.1+ compatibility direction, decoder behaviour,
Win32 compatibility, existing configuration fields, and current command IDs.
It changes navigation and presentation—not protocol logic or saved-data
meaning.

Microsoft recommends an adaptive navigation pane for a consistent experience
across many categories, with a persistent Settings entry and page header. It
also recommends that command bars keep the most important actions visible,
move secondary actions to overflow, and pair command icons with short labels.

- Navigation guidance:
  <https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/navigationview>
- Command-bar guidance:
  <https://learn.microsoft.com/en-us/windows/apps/design/controls/command-bar>
- Windows icon guidance:
  <https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font>

## Current problems

1. The Settings hub is modal. Selecting a page closes the hub, opens another
   modal dialog, and returns the user to the monitor instead of Settings.
2. Settings are duplicated across Monitor, Settings, nested submenus, toolbar
   actions, and context menus without one stable information architecture.
3. The toolbar is text-only and gives every action similar visual weight.
4. Input, decoder, recording, diagnostics, transfer, and delivery-health state
   are separated from the commands used to manage them.
5. The top-right signal UI is custom-painted decoration rather than an
   interactive control. Its update code is currently guarded by
   `old_rect_flg`, which is initialized to false and never enabled, so normal
   `UpdateSigInd()` calls cannot advance the meter.
6. The `Q` status and five bars do not explain whether PDW is disconnected,
   paused, receiving noise, seeing a transmission, clipping, or decoding
   poorly.

## Proposed main-window structure

### Menu bar

Use six stable top-level menus:

#### File

- Log file…
- Record signal
- Play recording…
- Export copied data…
- Print…
- Exit

#### Monitor

- Signal source and radio…
- Pause / Resume
- Decoder mode
  - POCSAG / FLEX
  - ACARS
  - MOBITEX
  - ERMES
- Statistics…
- Diagnostics…

#### Filters

- Manage filters…
- Filter options…
- Reload filters
- Reset all hit counters…
- Advanced
  - Write filter file
  - Run filter command file

#### Outputs

- Delivery health…
- Publish to web…
- File transfer…
- Push and Windows notifications…
- Email…
- Data outputs…

Delivery Health appears first because it answers “is delivery working?” before
the user changes configuration.

#### View

- Focus monitor pane / Focus filtered pane
- View and columns…
- Scrollback…
- Theme
  - Follow Windows
  - Light
  - Dark
- Text and colours…
- System tray…

#### Help

- User guide
- Keyboard shortcuts
- About PDW

Copy Selection remains `Ctrl+C` and stays in the pane context menu. The rarely
used whole-monitor and whole-filtered-window copy commands move to that same
context menu. Removing a shallow Edit menu reduces top-level scanning without
removing any functionality.

### Command bar

Keep six frequent, directly labeled commands visible:

| Command | Icon concept | Behaviour |
| --- | --- | --- |
| Source | Radio/antenna | Opens Signal & radio in Settings |
| Pause / Resume | Pause/play toggle | Shows the actual current state |
| Record | Record/stop toggle | Shows recording state and elapsed time |
| Filters | Funnel | Opens filter management |
| Clear | Eraser | Clears after the existing confirmation rules |
| Settings | Gear | Opens or focuses the Settings Center |

Log, Copy, Statistics, mode selection, and less-frequent actions move to the
menu, context menu, status area, or command overflow. This prevents the bar
from becoming another full menu.

Use monochrome 20px icons with visible one-word labels and tooltips. Because
Segoe Fluent Icons is not included by default on Windows 10, PDW should use
embedded transparent vector-like/GDI or icon resources, or provide a tested
Segoe MDL2 fallback. Do not depend solely on a Windows 11 font.

## Settings Center

The Settings Center is a single-instance, modeless window. Opening Settings
again focuses the existing window. Monitoring and decoding continue while it
is open.

The left navigation remains visible and contains:

1. General
2. Appearance
3. Display
4. Decoder
5. Signal & radio
6. Filters
7. Notifications
8. Transfers & publishing
9. Data outputs
10. Health & diagnostics
11. About me

At wide sizes the navigation displays icons and labels. At narrower sizes it
retains readable icon-and-label rows in a narrower rail. The content header
contains the current page title, Revert, and Apply changes.

### Navigation behaviour

- Changing pages never closes Settings.
- Draft values remain in memory while moving between pages.
- Apply validates and writes the current draft as one intentional action.
- Revert restores the last applied values for the current page.
- Closing with unapplied changes offers Apply, Discard, or Continue editing.
- Complex connection pages retain Test controls; testing does not save secrets.
- Errors appear beside the relevant setting and focus the first invalid field.
- `Ctrl+,` opens/focuses Settings; `Ctrl+F` inside Settings searches settings,
  while the main-window `Ctrl+F` continues to open filters.
- The last visited Settings page is restored for the current session only.

### Existing-dialog migration

Existing storage fields and validation logic are retained rather than
rewritten. The first implementation provides navigable summary cards in the
Settings Center and opens the matching legacy editor on demand. Settings stays
alive and returns to the same page when that editor closes. Individual editors
can move in-process later without changing their storage semantics.

## Live signal meter

Replace the `Q` box and fixed five-bar decoration with one clickable input
meter at the right side of the command bar.

### Information shown

- source state: Live input, Paused, Reconnecting, Playback, or No input;
- source name, such as Local audio, RTL-SDR, or RTL-TCP;
- current RMS level or dBFS value;
- moving waveform/level history from real input samples;
- peak-hold marker with controlled decay;
- quality or clipping warning when supported by the source.

### Motion rules

- Refresh visually at 20-30 frames per second, independent of decoder-message
  arrivals.
- When an input is active, display its real noise floor so the meter remains
  visibly alive between transmissions.
- Do not generate fake random movement. If samples stop, show No input,
  Reconnecting, or Paused and freeze/decay the trace appropriately.
- A transmission raises the live level and peak marker toward the full scale.
- Hold the peak for approximately 400ms, then decay smoothly.
- Honour reduced-motion and high-contrast Windows settings.

### Interaction

- Click opens **Settings > Signal & radio**.
- Tooltip/accessibility text reports source, state, level, and quality.
- Normal activity uses the theme accent; clipping uses warning colour; source
  failure uses the error colour. State must also be communicated by text, not
  colour alone.

### Technical correction

Remove the obsolete `old_rect_flg` gate. Feed a small thread-safe meter state
from the normalized audio/radio sample boundary, not from protocol-specific
decode branches. A UI timer invalidates only the meter rectangle. Decoder and
capture threads must never paint the toolbar directly.

## Status bar

Use a quiet bottom status bar for persistent state rather than adding more
toolbar buttons:

- current source and connection state;
- active decoder mode;
- recording/playback state when applicable;
- output-health summary and pending alert count.

Each status region is clickable and opens the related Settings page. No decoded
message text appears in the status bar or Delivery Health.

## Keyboard and accessibility

- Preserve existing shortcuts unless they conflict with a standard Windows
  convention.
- Add visible shortcut text to complete menu items.
- Every command-bar icon retains a text label; icon-only compact mode uses an
  accessible name and tooltip.
- Keep logical tab order and visible focus indicators.
- Support 100%, 125%, 150%, and 200% scaling, high contrast, and keyboard-only
  navigation.
- Do not use animation as the only signal of receiving or failure.

## Safe implementation order

1. Correct the signal-state model and meter update boundary without changing
   decoder output.
2. Reorganize menu resources while preserving command IDs and accelerators.
3. Replace the text toolbar with labeled icon commands and a real signal-meter
   child control.
4. Create the single-instance modeless Settings Center and navigation shell.
5. Move General, Appearance, Display, Decoder, and Signal pages first.
6. Move Filters, Notifications, Transfers, Publishing, Data Outputs, and
   Delivery Health.
7. Route old menu and context-menu entry points to the matching Settings page.
8. Remove superseded modal routes only after configuration round-trip and
   visual tests pass.

## Acceptance criteria

- A user can move between every settings category without closing Settings.
- Opening Settings twice focuses one window rather than creating duplicates.
- Monitoring and the live meter continue while Settings is open.
- Every existing setting remains reachable and retains its storage semantics.
- Menu commands and shortcuts invoke the same operation as before.
- The signal meter moves from real active-input samples, reaches peak on a test
  transmission, and clearly shows paused/disconnected states.
- The command bar remains usable at the minimum supported window size.
- No control or label clips at supported scaling and theme combinations.
- Win32 Release build, automated tests, INI round-trip tests, decoder fixtures,
  and targeted UI checks remain green.
