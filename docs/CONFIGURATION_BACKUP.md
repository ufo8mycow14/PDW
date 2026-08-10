# Configuration backup and restore

`Settings > General > Backup / Restore` exports the current PDW configuration to one portable
`.pdwbackup` file. The standard Windows Save dialog allows the file to be stored on any available
local, removable, or network location.

The backup contains:

- every value currently saved in `PDW.INI`, including receiver selection, frequency, gain, PPM,
  display, decoder, notification, publishing, transfer and data-output settings;
- every saved filter in `filters.ini`;
- PDW usernames and secrets held in Windows Credential Manager for Apprise, publishing, file
  transfer, MQTT, MySQL and other supported data outputs.

The complete file is protected with a user-chosen password using AES-256-GCM and
PBKDF2-HMAC-SHA256. Its contents, including legacy settings and credentials, are not stored as
readable text. PDW cannot recover a forgotten backup password.

Restore first authenticates and validates the complete backup, asks for confirmation, then replaces
the active configuration using durable temporary files. If a write fails, PDW attempts to roll back
to the original files and credentials. After a successful restore PDW closes; reopen it to load the
restored configuration cleanly. Credential records are retargeted to the current PDW folder so a
backup can be restored after PDW is moved or installed elsewhere.

This is a configuration backup, not a message-data archive. It does not copy decoded messages,
logs, recordings, output queues, generated website files, receiver DLLs, or other program binaries.
Their configured paths, selected receiver and radio frequency remain included through `PDW.INI`.
