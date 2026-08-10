# PDW v5 2026 Release handover

Updated: 10 August 2026

## Current release identity

- Repository: `C:\PDW Update\PDW-source`
- Active branch: `pdw-v5-2026-release`
- Product/display name: **PDW v5 2026 Release**
- Executable: `PDW v5 2026 Release.exe`
- Product version: `5.0.0 2026 Release`
- File version: `5.0.0.0`
- Installer: `PDW-v5-2026-Release-Setup.exe`
- Portable packages: `PDW-v5-2026-Release-Win32` and
  `PDW-v5-2026-Release-x64`

The v5 release is one PDW product. The installer contains both application
architectures and selects x64 by default on 64-bit Windows while retaining a
clear Win32 compatibility choice. The portable folder workflow remains fully
supported.

`Headers/version.h` is the authoritative application identity. CMake reads the
display name from that header for the output filename; the About dialog and
Settings **About me** page render the same display macro. The installer,
packaging, audit scripts, workflow names, documentation, and manifests are
aligned to the v5 identity.

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
architecture passes **24 of 24 CTest tests**. Optional live audio smoke also
passed on the development machine:

- Win32 WinMM: 44,100 bytes at 44.1 kHz, 8-bit mono;
- Win32 WASAPI: 48,480 samples at 48 kHz;
- x64 WinMM: 44,100 bytes at 44.1 kHz, 8-bit mono; and
- x64 WASAPI: 48,000 samples at 48 kHz.

The current executables report:

- Win32 PE machine `0x014C`, file version `5.0.0.0`, product version
  `5.0.0 2026 Release`;
- x64 PE machine `0x8664`, file version `5.0.0.0`, product version
  `5.0.0 2026 Release`; and
- embedded manifest version `5.0.0.0` with Per-Monitor V2 DPI awareness.

Content-free native UI review passed for both application architectures. The
main window, menu bar, six-command toolbar, pane headings, status bar, and Live
Input percentage render without overlap. The About dialog and Settings
**About me** page display **PDW v5 2026 Release**; the contributor line no
longer carries an obsolete Beta suffix.

The isolated installer smoke passes for x64 and Win32. It verifies application
architecture and version, settings co-location, upgrade preservation of
modified settings and custom receivers, clean uninstall, and
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
cmake -S . -B out\v5-build-win32 -A Win32
cmake --build out\v5-build-win32 --config Release --target clean
cmake --build out\v5-build-win32 --config Release --parallel
ctest --test-dir out\v5-build-win32 -C Release --output-on-failure

.\scripts\build-dependencies.ps1 -Architecture x64
cmake -S . -B out\v5-build-x64 -A x64 `
  -DPDW_DEPENDENCY_ROOT="$PWD\out\dependencies\x64"
cmake --build out\v5-build-x64 --config Release --target clean
cmake --build out\v5-build-x64 --config Release --parallel
ctest --test-dir out\v5-build-x64 -C Release --output-on-failure

.\scripts\stage-installer-input.ps1 -Architecture Win32 `
  -BuildDirectory out\v5-build-win32\Release `
  -Destination out\v5-installer-input\Win32
.\scripts\stage-installer-input.ps1 -Architecture x64 `
  -BuildDirectory out\v5-build-x64\Release `
  -Destination out\v5-installer-input\x64
.\scripts\build-installer.ps1 `
  -Win32ApplicationDirectory out\v5-installer-input\Win32 `
  -X64ApplicationDirectory out\v5-installer-input\x64 `
  -OutputDirectory out\v5-installer
.\scripts\audit-installer.ps1 `
  -Setup 'out\v5-installer\PDW-v5-2026-Release-Setup.exe'
.\tests\installer_smoke.ps1 `
  -Setup 'out\v5-installer\PDW-v5-2026-Release-Setup.exe' `
  -TestRoot out\v5-installer-smoke

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

- Inspect and commit the complete v5 release diff.
- Push `pdw-v5-2026-release` to the writable `fork` remote.
- Open a draft pull request against the fork's `master` and let dual CI,
  installer smoke, CodeQL, and artifact checks complete.
- Do not merge/tag/publish the public stable installer until the signing gate
  passes.
- Retain earlier branches and packages as rollback evidence.
