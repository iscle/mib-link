# Architecture and local API

```text
Phone / laptop
    │ WPA2 Wi-Fi · 192.168.4.0/24
    ▼
Pico W / ESP32-S3 · 192.168.4.1
    ├── :80       offline page + bounded HTTP API
    ├── :2323     configurable TCP mapping → HU :23
    ├── :2222     configurable TCP mapping → HU :22
    └── :8080     configurable TCP mapping → HU :80
    │ ASIX-compatible USB Ethernet · 172.16.250.1
    ▼
Head unit · 172.16.250.248
    ├── existing services, with their original authentication
    └── optional SD runner in /ramdisk/pico-manager
```

Pico W uses a single polled lwIP event loop. ESP32-S3 uses ESP-IDF's Wi-Fi/TCP-IP task and one application task; both CPU cores remain enabled. Every shared-service and TinyUSB operation runs under lwIP's core mutex, with `CONFIG_LWIP_CHECK_THREAD_SAFETY` enabled. TinyUSB polling uses a zero timeout so it never sleeps holding the network lock. TCP forwarding uses bounded send windows and preserves half-closes. A full send window leaves the receive buffer owned by lwIP until retry; it does not discard or acknowledge undelivered bytes. There are three relay slots, three HTTP slots, and one browser console. Listeners bind only the Wi-Fi address; only the HTTP payload server is also exposed to the USB network.

All forwarding targets are fixed to the HU address. The page exposes three **TCP** port pairs, not an arbitrary remote-host proxy. UDP, multicast and general IP routing are intentionally outside this release.

`src/manager.c` handles the existing stock login and fixed SD-control commands. Passwords are sent only at login prompts, never interpolated into shell commands. Manual Telnet access suspends manager operations. The browser console is a separate Telnet client with bounded text output and session IDs to reject stale input; its ID is not an authentication credential.

`platform/pico_w/settings.c` alternates two flash erase sectors. Records carry a format version, sequence and CRC. Interrupts are disabled around the Pico SDK's flash routines; only core 0 is used. Settings apply after a delayed watchdog reboot, giving the HTTP response time to leave. Holding GP15 low at boot selects defaults without changing the flash journal. ESP32-S3 uses a versioned NVS blob with the same validation and delayed-reboot behavior; its recovery input defaults to GPIO4. No head-unit credentials are persisted on either platform.

## HTTP API

Base address: `http://192.168.4.1`. Request headers and responses are bounded. No request body is required. Successful writes return plain text; reads return JSON. Errors use 400, 403, 409, or 503 as appropriate. There is no management key. Mutations require custom headers, cross-origin access is not enabled, and Host/Origin checks restrict the browser interface to the local address. Controls are rejected on the USB interface.

| Method / path | Headers / purpose |
| --- | --- |
| `GET /status` | USB state, adapter counters, non-writing service probe |
| `GET /manage/status` | Authentication, cards, payload state, bounded log and progress |
| `POST /manage/action` | `X-MHI2-Action`: `connect`, `disconnect`, `scan`, `run`, `stop`, `status`, `auto-on`, `auto-off` |
| `GET /api/settings` | SSID, three `[local,remote]` pairs, active stream count, successfully bound listener count; **no password** |
| `POST /api/settings` | `X-MIB-SSID`, `X-MIB-Password`, `X-MIB-Forward1` through `X-MIB-Forward3` as `local:remote`; save and reboot |
| `GET /api/console` | Optional decimal `X-MIB-Cursor`; output as hex, next cursor, session ID, state, overflow indicator |
| `POST /api/console` | `X-MIB-Action`: `open`, `send`, `close`; `X-MIB-Session` and hex `X-MIB-Data` for input |

`connect` uses `X-HU-User` and `X-HU-Password` (up to 64 printable ASCII characters). `run` and `auto-on` use `X-MHI2-Slot` and `X-MHI2-Digest` from the card scan. Credentials are held only in RAM and are omitted from responses. SD selections and digests are validated before entering the fixed command template.

Settings writes must include all three port pairs. Zero pairs disable mappings; nonzero local ports must be unique and cannot be 80. Blank Wi-Fi password preserves the existing password. Invalid settings, active relays/consoles, or a busy SD manager reject the write.

The browser console accepts up to 256 decoded bytes per send, escaping literal Telnet IAC bytes. It exposes at most 1 KiB per poll from an 8 KiB ring. No terminal text is interpreted as HTML. Opening a second console is rejected; the existing session can be recovered by reloading the page.

## Resource limits

- Pico W: RP2040, 264 KiB SRAM, 2 MiB flash.
- ESP32-S3: at least 4 MiB flash; PSRAM is disabled and not required.
- USB full-speed, 64-byte endpoint packets; not a high-throughput storage bridge.
- Pico flash: first 2040 KiB for firmware; final 8 KiB for settings.
- ESP32-S3 flash: app at `0x10000` (3 MiB partition); settings in NVS at `0x9000` (24 KiB).
- TCP relays: three streams, 15-second connection timeout, 30-minute data-idle timeout.
- Browser console: one session, 15-second connection timeout, 10-minute input-idle timeout.
- HTTP: three clients, 1280-byte request buffer, 15-second stalled-client timeout.
- Built-in Wi-Fi DHCP does not advertise Internet access or DNS.

Powering off the adapter removes access through it. It does not terminate independent head-unit processes. A firmware or Wi-Fi change cannot undo arbitrary commands issued in the console.
