# PDW v5 2026 Release installation

`PDW-v5-2026-Release-Setup.exe` is the recommended distribution for ordinary
Windows users. It contains both maintained PDW architectures and installs per
user without requiring administrator rights. The portable packages remain
available for existing deployments and recovery; they run the same PDW code.

## Guided Setup

Setup displays the licence and the worldwide monitoring/publication reminder,
then guides the user through:

1. an installation folder under `%LOCALAPPDATA%\Programs\PDW` by default;
2. x64 or Win32 compatibility selection on 64-bit Windows;
3. confirmation that the application, `PDW.INI`, filters, receivers, WAV
   files, and logs remain together in that PDW installation folder;
4. Start Menu, optional Desktop, and optional delayed Windows-startup
   shortcuts; and
5. a final option to launch PDW.

Use x64 with Windows audio, `rtl_tcp`, and matching x64 receiver DLLs. Choose
Win32 compatibility for the bundled x86 RTL-SDR library or another x86-only
receiver. On 32-bit Windows, Setup selects Win32 automatically.

Setup never enables SMTP, Apprise, publishing, file transfer, MQTT, database,
Telnet, or Windows notification output. It never copies passwords into its log.
Existing supported credentials remain available through Windows Credential
Manager for the same Windows user.

Setup does not offer a separate settings or data location. To bring settings
from another portable or installed copy, finish installation and use
**Settings > General > Backup / Restore** inside PDW.

## Upgrade and uninstall behavior

The stable Setup application ID recognises future PDW v5 installers as
upgrades. Application files and documentation are refreshed, while `PDW.INI`,
`filters.ini`, receiver additions, WAV files, logs, recordings, queues, and
other operator-created data are not overwritten.

Uninstall removes the installed application and shortcuts. Configuration,
filters, receiver additions, WAV files, logs, recordings, and other operator
data remain by default so an accidental uninstall does not destroy them. They
can be reviewed and removed manually after a backup when no longer required.

Portable use is unchanged: start `PDW v5 2026 Release.exe` in a writable folder
containing `PDW.INI`. No installed service, background updater, or driver is
required.

## Building Setup

The installer recipe is stable across normal PDW code changes. Each release
rebuilds the Win32 and x64 executables, stages reviewed inputs, compiles Setup,
and tests both architecture paths:

```powershell
.\scripts\stage-installer-input.ps1 -Architecture Win32 `
  -BuildDirectory out\build-win32\Release `
  -Destination out\installer-input\Win32
.\scripts\stage-installer-input.ps1 -Architecture x64 `
  -BuildDirectory out\build-x64\Release `
  -Destination out\installer-input\x64

.\scripts\build-installer.ps1 `
  -Win32ApplicationDirectory out\installer-input\Win32 `
  -X64ApplicationDirectory out\installer-input\x64 `
  -OutputDirectory out\installer `
  -ScanWithDefender

.\tests\installer_smoke.ps1 `
  -Setup out\installer\PDW-v5-2026-Release-Setup.exe `
  -TestRoot out\installer-smoke
```

`installer\PDW.iss` changes only when installation contents or behavior change;
ordinary decoder, UI, receiver, or output code changes require a rebuild but
not a rewritten installer.

## Public signing gate

An unsigned CI installer is a test artifact, not a public release. Before
publication, sign the PDW executables, Setup, and the generated uninstaller
with the same trusted Authenticode publisher identity and a trusted timestamp.
Then run:

```powershell
.\scripts\audit-installer.ps1 `
  -Setup out\installer\PDW-v5-2026-Release-Setup.exe `
  -RequireSignature -ScanWithDefender
```

Any missing/invalid signature, Defender detection, secret-bearing default,
architecture mismatch, separated settings storage, failed upgrade, or data-removing
uninstall blocks public release.
