# Changelog

## 1.2.1

- Remove the introductory banner so workspace controls appear directly beneath the header.
- Move board/version details to the footer and remove unused banner styles.

## 1.2.0

- Rename the project, repository, firmware downloads, USB identity and API headers to MIB-Link.
- Default Wi-Fi is now `MIB-Link` / `miblink1`.
- Use a new settings namespace on both chips; upgrading from an earlier release starts with the new defaults.
- Apply consistent code formatting across firmware, tools and web UI, with a pinned CI formatting check.

## 1.1.0

- ESP32-S3 target with native USB ASIX emulation, WPA2 AP, browser console, TCP forwarding and SD management.
- Shared service/USB implementation with isolated RP2040 and ESP-IDF platform code.
- NVS settings, GPIO4 recovery and an ESP32-S3 flash bundle that preserves settings.
- Platform-neutral page labels, live board identification and instructions for both chips.
- Both firmware builds and platform settings tests in CI; physical acceptance remains pending.

## 1.0.0

Initial standalone MIB-Link release for Raspberry Pi Pico W.

- Configurable WPA2 access point with `MIB-Link` / `miblink1` defaults.
- Persistent Wi-Fi/TCP settings, alternating CRC flash records and GP15 recovery.
- Offline responsive UI with SD management, live payload logs and storage progress.
- Browser Telnet line console with masked input, Ctrl+C and bounded output.
- Three user-configurable, protocol-independent TCP port mappings.
- Clean generic firmware with no embedded HU credentials or AA/cluster payload.
- Source-built QNX RAM runner with checked provenance and standalone build tools.
- Host protocol, state, settings, runner, browser and UF2 validation; hardware acceptance pending.
