# SDR# + VB-Audio Cable (Adelaide FLEX)

This named local-input profile preserves the decoder settings that restored live FLEX decoding on the Adelaide test setup. It is an explicit clean-install or operator-confirmed choice; PDW never silently applies these radio-specific values to the Standard profile.

SDR# and VB-Audio Cable are external, operator-installed products. PDW does not bundle, license, install, tune or configure either product.

> [!CAUTION]
> This profile is retained in Public Beta 2 for community testing. Automated
> endpoint, fail-closed, profile and installer checks pass, but the exact
> physical SDR#/VB-CABLE workflow has not been maintainer-validated. Confirm
> lawful use, protect operator configuration, and report only content-free
> results through the maintained PDW v5.5.1 beta feedback form on GitHub.

## Signal path

Configure SDR# manually as follows:

- frequency: `148.8125 MHz`
- mode: `NFM`
- Filter Audio: off (`filterAudio=False`)
- audio: unmuted
- output: `[WASAPI] CABLE Input (VB-Audio Virtual Cable)`

PDW captures the matching recording endpoint, `CABLE Output (VB-Audio Virtual Cable)`. `CABLE Input` is the playback side selected in SDR#; `CABLE Output` is the capture side selected in PDW.

The installer option and packaged profile set:

```ini
[PDW]
AudioEnabled=1
AudioSource=0
AudioDevice=0
AudioSampleRate=44100
AudioConfiguration=0
ComPortEnabled=0
DecodePocsag=1
DecodeFlex=1
PocsagFlex=1
PocsagShowBoth=1
ShowCFS=1
Flex1600=1
Flex3200=0
Flex6400=0
BTSYNC=13107
MINMSG=15
InvertData=1
Percent=69
Threshold1600=2

[InputProfile]
PresetId=sdrsharp-vbcable-adelaide-flex-v1
PresetName=SDR# + VB-Audio Cable (Adelaide FLEX)
DeviceEndpointId=
DeviceFriendlyName=CABLE Output (VB-Audio Virtual Cable)
IdentityInvalid=0
```

`AudioDevice=0` is retained only for compatibility. Before first capture, PDW resolves the unique VB-Cable recording endpoint by name and durably records its opaque Windows endpoint ID. It then opens that exact ID through WASAPI from the start, even if WinMM device ordering changes. If identity persistence fails or the stored endpoint is missing, PDW stops local capture and asks the operator to choose an input; it does not silently capture the default microphone.

## Install, reinstall and explicit apply behavior

- A clean installer offers `Standard PDW settings` and `SDR# + VB-Audio Cable (Adelaide FLEX)`.
- If `PDW.INI` already exists, setup does not replace or modify it, regardless of the selected profile or command-line option.
- Existing and portable installations can use **Radio and Signal Sources > Apply Adelaide FLEX...**. PDW previews the decoder/slicer changes, requires confirmation, resolves one unique active VB-Cable endpoint, and refuses ambiguous or missing devices.
- Before an approved apply, PDW probes the exact endpoint, creates a verified byte-exact `PDW.INI.pre-adelaide-flex-YYYYMMDD-HHMMSS.bak` copy, and commits the new INI through a same-directory temporary file. The backup can contain private operator settings and must be protected accordingly.
- The clean-install Adelaide file has only the documented differences from Standard because Standard already supplies many compatible defaults. The in-app **Apply Adelaide FLEX...** command is deliberately broader: after its exact preview and default-No confirmation it resets the complete known-good local-input, decoder and Custom-slicer state. This is reversible through the verified backup and is never run automatically.
- Fresh installations do not contain `filters.ini`. Profile choice never creates, imports, replaces or removes filters, the Capcode Directory database, aliases, hit counters or migration markers. Existing legacy `filters.ini` files remain available only for PDW's established one-time Capcode Directory migration and recovery path.
- Reinstall and normal uninstall preserve `PDW.INI`, `pdw-history.sqlite3`, existing legacy filters, receivers, WAV files and other operator data.

This profile does not tune or reconfigure SDR#. A live meter proves only that audio is arriving; verify real FLEX traffic after changes to SDR#, Windows audio devices, or VB-Cable.

## Lawful use and validation evidence

Only monitor radio traffic where reception and use are lawful and authorised.
Public test evidence, screenshots and fixtures must use synthetic, redacted or
licensed representative data; do not publish decoded traffic, capcodes, real
endpoint IDs or operator settings. Until representative Public Beta 2 reports
complete the documented physical SDR#/VB-Cable workflow, describe this profile
as configured or operator-reported rather than hardware-validated.
