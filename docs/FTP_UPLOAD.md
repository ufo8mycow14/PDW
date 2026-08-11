# Continuous file transfer

PDW 3.3 can continuously publish selected output files to a hosting account.
Open **Options > File Transfer** to configure it.

## Supported protocols

| Protocol | Usual port | Protection |
| --- | ---: | --- |
| FTP | 21 | No encryption. Credentials and files travel in plain text. |
| FTPS (explicit TLS) | 21 | Starts as FTP, then upgrades the control and data connections to TLS. |
| FTPS (implicit TLS) | 990 | Uses TLS from the start of the connection. |
| SFTP | 22 | Uses SSH. This release supports password authentication. |

FTPS and SFTP are different protocols. Choose the exact option named by the
hosting provider. Changing the protocol updates the usual port automatically
unless a custom port has already been entered.

## Configure an upload

1. Select the protocol supplied by the hosting provider.
2. Enter only the server hostname, without `ftp://`, `sftp://`, a port, or a
   folder.
3. Confirm the port, then enter the username and password.
4. Enter the remote folder if files should not go to the account's default
   folder. For SFTP, a blank or relative folder starts from the login user's
   home. Use a leading `/` only when the provider supplies an absolute path.
5. For FTP or FTPS, leave passive mode enabled unless the provider specifically
   requires active mode.
6. For SFTP, paste the server's SHA-256 SSH host-key fingerprint supplied by
   the provider. PDW accepts the common `SHA256:base64-value` format.
7. Select one or more local files with **Add files**.
8. Set an interval from 10 to 86400 seconds.
9. Select **Enable automatic uploads**, then choose **OK**.

**Upload now** saves the settings and starts a background upload immediately.
The latest result appears in the dialog, and detailed results are appended to
`FileTransfer.log` beside `PDW v5.3 2026 Release.exe`.

Each selected file keeps its existing filename on the server. PDW prevents two
selected files with the same filename because they would target the same remote
file. Before each transfer, PDW creates a temporary snapshot so a log file can
continue changing without changing the bytes being uploaded. The remote folder
must already exist; PDW does not create hosting directories.

## Password and server verification

The hosting password is stored as a generic credential in Windows Credential
Manager for the current PDW installation. It is never written to `pdw.ini` or
`FileTransfer.log`. Use **Clear** beside the password field to remove the saved
credential; PDW also disables automatic uploads when the credential is
removed.

FTPS requires TLS 1.2 or newer and validates the server hostname and
certificate through the Windows certificate trust store. A certificate error
causes the upload to fail; there is no bypass switch.

SFTP requires an exact SHA-256 host-key match before authentication. Obtain the
fingerprint through the hosting provider's control panel or support team, using
a separate trusted channel. Do not copy a fingerprint from an unexpected
connection warning. If the provider legitimately rotates its SSH host key,
verify the new fingerprint with the provider before updating PDW. SSH private
key authentication is not included in this release.

Classic FTP does not encrypt the username, password, or uploaded data. Prefer
FTPS or SFTP whenever the provider supports either secure option.

## Scheduling behaviour

The scheduler is checked once per second, including while PDW display output
is paused. Network and file-copy work runs on a background worker, with only
one upload active at a time, so decoding is not deliberately blocked by a
hosting connection. Network operations use finite timeouts, and the next
interval begins after the current upload finishes.
