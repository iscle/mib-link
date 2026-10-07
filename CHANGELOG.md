# Changelog

## 1.1.0

- ESP32-S3 target with native USB ASIX emulation, WPA2 AP, browser console, TCP forwarding and SD management.
- Shared service/USB implementation with isolated RP2040 and ESP-IDF platform code.
- NVS settings, GPIO4 recovery and an ESP32-S3 flash bundle that preserves settings.
- Platform-neutral page labels, live board identification and instructions for both chips.
- Both firmware builds and platform settings tests in CI; physical acceptance remains pending.

## 1.0.0

Initial standalone MST-Link release for Raspberry Pi Pico W.

- Configurable WPA2 access point with `MST-Link` / `mstlink1` defaults.
- Persistent Wi-Fi/TCP settings, alternating CRC flash records and GP15 recovery.
- Offline responsive UI with SD management, live payload logs and storage progress.
- Browser Telnet line console with masked input, Ctrl+C and bounded output.
- Three user-configurable, protocol-independent TCP port mappings.
- Clean generic firmware with no embedded HU credentials or AA/cluster payload.
- Source-built QNX RAM runner with checked provenance and standalone build tools.
- Host protocol, state, settings, runner, browser and UF2 validation; hardware acceptance pending.
