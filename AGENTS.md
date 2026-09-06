# PDW engineering rules

These rules apply to every human or automated change in this repository.

## Compatibility is non-negotiable

- PDW is one native C++ product with two mandatory release targets: x64 and Win32/x86. A release is incomplete until both configure, build, test, start, and package successfully.
- Preserve all established protocols, decoders, slicers, serial and WinMM paths, receiver choices, drivers, configuration compatibility, filters, and outputs. Older scanners and x86-only receiver libraries are supported use cases, not dead code.
- Never remove or replace a legacy path without representative, redistributable regression evidence and an approved compatible fallback. New behaviour must be additive, disabled by default when it sends data, and fail independently from capture and decoding.
- Native DLLs and dependencies must match the target architecture. The Win32 package retains intentional x86-only receiver and legacy support assets; the x64 package must not load or distribute them as x64 binaries.

## Security and dependency maintenance

- Before each release, review the official release and security-advisory pages for every native dependency and documented external integration. Record the review in `docs/DEPENDENCY_SECURITY.md`.
- Pin source versions and SHA-256 hashes. Apply supported security updates only after successful Win32 and x64 compatibility, test, package, and smoke gates.
- Store secrets in Windows Credential Manager, never source, INI defaults, logs, packages, screenshots, tests, or chat. Keep TLS certificate and SFTP host-key validation mandatory. Network outputs stay disabled by default.
- Keep decoded traffic and identifiers out of diagnostics, health records, fixtures, and public artefacts. Use only synthetic, redacted, or licensed representative data.
- Validate SQL identifiers and bind all event values through prepared statements. Do not construct SQL from decoded content or credentials.
- Introducing Visual Basic, a new database/runtime, driver, or other platform dependency requires explicit supported-version, security, licensing, architecture, and package review for both targets.

## Release and validation routing

- Use `docs/PROJECT_RULES.md`, `SECURITY.md`, `docs/REPOSITORY_AUDIT.md`, `docs/PUBLISHING.md`, `docs/WINDOWS_ARCHITECTURES.md`, and `docs/DEPENDENCY_SECURITY.md` for the full release, packaging, signing, dependency, and evidence requirements.
- Run `scripts/audit-release.ps1` before release building or packaging.
- For a published update, all current-version surfaces must align: source version, resources, manifest, executable name, UI version text, CMake output, packages, changelog, readme, handover, roadmap, release branch/PR metadata, `PDW_BUILD_COMMIT.txt`, checksums, and installer artefacts.
- Historical release notes and contributor-version credits remain historical and must not be rewritten merely to match the current release.
- Confirm every worktree is clean before publication. Do not publish, tag, sign, upload, or describe a build as stable unless the matching release gates have actually passed.

## Working boundary

- Preserve unrelated work and inspect the relevant diff before editing.
- Keep release procedures and broad gate details in the canonical docs above instead of copying them into task-local instructions.
- Do not commit, push, publish, package, upload, sign, or delete release artefacts unless the current task explicitly authorises it.
- Final reports should state completed work, validation actually run, important files, and genuine limitations.
