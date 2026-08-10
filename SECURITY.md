# PDW security policy

## Supported versions

Security maintenance targets the current release on both native Windows
architectures. Win32 remains supported because legacy receiver DLLs, scanners,
serial slicers, and drivers may require a 32-bit process. x64 is not a
replacement for those compatibility paths.

Older packages are retained as rollback evidence but do not receive routine
security updates. Verify an issue against the current release before reporting
it where possible.

## Reporting a vulnerability

Do not publish credentials, decoded radio traffic, private logs, recordings,
databases, crash dumps containing message content, or exploit details in a
public issue. Contact the fork maintainer privately through GitHub, or use the
repository's private vulnerability-reporting facility when it is available.
Include the PDW version, architecture, affected feature, reproduction steps,
and a synthetic or redacted test case.

## Security requirements

- Optional network outputs are disabled by default.
- Credentials belong in Windows Credential Manager and are excluded from INI
  defaults, logs, diagnostics, backups, and release packages.
- TLS certificate/hostname verification and SFTP SHA-256 host-key verification
  cannot be bypassed.
- SQL table identifiers are restricted and decoded event fields are passed as
  bound prepared-statement parameters.
- Queues, retries, timeouts, payload sizes, and diagnostic histories are
  bounded so external services cannot block decoding indefinitely.
- Public tests and artifacts use synthetic, redacted, or licensed content.
- Native dependencies are source-pinned with SHA-256 verification and reviewed
  against official releases and advisories before every release.
- Win32 and x64 builds, tests, package audits, and native startup smoke are all
  release gates.
- Public application, Setup, and uninstaller files require a valid trusted
  Authenticode publisher signature and timestamp. Unsigned CI installers are
  explicitly non-public test artifacts.
- The complete Setup and its staged architecture inputs must pass Microsoft
  Defender scanning before publication. Suspected false positives are reviewed
  through Microsoft Security Intelligence rather than bypassed or excluded.

Security fixes must preserve the legacy decoder, protocol, slicer, audio,
serial, receiver, driver, filter, and configuration compatibility contract.
Where a direct fix could change decoded output, add representative regression
evidence before changing behavior.
