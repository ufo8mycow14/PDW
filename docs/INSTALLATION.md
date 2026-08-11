# PDW v5.5.1 2026 Release installation

For beta testers, `PDW-v5.5.1-2026-Release-Setup.exe` is the recommended
distribution. It contains both maintained PDW architectures and installs per
user without requiring administrator rights. The portable packages remain
available for existing deployments and recovery; they run the same PDW code.

> [!WARNING]
> The 11 August 2026 Public Beta 2 Setup is intentionally unsigned under an
> explicit maintainer-approved beta exception. Windows may show an unknown
> publisher or SmartScreen warning. Download only from the maintained GitHub
> prerelease and verify its published SHA-256 checksum. It is not a signed
> stable release, and physical SDR#/VB-CABLE compatibility is still being
> evaluated by beta testers.

Setup enforces Windows build 10586 as the technical API floor. Production use
requires a Windows 10/11 edition and build still receiving Microsoft security
servicing (including applicable ESU or LTSC servicing), or a serviced Windows
Server 2016-or-newer release. The Visual Studio 2026/MSVC v145 build and Windows
SQLite binding no longer support Windows 7/8/8.1.

Each portable package and Setup architecture includes the matching release
app-local Microsoft Visual C++ runtime DLLs beside the PDW executable. No
`VC_redist` installer is launched and no UCRT is bundled; the supported Windows
floor supplies and services the UCRT. Packaging accepts only the reviewed VC145
filename set, validates every DLL's PE architecture and 14.5x file-version
family, and rejects debug or unlisted runtime files. These DLLs and
`PDW_BUILD_COMMIT.txt` are application-owned: upgrades refresh them, an
architecture switch first removes the complete reviewed filename set so an
opposite-bitness DLL cannot remain, and uninstall removes them while preserving
operator settings and data.

`PDW_BUILD_COMMIT.txt` records the exact Git commit and clean/dirty state seen
again when PDW is linked. Portable and installer staging require the marker to
name the current clean source `HEAD`; both installer architectures must match
each other and that same commit. This provenance gate prevents a later-dated
executable from being combined with source from a different revision.
All tracked portable and installer inputs are copied from one immutable archive
of that commit, rather than from the mutable working tree. Build outputs are
hash-checked before and after copying, and the working tree is rechecked before
publication. Each architecture's installer input also carries
`PDW_INSTALLER_INPUT_SHA256SUMS.txt`; Setup validates its exact file set and
every hash before and after compilation, but does not install this staging-only
manifest. The compiler writes to a unique temporary output directory; only a
candidate that also passes the final provenance, signature-policy, and optional
Defender gates is published. Portable output is published as one atomic
`*-package` directory containing the validated expanded folder and ZIP; Setup
uses the same pattern for its executable and checksum. A destination created
after preflight is never merged into or overwritten, and one half of a package
cannot remain publicly named when the other half fails.

Release audits also reject settings transaction and recovery artifacts such as
`PAP*.tmp`, `PDS*.tmp`, `.operator-swap*`, and
`*.pre-adelaide-flex-*.bak`. They can contain private operator configuration or
legacy mail credentials and are never portable or installer payloads.

The portable `Source` folder is a Git-archive snapshot rather than a Git
checkout. It therefore carries `PDW_SOURCE_PROVENANCE.txt` plus
`PDW_SOURCE_SHA256SUMS.txt`. CMake validates the exact clean archive commit and
every listed source-file hash both at configure and link time; modified or
incomplete extracted source fails closed. A clean configure writes a `pending`
build marker. Immediately before linking, PDW replaces any prior marker beside
the executable with `dirty`; only a successful link-time recheck may promote it
to `clean`. A link or validator failure therefore cannot leave a stale clean
marker beside a replaced executable.

## Guided Setup

Setup displays the licence and the worldwide monitoring/publication reminder,
then guides the user through:

1. an installation folder under `%LOCALAPPDATA%\Programs\PDW` by default;
2. x64 or Win32 compatibility selection on 64-bit Windows;
3. for a new settings file only, Standard PDW settings (the default) or the
   optional SDR# + VB-Audio Cable Adelaide FLEX profile;
4. confirmation that the application, `PDW.INI`, Capcode Directory, receivers, WAV
   files, and logs remain together in that PDW installation folder;
5. Start Menu, optional Desktop, and optional delayed Windows-startup
   shortcuts; and
6. a final option to launch PDW.

Use x64 with Windows audio, `rtl_tcp`, and matching x64 receiver DLLs. Choose
Win32 compatibility for the bundled x86 RTL-SDR library or another x86-only
receiver. On 32-bit Windows, Setup selects Win32 automatically.

Setup never enables SMTP, Apprise, publishing, file transfer, MQTT, database,
Telnet, or Windows notification output. It never copies passwords into its log.
Existing supported credentials remain available through Windows Credential
Manager for the same Windows user.

The Adelaide FLEX choice changes only the documented local-input, decoder and
slicer keys in
the new `PDW.INI`. SDR# and VB-Audio Cable are separate, operator-installed
products; Setup neither installs nor configures them. A pre-existing `PDW.INI`
always wins, so upgrades skip the profile choice and never replace current
settings. Existing and portable users can instead review and explicitly apply
the same preset from **Settings > Radio and Signal Sources > Apply Adelaide
FLEX...**. PDW requires one unambiguous active VB-Cable endpoint and saves its
stable Windows identity; a missing or ambiguous saved endpoint fails closed
instead of silently selecting another recording device.

The clean-install profile contains only its audited differences from Standard.
The in-app action is intentionally a complete known-good reset of local-input,
decoder and Custom-slicer values for this named workflow. PDW shows every group
that will change, defaults the confirmation to No, probes the exact endpoint
before writing, and preserves a verified byte-exact `PDW.INI` backup. It is not
an automatic migration and it does not touch Capcode Directory or filter data.

Setup does not offer a separate settings or data location. To bring settings
from another portable or installed copy, finish installation and use
**Settings > General > Backup / Restore** inside PDW.

## Upgrade and uninstall behavior

The stable Setup application ID recognises future PDW v5 installers as
upgrades. Application files and documentation are refreshed, while `PDW.INI`,
the Capcode Directory database, receiver additions, WAV files, logs, recordings, queues, and
other operator-created data are not overwritten.

On a normal upgrade, Setup reads `installation-architecture.txt` and preserves
the prior Win32 or x64 choice unless `/ARCH` or the visible architecture page is
used deliberately. If the destination is changed with **Browse**, the
architecture page re-reads the marker from that final folder before presenting
its default. Writing and re-reading that marker is a required Setup step;
if it cannot be persisted exactly, installation stops and the prior marker and
files are restored. When an operator explicitly changes architecture, Setup
copies the exact active `Receivers\RTL-SDR\rtlsdr.dll` to a uniquely named
`.pre-<architecture>-architecture*.bak` sibling before removing it from the
active receiver slot. Win32 can then install its reviewed x86 DLL; x64 leaves
the slot empty for RTL-TCP or an explicitly supplied compatible x64 receiver.
After a successful switch, the byte-verified `.bak` remains beside the receiver
as operator recovery data. If Setup is cancelled or fails after preservation,
it restores the exact active DLL and removes the temporary backup only after
the restored bytes verify. If either backup or restoration cannot be verified,
the recovery copy is retained and Setup reports the failure. Custom receiver
packages and configuration remain untouched.

Uninstall removes the installed application and shortcuts. Configuration,
the Capcode Directory, receiver additions, WAV files, logs, recordings, and other operator
data remain by default so an accidental uninstall does not destroy them. They
can be reviewed and removed manually after a backup when no longer required.

Current Setup and portable packages do not ship the obsolete, untracked
`COMPRT.VXD`, `Comprt2.vxd`, or `xp_driver.zip` Windows 9x/XP-era slicer-driver
artifacts. Their former inclusion depended on sibling files outside the source
tree and could not produce a deterministic clean-clone release. The maintained
Win32 COM/RS232 and serial-slicer paths remain available, and upgrade or
uninstall does not delete an operator's pre-existing legacy files.

Neither initial profile creates or replaces `filters.ini`. Fresh installations
remain filter-file-free. When an older copy is upgraded, PDW merges an existing
legacy file into the Capcode Directory without duplicating equivalent mappings,
fixes known duplicated label/text rows, then renames the original to a
recoverable `.migrated` backup. Profile selection and the in-app preset leave
the Capcode Directory, aliases, hit counters, history database, receivers, and
legacy recovery file untouched. Directory saves and CSV imports apply
immediately; no scheduled regeneration or manual reload is required.

Portable use is unchanged: start `PDW v5.5.1 2026 Release.exe` in a writable folder
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
  -Setup out\installer\PDW-v5.5.1-2026-Release-Setup-package\PDW-v5.5.1-2026-Release-Setup.exe `
  -TestRoot out\installer-smoke
```

`installer\PDW.iss` changes only when installation contents or behavior change;
ordinary decoder, UI, receiver, or output code changes require a rebuild but
not a rewritten installer.

## Stable-release signing gate

Unsigned CI output normally remains a test artifact. Public Beta 2 is a narrow,
explicitly approved prerelease exception: it must be labelled unsigned and
hardware-unverified, published as a GitHub prerelease with its checksum, and
must not be described as stable or from a verified publisher. Before any stable
publication, sign the PDW executables, Setup, and the generated uninstaller with
the same trusted Authenticode publisher identity and a trusted timestamp. Then
run:

```powershell
.\scripts\audit-installer.ps1 `
  -Setup out\installer\PDW-v5.5.1-2026-Release-Setup-package\PDW-v5.5.1-2026-Release-Setup.exe `
  -RequireSignature -ScanWithDefender
```

Any Defender detection, secret-bearing default, architecture mismatch,
separated settings storage, failed upgrade, or data-removing uninstall blocks
beta and stable publication. A missing or invalid signature additionally blocks
promotion to a stable release.
