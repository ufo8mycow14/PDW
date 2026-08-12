# Dependency and security review

Reviewed: 12 August 2026

This record must be refreshed from official upstream release and security
pages before each PDW release. A newer version is not adopted until it passes
the complete Win32 and x64 gates and preserves legacy support.

| Component | Pinned | Official status reviewed | Decision |
| --- | --- | --- | --- |
| OpenSSL | 3.5.7 | Current OpenSSL 3.5 LTS release; 3.5 support is listed through 8 April 2030 | Retain 3.5.7 and its verified source hash |
| curl/libcurl | 8.21.0 | Current published curl release; the next release is listed as pending | Retain 8.21.0 and its verified source hash |
| libssh2 | 1.11.1 | Newest archive on the official download index | Retain 1.11.1 and its verified source hash |
| Windows SQLite | Operating-system component | Uses the supported Windows `winsqlite3` API; no bundled SQLite DLL | Retain the platform binding; require fully patched Windows and fail closed when archive connection protections are unavailable |
| MySQL | Operator-installed ODBC driver | Connector/ODBC 9.7.0 is the current GA release and Windows x64 download reviewed for this release; PDW does not bundle a driver or server | Use Connector/ODBC 9.7.0 with a secured Windows DSN for x64; require a supported architecture-matched driver before enabling Win32 MySQL output |
| Inno Setup | 6.7.3 | Latest 6.x compiler; Inno Setup 7.0.2 is also available | Retain 6.7.3 for v5.5.2 so a compiler-major migration does not overlap the message-handling and display-recovery change; evaluate 7 separately through full dual installer gates |
| Visual Studio / MSVC | Visual Studio 2026 / v145 | Current maintained Windows release toolchain; v145 targets Windows 10/Server 2016 and newer | Build and test both architectures on the explicit VS 2026 runner; distribute only the reviewed architecture-matched app-local Microsoft Visual C++ runtime DLLs; record the exact compiler, generator and CMake version in each dependency lock |
| SDR# | Production revision 1921 | External Windows SDR application used only by the named local-audio profile; the official x86/x64 revision 1922 downloads are labelled beta | Record 1921 as the reviewed stable integration target; operator installs, configures, updates, and supports it; PDW does not bundle or control it |
| VB-CABLE | Package 45 | External VB-Audio virtual driver for Windows 32/64/Arm64; published October 2024 | Operator installs and licenses it directly; PDW does not bundle the driver and uses only the selected Windows recording endpoint |

Operator-installed MySQL Connector/ODBC 9.0.0 through 9.5.0 must not be used;
Oracle's January 2026 Critical Patch Update lists CVE-2025-9230 for those
versions. Operators must use an actively supported, matching-bitness driver
and apply current Oracle CPU updates.

Oracle's July 2026 Critical Patch Update lists affected Connector/NET,
Connector/J and Connector/C++ components in the 9.7 line; it does not identify
Connector/ODBC as an affected component in that risk matrix. The current
Connector/ODBC 9.7.0 GA download remains the reviewed PDW x64 integration
target. PDW does not install the driver, and operators must still repeat the
vendor-advisory review before deployment rather than relying indefinitely on
this release record.

Official review sources:

- OpenSSL releases: <https://openssl-library.org/source/>
- OpenSSL advisories: <https://openssl-library.org/news/secadv/>
- curl releases: <https://curl.se/docs/releases.html>
- curl vulnerabilities: <https://curl.se/docs/vulnerabilities.html>
- curl 8.21.0 security status: <https://curl.se/docs/vuln-8.21.0.html>
- libssh2 downloads: <https://libssh2.org/download/>
- libssh2 security advisories: <https://github.com/libssh2/libssh2/security>
- SQLite releases and security guidance: <https://sqlite.org/changes.html> and
  <https://sqlite.org/security.html>
- Windows `winsqlite3` API availability:
  <https://learn.microsoft.com/en-us/uwp/win32-and-com/win32-extension-apis>
- MySQL Connector/ODBC downloads and Oracle CPU advisories:
  <https://dev.mysql.com/downloads/connector/odbc/> and
  <https://www.oracle.com/security-alerts/>
- Oracle January 2026 Critical Patch Update:
  <https://www.oracle.com/security-alerts/cpujan2026.html>
- Oracle July 2026 Critical Patch Update:
  <https://www.oracle.com/security-alerts/cpujul2026.html>
- Inno Setup releases: <https://jrsoftware.org/isinfo.php>
- Inno Setup downloads, including 6.7.3 and 7.0.2:
  <https://jrsoftware.org/isdl.php>
- Inno Setup 6 revision history: <https://jrsoftware.org/files/is6-whatsnew.htm>
- Visual Studio 2026/MSVC v145 support boundary:
  <https://learn.microsoft.com/cpp/overview/what-s-new-for-msvc?view=msvc-150>
- CMake Visual Studio 18 2026 generator:
  <https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html>
- GitHub Windows runner images:
  <https://github.com/actions/runner-images>
- SDR# production and beta downloads: <https://airspy.com/download/>
- VB-CABLE Package 45 and platform support: <https://vb-audio.com/Cable/>
- VB-Audio donationware, professional-use, and distribution licensing:
  <https://vb-audio.com/Services/licensing.htm>

Repository checks confirmed that SQLite and MySQL event values use prepared
statements with bound parameters. Dynamic table names are accepted only after
`IsSafeSqlIdentifier` validation. Credentials are not embedded in SQL and the
ODBC connection buffer is cleared after use.

The 10 August 2026 v5.1 refresh rechecked the OpenSSL 3.5.7, curl 8.21.0,
and libssh2 1.11.1 archive SHA-256 values against their official release
artifacts; all repository pins matched. The official OpenSSL release and
advisory pages list 3.5.7 as the current 3.5 LTS maintenance release. curl
8.21.0 has no published security problems, and libssh2's official repository
lists no published advisories.

The v5.1 feature bundle introduces no new third-party native or runtime
dependency. Message Archive reuses the Windows `winsqlite3` binding already
linked by SQLite Output; the local dashboard, multi-channel receiver, and
signal conditioner use existing project or Windows APIs. Because Message
Archive can open an operator-selected database, required connection defenses
and its first-on-open bounded quick integrity check must succeed before schema
or message SQL runs. Files that fail are preserved unchanged for external
recovery.

The 11 August 2026 v5.2 refresh rechecked the official upstream release and
security pages. OpenSSL 3.5.7 remains the current 3.5 LTS maintenance release,
curl 8.21.0 remains the current published curl release with no published
security problems, libssh2 1.11.1 remains the latest official archive with no
published security advisories, and Inno Setup 6.7.3 remains the current 6.x
compiler revision. The Message History CSV export introduces no third-party
native or runtime dependency; it uses the existing Windows SQLite binding and
C++/Windows file APIs. The repository pins and decisions therefore remain
unchanged for v5.2.

The 11 August 2026 v5.3 refresh rechecked the same official release and
security sources. OpenSSL 3.5.7 remains the current supported 3.5 LTS release
through 8 April 2030; curl 8.21.0 remains the current published release with
zero listed vulnerabilities; libssh2 1.11.1 remains the newest official
archive; and Inno Setup 6.7.3 remains the current 6.x compiler revision. The
Capcode Directory filtering work introduces no new native or runtime
dependency and reuses the existing Windows SQLite binding and project code.
All repository pins and release decisions therefore remain unchanged for
v5.3.

The 11 August 2026 v5.4 refresh again checked the official upstream release
and security sources. OpenSSL 3.5.7 remains the current supported 3.5 LTS
release through 8 April 2030; curl 8.21.0 remains the current published
release with zero listed vulnerabilities; libssh2 1.11.1 remains the newest
official archive and has no published security advisory; and Inno Setup 6.7.3
remains the current 6.x compiler revision. FLEX shadow reassembly and display
layout introduce no third-party native or runtime dependency and use only
existing project and C++ standard-library code. Repository pins and release
decisions therefore remain unchanged for v5.4.

The 11 August 2026 v5.5 refresh checked the same native-dependency sources and
the new documented external integrations. OpenSSL 3.5.7 remains the current
3.5 LTS maintenance release; the 5 August 2026 low-severity OCSP advisory
explicitly states that OpenSSL 3.5 is not affected. curl 8.21.0 still lists
zero published vulnerabilities, and libssh2 1.11.1 remains the newest official
archive with no published advisory. Repository source versions and SHA-256
pins therefore remain unchanged.

Inno Setup 7.0.2 is available, while 6.7.3 remains the latest 6.x compiler.
PDW v5.5 deliberately retains pinned 6.7.3: changing the installer compiler's
major version while also changing clean-install profile and upgrade behavior
would broaden the release surface. Inno 7 evaluation remains a separate task
requiring Win32/x64 staging, build, metadata, install, upgrade, uninstall,
Defender, and signature verification before adoption.

The named profile was reviewed against SDR# production revision 1921 and
VB-CABLE Package 45. Both are external/operator-installed and no vendor binary,
driver, license key, or installer is downloaded or packaged by PDW. VB-CABLE is
donationware and its vendor requires licensing for professional use. PDW uses
existing Windows MMDevice/WASAPI APIs only, stores no real machine endpoint ID
in release defaults, and fails closed if the saved exact endpoint is missing.
The profile does not create or modify Capcode Directory data or `filters.ini`
and introduces no new linked native dependency. Fresh dual builds, tests,
package/source-tamper checks, combined installer smoke, CI/CodeQL and Defender
pass for Public Beta 1. Physical audio acceptance and trusted Authenticode
signing remain open; the maintainer-approved public beta must stay visibly
unsigned and hardware-unverified until those gates are completed.

The 11 August 2026 v5.5.1 Public Beta 2 refresh rechecked the official OpenSSL,
curl, libssh2, Inno Setup, SDR#, and VB-CABLE release/security sources. OpenSSL
3.5.7 remains the current supported 3.5 LTS release through 8 April 2030; curl
8.21.0 remains the current published release with zero listed vulnerabilities;
libssh2 1.11.1 remains the newest official archive with no published advisory;
Inno Setup 6.7.3 remains the current 6.x compiler while 7.0.2 is a separate
major-version migration; SDR# production revision 1921 and VB-CABLE Package 45
remain the external integration references. Capcode filtering, routing, label,
and migration changes add no third-party native/runtime dependency and reuse
existing Windows, SQLite, and project code. Repository pins and security
decisions therefore remain unchanged for v5.5.1.

The 12 August 2026 v5.5.2 release refresh again checked the official
OpenSSL, curl, libssh2, Inno Setup, MySQL Connector/ODBC, SDR#, and VB-CABLE
release and security pages. OpenSSL 3.5.7 remains the supported 3.5 LTS
release through 8 April 2030; curl 8.21.0 remains the current published
release with zero listed vulnerabilities; libssh2 1.11.1 remains the newest
official archive; Inno Setup 6.7.3 remains the latest 6.x compiler while 7.0.2
is a separate major-version migration; Connector/ODBC 9.7.0 is the current GA
release reviewed for the optional x64 DSN integration; and the external
profile references
remain SDR# production revision 1921 and VB-CABLE Package 45. Rejected-message
discarding, multipart presentation, pane repainting, Capcode CSV upsert, and
the Local Gateway Outbox add no bundled third-party native or runtime
dependency; the outbox uses the existing Windows `winsqlite3` binding.
Repository pins and security decisions therefore remain unchanged for v5.5.2.

The v5.5 distribution makes PDW's existing `/MD` compiler-runtime dependency
explicit by placing the architecture-matched Microsoft Visual C++ runtime DLLs
beside PDW. CMake's `InstallRequiredSystemLibraries` selects the current target
architecture, after which a repository allowlist limits output to
`concrt140.dll`, the reviewed `msvcp140` family, `vcruntime140.dll`, and x64
`vcruntime140_1.dll`. Package and installer staging require the exact expected
set, validate PE machine type, original filename, and the VC145 14.5x version
family. The Win32 set intentionally omits `vcruntime140_1.dll` because the
reviewed VC145 x86 redistributable directory does not supply it; Setup deletes
the complete cross-architecture allowlist before copying the chosen set.

PDW does not change to `/MT`, invoke or carry a `VC_redist` installer, or bundle
the UCRT, debug CRT, MFC, OpenMP, `vccorlib`, or unreviewed runtime DLLs. The
supported Windows floor provides the serviced UCRT. Microsoft runtime files
remain governed by Microsoft's Visual Studio licensing and redistributable-code
terms as recorded in `THIRD_PARTY_NOTICES.md`; PDW's GPL license does not
relicense those Microsoft binaries.

Build provenance is re-evaluated after the executable links. A Git checkout
must still have the configured commit and a clean status; a packaged `Source`
folder must still match its package-generated SHA-256 manifest and clean
Git-archive commit metadata. Configure-only `pending` markers and dirty or
changed trees cannot enter portable or installer staging. A pre-link step first
invalidates the marker beside the executable, and post-build validation alone
can promote it, closing the failed-validation/stale-marker path. Release staging
copies tracked inputs from one immutable archive of the accepted commit,
hash-checks mutable build outputs across each copy, and rechecks exact clean
`HEAD` after staging. `PDW_INSTALLER_INPUT_SHA256SUMS.txt` binds every staged
installer-input byte and rejects missing or unlisted files before and after
Setup compilation. Inno writes to a unique temporary output and the build moves
the executable to its public Setup filename only after postcompile provenance,
signature-policy, and optional Defender checks pass. The release package's
outer `SHA256SUMS.txt` also binds the inner source provenance and manifest;
Authenticode remains the separate public-release authenticity gate.

## Required review procedure

1. Check every official release and advisory page above.
2. Compare the pinned version, support lifetime, fixes, and published security
   impact; add new dependencies and external integrations to this table.
3. Update source URLs and SHA-256 pins only from official upstream material.
4. Build and test the candidate for x86 and x64.
5. Re-run SQL, transfer, notification, package, and native UI smoke relevant to
   the update.
6. Record the review date and outcome here, including why an available update
   was adopted or deferred.

This file records a point-in-time review. It is not evidence that a dependency
will remain current after the review date.
