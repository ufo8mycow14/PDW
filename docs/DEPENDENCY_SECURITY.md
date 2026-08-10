# Dependency and security review

Reviewed: 10 August 2026

This record must be refreshed from official upstream release and security
pages before each PDW release. A newer version is not adopted until it passes
the complete Win32 and x64 gates and preserves legacy support.

| Component | Pinned | Official status reviewed | Decision |
| --- | --- | --- | --- |
| OpenSSL | 3.5.7 | Current OpenSSL 3.5 LTS release; 3.5 support is listed through 8 April 2030 | Retain 3.5.7 and its verified source hash |
| curl/libcurl | 8.21.0 | Current published curl release; the next release is listed as pending | Retain 8.21.0 and its verified source hash |
| libssh2 | 1.11.1 | Newest archive on the official download index | Retain 1.11.1 and its verified source hash |
| Windows SQLite | Operating-system component | Uses the supported Windows `winsqlite3` API; no bundled SQLite DLL | Retain the platform binding; require fully patched Windows and fail closed when archive connection protections are unavailable |
| MySQL | Operator-installed ODBC driver | Current Connector/ODBC GA is 26.7.0 and the official Windows download is x64; PDW does not bundle a driver or server | Keep DSN-based integration; require a supported architecture-matched driver and separately validate x86 |
| Inno Setup | 6.7.3 | Current pinned stable Inno Setup 6 compiler used only to build Setup | Retain as a build-time dependency; no Inno runtime is installed with PDW |

Operator-installed MySQL Connector/ODBC 9.0.0 through 9.5.0 must not be used;
Oracle's January 2026 Critical Patch Update lists CVE-2025-9230 for those
versions. Operators must use an actively supported, matching-bitness driver
and apply current Oracle CPU updates.

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
- Inno Setup releases: <https://jrsoftware.org/isinfo.php>
- Inno Setup 6 revision history: <https://jrsoftware.org/files/is6-whatsnew.htm>

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
