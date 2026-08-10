# Apprise notifications

PDW can send every message that reaches its filtered-message output through an
operator-managed Apprise API. Open **Options > Apprise** to configure it.
Email remains a separate channel under **Options > SMTP/Email**. This permits
email only, Apprise only, both channels, or neither channel.

## Architecture

PDW has no application backend and its native ZIP-style deployment has no
Python runtime or installer capable of safely carrying a local Apprise build.
The implementation therefore uses an explicitly configured Apprise API rather
than introducing an undisclosed hosted service or requiring Python on the PDW
computer.

The client request contract is validated against Apprise API 1.5.1 with
Apprise 1.12.0. Deploy and pin those versions, or regression-test a newer
release before upgrading. The API request is an HTTPS JSON `POST` containing
the documented `body`, `title`, `type`, `format`, and optional `urls` fields.

Apprise API does not enable authentication or TLS by default. Never expose its
container port directly to an untrusted network. Put it behind an HTTPS reverse
proxy that requires Basic authentication, then enter that proxy account in
PDW. The URL must end in either:

- `/notify` for stateless delivery. Enter one or more Apprise service URLs in
  **Destination URL(s)**, separated by spaces or commas.
- `/notify/{key}` for a configuration already stored on the Apprise server.
  **Destination URL(s)** must be left blank in this mode; the v1.5.1 stateful
  notify schema does not accept the stateless `urls` field.

The Apprise server is an independently operated component. It is not packaged
inside `PDW v4.5.0 Beta.exe` and PDW does not contact any endpoint until the operator saves
valid settings and selects **Enable Apprise**.

## Configure PDW

1. Open **Options > Apprise**.
2. Enter the full HTTPS Apprise notify URL.
3. Enter the Basic-auth username and password enforced by the reverse proxy.
4. For a stateless `/notify` endpoint, enter the Apprise destination URL or
   URLs. For `/notify/{key}`, leave that field blank and use the server-side
   configuration.
5. Select **Send test notification**. Testing does not enable automatic sends
   and does not save unsaved credentials.
6. Select **Enable Apprise**, then choose **OK**.

Every non-rejected, non-duplicate message that PDW places in the filtered
message pane is then queued for Apprise. Monitor-only matches and unfiltered
traffic are not sent. Delivery runs on a bounded background queue, so a slow or
unavailable notification service does not block decoding or fail the original
operation.

By default the push body says only that a filtered message was received; it does
not expose the filter label, pager address, or decoded content. Select **Include
filter details and decoded text** only after considering that those details may
be displayed on a phone lock screen. Bodies are capped at 480 UTF-8 bytes for
broad mobile-provider compatibility.

## Secret storage and delivery safeguards

- The API endpoint, destination URLs, API username, and API password are stored
  in Windows Credential Manager under targets scoped to the current
  `pdw.ini` path. `pdw.ini` stores only the enable and message-text choices.
- Endpoint, destination, and API credential fields are password-masked. They are
  never passed on a command line,
  added to PDW logs, or included in configuration backups.
- `Apprise.log` records only timestamp, generated event identifier, and a
  sanitized result category. It never records decoded content, endpoints,
  credentials, destination URLs, or provider response bodies.
- TLS certificate and host verification are mandatory. Redirect following is
  disabled so credentials cannot be forwarded to a different host.
- Each request has a 4-second connection timeout and a 12-second total timeout.
  Only transient connection failures and selected transient HTTP responses are
  retried, at most twice, with bounded exponential backoff. Authentication,
  validation, and partial-delivery failures are not retried.
- A stable `X-PDW-Notification-ID` is retained across retries, and PDW prevents
  the same event identifier from entering its queue twice. External providers
  may not honor that identifier, so an upstream response lost after delivery
  can never be made perfectly duplicate-proof.
- PDW sends no attachments or remote attachment URLs. The Apprise server should
  still run in API-only mode, allow only required service plugins, and block
  generic webhook targets in localhost, private, link-local, and cloud-metadata
  ranges. Those controls belong at the server because it performs the outbound
  provider requests.

Use **Clear saved settings** to remove all Apprise entries from Windows
Credential Manager and disable the channel. The action requires confirmation.

## Manual phone test: ntfy

1. Install the ntfy mobile app and subscribe to a private/authenticated topic.
   For the public `ntfy.sh` service, use a long unguessable topic and remember
   that anyone who learns the topic name can subscribe to it.
2. Use `ntfys://ntfy.sh/YOUR_TOPIC` as the stateless destination URL, or add it
   to the server-side `/notify/{key}` configuration.
3. Open **Options > Apprise**, enter the authenticated HTTPS API settings, and
   select **Send test notification**.
4. Confirm the phone receives **PDW Apprise test**.
5. Enable Apprise, then feed a synthetic message that matches a test PDW filter.
   Confirm one notification is received and that an unfiltered synthetic
   message produces none.

## Manual phone test: Pushover

1. Install Pushover, sign in, and obtain the account user key.
2. Create a Pushover application/API token.
3. Use `pover://USER_KEY@APPLICATION_TOKEN` as the stateless destination URL,
   or store it in the server-side `/notify/{key}` configuration.
4. Select **Send test notification** in PDW and confirm the device receives it.
5. Enable Apprise and repeat the synthetic filtered/unfiltered check above.

Never place real keys or decoded pager traffic in screenshots, issue reports,
test fixtures, or public logs.

## Automated tests

`PDWNotificationTests` covers independent channel routing, filtered-only push
routing, lock-screen-safe body construction, JSON escaping, severity mapping,
and transient-versus-terminal retry classification. It uses fake channels and
does not contact a real notification provider.

```powershell
cmake --build build --config Release --target PDW PDWNotificationTests
ctest --test-dir build -C Release --output-on-failure
```

Credential Manager persistence, dialog layout, a real TLS reverse proxy, and
phone delivery remain manual Windows integration tests.
