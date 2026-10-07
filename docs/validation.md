# Validation record

## 1.2.0

MIB-Link branding, filenames, API headers and settings namespaces are covered by the host and browser tests. Both firmware targets are rebuilt from the renamed source tree and checked with the image validators. Physical ESP32-S3 and head-unit acceptance remain pending.

## 1.1.0

Two firmware targets: Pico W/RP2040 and generic ESP32-S3 (4 MB DIO/40 MHz, no PSRAM). Local cross-builds succeeded with the pinned Pico SDK and ESP-IDF v5.5.2. The shared host integration tests and browser tests pass after platform separation.

Added checks:

- ESP32-S3 NVS persistence, invalid version/length, failed open/write/commit, restart timing and recovery input, using the real settings module with a simulated NVS backend under ASan/UBSan.
- Actual ESP32-S3 image chip ID, segment checksum and appended SHA-256.
- Partition-table checksum/layout and flash-helper region validation, including corruption/unsafe-offset rejection and preservation of the NVS region.
- SDK configuration requires both CPU cores, no PSRAM, native USB without USB serial/JTAG console, TCP/IP core locking, and lwIP thread checks.
- CI builds and validates both targets independently.

**Physical ESP32-S3 testing is pending.** The generic build makes no assumption about board-specific RGB LEDs. Before acceptance, verify native USB enumeration and packet traffic on a supported HU, Wi-Fi reconnects and multiple clients, NVS persistence/recovery, and simultaneous console/TCP/SD activity under load. The existing physical checklist below remains applicable to both targets, using GPIO4 instead of GP15 for ESP32-S3 settings recovery.

## 1.0.0

Built for Raspberry Pi Pico W/RP2040 with Arm GNU GCC 14.3.1 and the pinned Pico SDK. The source-derived QNX runner is recorded in `assets/runner.json`.

Automated checks on the development Mac:

- ASIX parsing/control/framing tests with ASan/UBSan.
- Packet-level production lwIP manager and access tests.
- Settings flash journal and 128 simulated interrupted program operations with ASan/UBSan.
- QNX runner logic built natively with ASan/UBSan.
- Five desktop/mobile Playwright browser tests using mocked API responses.
- Native RP2040 build and UF2 round-trip/boundary/checksum checks.

These checks validate the implemented software paths. Simulated peers are not the physical head unit; mocked browser status is not hardware evidence. The existing USB adapter and stock-login predecessor worked on the owner's Porsche P5250, but **this standalone release has not yet been flashed and accepted on that vehicle**.

## Physical acceptance checklist (pending)

- [ ] Flash the release UF2 on a Pico W; join default Wi-Fi and load all page tabs.
- [ ] Confirm real HU USB enumeration, network address and stock Telnet login.
- [ ] Change SSID/password/mappings; power-cycle and verify persistence.
- [ ] Test GP15-to-GND recovery, remove jumper and save replacement settings.
- [ ] Use browser console: login, harmless command, Ctrl+C, disconnect, reconnect.
- [ ] Use native Telnet through 2323 and an additional service the HU actually runs.
- [ ] Run and stop the harmless SD example; verify no persistent HU changes.
- [ ] Exercise USB/Wi-Fi loss and recovery during idle and active sessions.
- [ ] Monitor memory/stability over an extended real session.

Do not label the pending checklist complete without recording real results. Compatibility with Android Auto, cluster modifications, navigation databases or unrelated payloads is separate from MIB-Link's service transport.
