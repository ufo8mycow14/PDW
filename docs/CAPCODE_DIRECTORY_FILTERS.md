# Capcode Directory filters

The Capcode Directory is PDW's single persistent source for address descriptions and message
filter rules. The database is stored at the Message History path configured in Settings; history
capture may remain disabled while the directory and filters continue to work.

## Matching and display

- A capcode rule with an empty **Text** field matches the capcode alone.
- **Match EXACT message** requires the complete pager message to equal **Text**.
- Put `+` between two or more words to require every word, in any order. For example,
  `PR1+Traffic` matches only messages containing both `PR1` and `Traffic`. Matching ignores case;
  leave **Match EXACT message** off when using this syntax.
- Existing legacy `&` expressions remain supported for compatibility.
- **Reject** suppresses a matched message using the existing PDW rejection path.
- **Monitor Only** keeps a match in the upper monitor instead of the filtered pane.
- **Show Filter Label**, **Show descriptions in Monitor window and logfile**, and
  **Description on a new line** control where the friendly description is rendered.

The runtime still uses PDW's established protocol-specific matcher and display/logging paths. A
directory row is converted into an in-memory legacy filter; no decoder or slicer algorithm is
replaced. Changes are reloaded immediately after save, delete, import or explicit reload.

## CSV transfer

Import and Export use UTF-8 CSV. The first seven columns remain compatible with the original
Capcode Directory format: `protocol,address,display_name,agency,color,notes,enabled`. Additional
columns carry filter type, required text, label, reject/exact/command/monitor settings, separate
files, sound/colour selection, hit count and last-hit time. Invalid rows are rejected and reported.

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
