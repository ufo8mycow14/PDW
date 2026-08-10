# Optional USB driver tool

`zadig-2.9.exe` is the unmodified official Zadig 2.9 release. It installs a
libusb-compatible Windows USB driver and requires administrator approval.

For a typical RTL-SDR receiver:

1. Disconnect other non-essential USB devices.
2. Run Zadig and choose **Options > List All Devices** only if the receiver is
   not already visible.
3. Select the RTL-SDR receiver's Bulk-In Interface 0 (often shown as
   `RTL2838UHIDIR` or `Bulk-In, Interface (Interface 0)`).
4. Confirm the device's USB ID before choosing **WinUSB** and installing it.

Do not select a keyboard, mouse, storage device, hub, or unrelated receiver.
PDW does not launch Zadig or install/change drivers automatically.

Provenance:

- Project: https://github.com/pbatard/libwdi
- Release: v1.5.1 / Zadig 2.9
- `zadig-2.9.exe` SHA-256: `4ECAA95DF3DA3621486A043AEF8B3050B8BAFE7C901402871E816229EF82039B`
- Licence: GNU GPL version 3

The matching source archive and licence are included in this folder.
