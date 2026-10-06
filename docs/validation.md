# Validation record

## 1.0.0

Built for Raspberry Pi Pico W/RP2040 with Arm GNU GCC 14.3.1 and the pinned Pico SDK. The source-derived QNX runner is recorded in `assets/runner.json`.

Automated checks on the development Mac:

- ASIX parsing/control/framing tests with ASan/UBSan.
- Packet-level production lwIP manager and access tests.
- Settings flash journal and 128 simulated interrupted program operations with ASan/UBSan.
- QNX runner logic built natively with ASan/UBSan.
- Four desktop/mobile Playwright browser tests using mocked API responses.
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

Do not label the pending checklist complete without recording real results. Compatibility with Android Auto, cluster modifications, navigation databases or unrelated payloads is separate from MST-Link's service transport.
