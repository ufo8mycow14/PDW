# PDW engineering rules

These rules apply to every human or automated change in this repository.

## Compatibility is non-negotiable

- PDW is one native C++ product with two mandatory release targets: x64 and
  Win32/x86. A release is incomplete until both configure, build, test, start,
  and package successfully.
- Preserve all established protocols, decoders, slicers, serial and WinMM
  paths, receiver choices, drivers, configuration compatibility, filters, and
  outputs. Older scanners and x86-only receiver libraries are supported use
  cases, not dead code.
- Never remove or replace a legacy path without representative, redistributable
  regression evidence and an approved compatible fallback. New behavior must
  be additive, disabled by default when it sends data, and fail independently
  from capture and decoding.
- Native DLLs and dependencies must match the target architecture. The Win32
  package retains intentional x86-only receiver and legacy support assets; the
  x64 package must not load or distribute them as x64 binaries.

## Release identity

Every published update must advance and align all current-version surfaces:

- `Headers/version.h`, Windows resources, embedded manifest, executable name,
  main-window title, About dialog, and Settings About me page;
- CMake output, GitHub workflow artifact, package folder/ZIP, changelog,
  Readme, handover, roadmap, release branch, pull request, and fork metadata.

Run `scripts/audit-release.ps1` before building or packaging. Historical
release notes and contributor-version credits remain historical and must not
be rewritten merely to match the current release.

## Security and dependency maintenance

- Before each release, review the official release and security-advisory pages
  for every native dependency and documented external integration. Record the
  review in `docs/DEPENDENCY_SECURITY.md`.
- Pin source versions and SHA-256 hashes. Apply supported security updates only
  after successful Win32 and x64 compatibility, test, package, and smoke gates.
- Store secrets in Windows Credential Manager, never source, INI defaults,
  logs, packages, screenshots, tests, or chat. Keep TLS certificate and SFTP
  host-key validation mandatory. Network outputs stay disabled by default.
- Keep decoded traffic and identifiers out of diagnostics, health records,
  fixtures, and public artifacts. Use only synthetic, redacted, or licensed
  representative data.
- Validate SQL identifiers and bind all event values through prepared
  statements. Do not construct SQL from decoded content or credentials.
- The repository currently contains no Visual Basic. If Visual Basic, a new
  database/runtime, driver, or other platform dependency is introduced, it
  requires an explicit supported-version, security, licensing, architecture,
  and package review for both targets.

## Required gates

1. Preserve unrelated work and inspect the complete diff.
2. Run `scripts/audit-release.ps1`.
3. Clean-build all configured Release targets for Win32 and x64.
4. Pass every CTest test on both architectures.
5. Compile the optional WinMM and WASAPI device-smoke targets on both.
6. Verify PE machine, file/product version, manifest, and About UI.
7. Run native Light/Dark and compact-size UI smoke on both.
8. Generate and independently audit both portable packages.
9. Confirm every worktree is clean before publication.

See `docs/PROJECT_RULES.md`, `SECURITY.md`, and
`docs/REPOSITORY_AUDIT.md` for the human-facing policy and evidence.
