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
| Windows SQLite | Operating-system component | Uses the supported Windows `winsqlite3` API; no bundled SQLite DLL | Retain platform binding; continue prepared-statement tests |
| MySQL | Operator-installed ODBC driver | PDW does not bundle a driver or server | Keep DSN-based integration; operator maintains a supported driver |
| Inno Setup | 6.7.3 | Current pinned stable Inno Setup 6 compiler used only to build Setup | Retain as a build-time dependency; no Inno runtime is installed with PDW |

Official review sources:

- OpenSSL releases: <https://openssl-library.org/source/>
- OpenSSL advisories: <https://openssl-library.org/news/secadv/>
- curl releases: <https://curl.se/docs/releases.html>
- curl vulnerabilities: <https://curl.se/docs/vulnerabilities.html>
- libssh2 downloads: <https://libssh2.org/download/>
- Inno Setup releases: <https://jrsoftware.org/isinfo.php>
- Inno Setup 6 revision history: <https://jrsoftware.org/files/is6-whatsnew.htm>

Repository checks confirmed that SQLite and MySQL event values use prepared
statements with bound parameters. Dynamic table names are accepted only after
`IsSafeSqlIdentifier` validation. Credentials are not embedded in SQL and the
ODBC connection buffer is cleared after use.

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
