# Third-party components

PDW's Win32 build links the following open-source components statically. The
build script downloads the official source archives, checks their SHA-256
hashes before extraction, and installs a copy of each component's license with
the generated dependency set.

| Component | Version | SHA-256 source archive | License |
| --- | --- | --- | --- |
| OpenSSL | 3.5.7 | `A8C0D28A529CA480F9F36CF5792E2CD21984552A3C8E4AA11A24AA31AEAC98E8` | Apache License 2.0 |
| curl/libcurl | 8.21.0 | `AA1B66A70EACE83DC624508745646C08AE561DE512AB403ADFFB93AC87FC72E6` | curl license |
| libssh2 | 1.11.1 | `9954CB54C4F548198A7CBEBAD248BDC87DD64BD26185708A294B2B50771E3769` | Revised BSD license |

Official sources and license information:

- OpenSSL: <https://github.com/openssl/openssl/releases/tag/openssl-3.5.7>
- curl: <https://github.com/curl/curl/releases/tag/curl-8_21_0>
- libssh2: <https://github.com/libssh2/libssh2/releases/tag/libssh2-1.11.1>

OpenSSL continues to provide TLS for PDW's existing SMTP code. The file
transfer feature uses libcurl with Windows Schannel for FTPS and libssh2 with
Windows CNG for SFTP. The dependency build is defined in
`scripts/build-dependencies.ps1`; no prebuilt third-party binaries are checked
into the repository.

## Optional external notification service

The Apprise feature is an HTTPS client integration and does not link or package
Apprise code in `PDW.exe`. The validated external deployment is Apprise API
1.5.1 (MIT License) with Apprise 1.12.0 (BSD 2-Clause License). Operators host,
secure, license, and update that service independently:

- Apprise API: <https://github.com/caronc/apprise-api/releases/tag/v1.5.1>
- Apprise: <https://github.com/caronc/apprise/releases/tag/v1.12.0>
