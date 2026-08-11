# Capcode Directory filters

The Capcode Directory is PDW's single persistent source for address descriptions and message
filter rules. The database is stored at the Message History path configured in Settings; history
capture may remain disabled while the directory and filters continue to work.

## Matching, display, and routing

- **Filter** sends a matching rule to PDW's lower Filtered panel. It is independent from output
  delivery: a rule can filter locally, deliver to selected outputs, or do both.
- A capcode rule with an empty **Filter by keywords** field matches the capcode alone.
- Enter one word to require that keyword. Put `+` between two to ten keywords to require every
  keyword, in any order. For example, `PR:1+Traffic` matches only messages containing both
  `PR:1` and `Traffic`. Matching ignores case.
- **Enable exact message match** changes the keyword test into a case-insensitive comparison of the
  complete pager message. Do not enable it for a `+` keyword expression.
- When several rules can match the same capcode, PDW evaluates the most specific message condition
  first (exact, then multi-keyword, then single-keyword, then capcode-only) so a broad directory row
  cannot hide a more specific output route.
- Existing legacy `&` expressions remain supported for compatibility.
- **Reject** suppresses a matched message using the existing PDW rejection path.
- **Monitor Only** keeps a match in the upper monitor instead of the filtered pane.
- **Filter** and **Monitor Only** are mutually exclusive because they select different panes.
- **Display name** is the one friendly label used by the live filter, history, outputs, and command
  arguments. There is no separate editable Filter label. **Agency in display** can hide the agency,
  place it before the display name, or place it after the display name.
- **Show display name**, **In monitor window & monitor logfile**, and **On a new line** control where
  the friendly display name is rendered.

## Per-rule outputs

**Send to enabled outputs** exposes Email, Apprise, Publishing, MQTT, SQLite, MySQL/ODBC, Telnet,
and Windows notification choices. Selecting a destination on a directory rule never enables it.
The corresponding destination and any required output-group/privacy acknowledgement must also be
enabled and configured in Settings. This two-step gate allows different capcodes to feed different
combinations of destinations without preventing the same rule from appearing in the lower Filtered
panel.

The optional separate CSV setting accepts up to three file paths. All three fields are functional:
each selected file receives the same matched row using the shared column selection. The shared
command-file switch applies the configured command to matching rules; the former duplicate
per-directory command checkbox has been removed.

The runtime still uses PDW's established protocol-specific matcher and display/logging paths. A
directory row is converted into an in-memory legacy filter; no decoder or slicer algorithm is
replaced. Changes are reloaded immediately after save, delete, import or explicit reload.

## CSV transfer

Import and Export use UTF-8 CSV. The first seven columns remain compatible with the original
Capcode Directory format: `protocol,address,display_name,agency,color,notes,enabled`. Additional
columns carry filter type, required keywords, legacy compatibility fields, `filter_enabled`, whether
explicit output routing is configured, the selected output bit mask, agency-label placement,
reject/exact/monitor settings, separate files, sound/colour selection, hit count and last-hit time.
The legacy `filter_label` column is still accepted and exported for older tools but is normalized to
`display_name`; when an older row has only a Filter Label, it becomes the Display name. Invalid rows
are rejected and reported. A previously disabled legacy row stays internally dormant until it is
deliberately edited and saved with the new controls.

Filtered and separate message CSV files use the operator-selected subset of:
`Capcode, Time, Date, Mode, Type, Bitrate, Message`.

## Legacy migration

Fresh packages do not include `filters.ini`. At startup, if an older `filters.ini` exists, PDW merges
all readable legacy rules into the Capcode Directory. Equivalent rules are updated rather than
duplicated, while existing directory names, agencies, colours, notes and disabled state are retained.
A known generator pattern where the label was copied into the required message-text field is repaired
for capcode rules. After success, the original file is renamed to a unique `.migrated` backup; it is
never silently deleted. If import fails, the file remains unchanged and the directory is not
partially replaced. PDW records the successful migration, so an obsolete scheduled generator cannot
re-import a newly recreated `filters.ini` on later starts. Retire that scheduled task and use the
directory's CSV Import/Export controls for future bulk maintenance.

Encrypted configuration backups now contain the complete Capcode Directory CSV. Backups from the
older `filters.ini` format are recognized and migrated during restore.
