# PDW v5.2 2026 Release handover

Updated: 11 August 2026

## Current release identity

- Repository: `C:\PDW Update\tmp\pdw-message-history-csv`
- Active branch: `pdw-v5.2-message-history-csv`
- Product/display name: **PDW v5.2 2026 Release**
- Executable: `PDW v5.2 2026 Release.exe`
- Product version: `5.2.0 2026 Release`
- File version: `5.2.0.0`
- Installer: `PDW-v5.2-2026-Release-Setup.exe`
- Portable packages: `PDW-v5.2-2026-Release-Win32` and
  `PDW-v5.2-2026-Release-x64`

The v5.2 release is one PDW product. The installer contains both application
architectures and selects x64 by default on 64-bit Windows while retaining a
clear Win32 compatibility choice. The portable folder workflow remains fully
supported.

`Headers/version.h` is the authoritative application identity. CMake reads the
display name from that header for the output filename; the About dialog and
Settings **About me** page render the same display macro. The installer,
packaging, audit scripts, workflow names, documentation, and manifests are
aligned to the v5.2 identity.

## Guided installer

The Inno Setup definition is `installer/PDW.iss`. It produces one guided,
per-user installer that:

- installs without requiring administrator privileges;
- offers x64 or Win32 on 64-bit Windows and automatically uses Win32 on
  32-bit Windows;
- supports `/ARCH=x64` and `/ARCH=Win32` for unattended deployment;
- keeps `PDW.INI`, filters, receivers, WAV files, logs, and the executable in
  one selected PDW installation folder;
- leaves same-user Windows Credential Manager records available;
- preserves operator INI files, filters, receivers, WAV files, logs, queues,
  recordings, and databases on upgrade;
- does not remove operator configuration during uninstall;
- offers Start menu, optional Desktop, and optional Windows startup shortcuts;
  and
- can sign Setup and its uninstaller when an approved Authenticode signing
  command is supplied.

Tracked default WAV files and required legacy data are staged from `packaging`
so a clean clone can build the installer. Obsolete VxD drivers are not installed
automatically. Architecture-specific receiver rules remain enforced: Win32 can
include the redistributed x86 RTL-SDR DLL; x64 excludes it and supports matching
x64 custom libraries or the architecture-neutral `rtl_tcp` path.

## Current verification

Local Visual Studio 2022 Release builds completed for Win32 and x64. Each
architecture passes **28 of 28 CTest tests**, and the optional WinMM and WASAPI
device-smoke targets compile for both architectures. The following physical
audio measurements are retained predecessor-v5 evidence and were not rerun for
the v5.2 candidate:

- Win32 WinMM: 44,100 bytes at 44.1 kHz, 8-bit mono;
- Win32 WASAPI: 48,480 samples at 48 kHz;
- x64 WinMM: 44,100 bytes at 44.1 kHz, 8-bit mono; and
- x64 WASAPI: 48,000 samples at 48 kHz.

The current executables report:

- Win32 PE machine `0x014C`, file version `5.2.0.0`, product version
  `5.2.0 2026 Release`;
- x64 PE machine `0x8664`, file version `5.2.0.0`, product version
  `5.2.0 2026 Release`; and
- embedded manifest version `5.2.0.0` with Per-Monitor V2 DPI awareness.

Content-free native UI review passed for the predecessor-v5 application on both
architectures. A v5.2 native Light/Dark and compact-size UI smoke remains open.
The shared resources and binary metadata now carry **PDW v5.2 2026 Release**;
visual confirmation of the main window, new dialogs, About dialog, and Settings
**About me** page remains part of that open UI gate.

The current v5.2 isolated installer smoke passes for x64 and Win32. It verifies application
architecture and version, settings co-location, upgrade preservation of
modified settings and custom receivers, removal of the exact renamed v5 and
v5.1 predecessor executables, clean uninstall, and
retention of operator-owned configuration after uninstall. Microsoft Defender
reported no threat in the locally generated candidate.

## Signing and public-release boundary

The local installer candidate is intentionally not described as the public
stable release because no trusted publisher certificate is configured. The
release scripts support signing and `-RequireSignature`; the public-release
path must fail when the signature is absent or invalid.

Before attaching the stable installer to a GitHub Release:

1. approve a trusted publisher identity and Authenticode certificate or an
   approved managed signing service;
2. sign both application executables and the installer/uninstaller;
3. verify the signature chain and timestamp on a clean Windows machine;
4. scan the signed artifacts with Microsoft Defender;
5. rerun the isolated dual-architecture installer smoke; and
6. if Defender or SmartScreen reports a false positive, submit the exact signed
   artifact to Microsoft rather than weakening application or installer
   security.

Signing reduces unknown-publisher warnings and establishes identity and
integrity. It cannot guarantee immediate SmartScreen reputation or that no
security product will ever inspect the application.

## Compatibility and privacy boundaries

- Preserve POCSAG, FLEX, ACARS, MOBITEX, and ERMES legacy protocol behavior.
- Keep WinMM, WASAPI fallback, serial slicers, `.rec` playback, INI files,
  filters, logs, WAV alerts, and established hard-decision parsers available.
- Keep SMTP, Apprise, FTP/FTPS/SFTP, static publishing, webhooks, MQTT,
  SQLite, ODBC/MySQL, Telnet, and Windows notifications intact and independent.
- Publishing and every optional adapter remain disabled by default. Operators
  must consider the laws and permissions that apply in their own country.
- Never commit or package pager traffic, operator logs, credentials, queues,
  recordings, databases, or personal configuration.
- Secrets remain in Windows Credential Manager and are excluded from INI
  files, logs, screenshots, packages, and chat.
- Git source, local builds, portable packages, installer candidates, test
  installations, pushed branches, pull requests, CI, tags, and GitHub Releases
  are separate states and must be reported separately.

## Rebuild and audit

```powershell
.\scripts\build-dependencies.ps1
cmake -S . -B out\v5.2-build-win32 -A Win32
cmake --build out\v5.2-build-win32 --config Release --target clean
cmake --build out\v5.2-build-win32 --config Release --parallel
ctest --test-dir out\v5.2-build-win32 -C Release --output-on-failure

.\scripts\build-dependencies.ps1 -Architecture x64
cmake -S . -B out\v5.2-build-x64 -A x64 `
  -DPDW_DEPENDENCY_ROOT="$PWD\out\dependencies\x64"
cmake --build out\v5.2-build-x64 --config Release --target clean
cmake --build out\v5.2-build-x64 --config Release --parallel
ctest --test-dir out\v5.2-build-x64 -C Release --output-on-failure

.\scripts\stage-installer-input.ps1 -Architecture Win32 `
  -BuildDirectory out\v5.2-build-win32\Release `
  -Destination out\v5.2-installer-input\Win32
.\scripts\stage-installer-input.ps1 -Architecture x64 `
  -BuildDirectory out\v5.2-build-x64\Release `
  -Destination out\v5.2-installer-input\x64
.\scripts\build-installer.ps1 `
  -Win32ApplicationDirectory out\v5.2-installer-input\Win32 `
  -X64ApplicationDirectory out\v5.2-installer-input\x64 `
  -OutputDirectory out\v5.2-installer
.\scripts\audit-installer.ps1 `
  -Setup 'out\v5.2-installer\PDW-v5.2-2026-Release-Setup.exe'
.\tests\installer_smoke.ps1 `
  -Setup 'out\v5.2-installer\PDW-v5.2-2026-Release-Setup.exe' `
  -TestRoot out\v5.2-installer-smoke

.\scripts\audit-release.ps1
```

Public release builds add the approved signing command to
`build-installer.ps1` and require signatures during the installer audit.

## Remaining acceptance work

1. Obtain and configure the trusted publisher signing identity.
2. Complete keyboard-only, High Contrast, and 125%, 150%, and 200% DPI checks.
3. Verify WinMM/WASAPI device loss and recovery, `rtl_tcp`, and physical
   supported RTL-SDR devices on intended hardware.
4. Extend synthetic decoder fixtures with audio, FLEX, filtering, duplicate,
   correction, and remaining protocol coverage.
5. Exercise secure outputs with disposable non-private test services.

Historical v4.1, v4.5, v4.6.0, and v4.6.1 references remain only where they
describe earlier release evidence or changelog history. They do not define the
current product identity.

## Publication workflow

- Inspect and commit the complete v5.2 release diff.
- Push `pdw-v5.2-message-history-csv` to the writable `fork` remote.
- Open a draft pull request against the fork's `master` and let dual CI,
  installer smoke, CodeQL, and artifact checks complete.
- Merge the source PR only after dual CI, installer smoke, and security checks
  pass. Do not tag or publish a public installer until the signing gate passes.
- Retain earlier branches and packages as rollback evidence.
