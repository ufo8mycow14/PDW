# PDW project rules

These are binding release rules for PDW v5 2026 Release and later.

## One product, two maintained architectures

PDW is maintained from one CMake source tree and released as both native x64
and Win32/x86 executables. The two packages share decoder and user-facing
behavior, but carry architecture-matched dependencies. Win32 remains a full
release target for older scanners, serial slicers, x86-only receiver DLLs, and
legacy drivers. It is not a temporary compatibility build.

Every change must be evaluated on both architectures. A passing x64 build does
not authorize deleting Win32 code or assets, and a passing Win32 build does
not prove pointer-width safety on x64.

## Legacy preservation rule

Keep POCSAG, FLEX, ERMES, ACARS, MOBITEX, WinMM, WASAPI fallback, RS232,
serial/slicer interfaces, `.rec` replay, RTL-TCP, optional RTL-SDR packages,
filters, logs, WAV alerts, SMTP, Apprise, FTP/FTPS/SFTP, publishing, and all
documented configuration fallbacks.

Do not classify a path as obsolete because it targets old hardware. Removal or
behavioral replacement requires all of the following:

1. representative synthetic, licensed, or redacted regression evidence;
2. proof that current packages/builds do not consume it;
3. a retained compatible fallback for supported users;
4. successful Win32 and x64 gates; and
5. an explicit documented approval in the release change.

## Version and release identity gate

For every published update, advance the canonical semantic version in
`Headers/version.h`. The executable file/product version, filename, title,
About dialog, Settings About me page, manifest, workflow artifact, portable
package, guided Setup, Readme, changelog, handover, roadmap, branch, pull
request, and fork must describe the same release. Historical rows and
contributor credits stay historical.

`scripts/audit-release.ps1` checks the repository-level identity before build
or packaging. Native metadata and UI still require post-build inspection.

## Secure engineering rule

- Review official dependency release and security pages before every release;
  update `docs/DEPENDENCY_SECURITY.md` with date, version, source, decision,
  and both-architecture validation.
- Pin and SHA-256 verify native source archives. Never silently substitute a
  downloaded binary or architecture.
- Use Windows Credential Manager for every supported output secret and redact
  errors. Legacy SMTP password storage in an operator-owned `PDW.INI` remains
  a narrow compatibility exception: release defaults and packages keep it
  blank, backups must be protected, and operators should clear or migrate it
  when practical. Never place secrets or decoded traffic in source, release
  INI defaults, logs, packages, diagnostics, tests, screenshots, or chat.
- Keep secure transport validation mandatory. Prefer FTPS/SFTP over classic
  FTP and document FTP's plaintext risk.
- Keep network/data outputs disabled by default and isolated behind bounded
  queues, timeouts, retries, and payload limits.
- Restrict SQL identifiers and bind data values. Review Windows SQLite and ODBC
  behavior whenever those platform APIs or database schemas change.
- Review licensing, supported runtime, compiler, security advisories, and both
  architectures before introducing Visual Basic or another language/runtime.

## Release checklist

1. Run `scripts/audit-release.ps1` and review the complete diff.
2. Build pinned x86 dependencies and every Win32 Release target.
3. Build pinned x64 dependencies and every x64 Release target.
4. Pass the complete CTest suite on both.
5. Compile WinMM and WASAPI smoke targets on both.
6. Inspect PE machine values, file/product versions, manifest, executable
   filenames, main-window title, About dialog, and Settings About me page.
7. Smoke Light, Dark, minimum/compact, and normal layouts on both.
8. Create both portable packages and independently validate hashes, secrets,
   forbidden runtime content, source snapshot, and architecture-only assets.
9. Build the combined Setup and pass Win32/x64 install, co-located settings,
   upgrade, uninstall, Defender, metadata, and architecture-selection checks.
10. Require trusted Authenticode signatures for every public executable,
    Setup, and uninstaller; unsigned CI outputs remain test artifacts only.
11. Push a named release branch, open/update a draft pull request, wait for all
   dual-architecture CI and security checks, and keep the tree clean.

Physical receiver, slicer, driver, network-service, and live-radio acceptance
remain separate manual evidence. A clean build must never be described as
proof that untested hardware or services work.
