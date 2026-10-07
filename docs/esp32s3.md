# ESP32-S3

MST-Link builds for **ESP32-S3 boards with at least 4 MB flash and native USB exposed**. The generic image uses DIO flash at 40 MHz, does not require PSRAM, and keeps both CPU cores enabled. It provides the same Wi-Fi settings, browser console, three TCP mappings and SD bundle manager as Pico W.

This target is build- and automated-tested. Physical ESP32-S3 enumeration, Wi-Fi and head-unit acceptance are still pending. A successful build is not evidence that a particular board or HU has passed those checks.

## USB and board connections

Use the board's **native USB / USB-OTG** connector. It connects **GPIO19 to D−** and **GPIO20 to D+**. A connector labeled UART, COM, CP210x or CH340 is generally a USB-to-UART bridge and cannot act as the head unit's Ethernet adapter. On boards with two connectors, use the native one for the HU; the UART connector may still be used for flashing and logs.

The application takes over the native USB PHY for ASIX emulation. USB serial/JTAG and CDC console are not enabled on that connector while MST-Link runs. Diagnostic logs use UART0. Native USB flashing remains available through the chip's ROM download mode.

Use a board designed to receive USB bus power, with its native USB connector and regulator wired correctly. The S3 descriptor requests up to 500 mA. For a custom/self-powered board, review VBUS sensing and power isolation before connecting it; do not connect two independent USB power sources together through unprotected 5 V rails. The generic target has no board-specific RGB/status LED driver.

Espressif documents the fixed USB pin mapping and native-port requirement in its [USB Device Stack guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/usb_device.html).

## Flash a release

1. Download and extract `mst-link-esp32s3.zip` from [Releases](https://github.com/iscle/mst-link/releases/latest).
2. Install the flashing tool in a Python environment: `python3 -m pip install 'esptool>=4.12,<6'`.
3. Hold **BOOT** (GPIO0 low), press/release **RESET/EN**, then release BOOT. Identify the ROM serial port on the computer. For a board without buttons, follow its documented download-mode wiring.
4. Run:

   ```sh
   python3 flash.py --port /dev/cu.usbmodemYOUR_PORT
   ```

   On Windows use the corresponding `COM` port. If the board has a USB-UART bridge, it may also be used for this flashing step.

5. Reset normally with BOOT released. Connect the native USB connector to the head unit. Join **MST-Link**, password **mstlink1**, then open **http://192.168.4.1/**.

`flash.py` verifies each image hash and writes exactly these regions:

| Offset | File | Purpose |
| --- | --- | --- |
| `0x0` | `bootloader.bin` | ESP32-S3 bootloader |
| `0x8000` | `partition-table.bin` | Partition layout |
| `0x10000` | `mst-link-esp32s3.bin` | Application |

Do not flash the application at address zero. The helper deliberately does not write NVS at `0x9000–0xefff`, so ordinary updates preserve settings. It also does not erase other partitions. Migrating from unrelated firmware may require an explicit settings reset if UART logs report incompatible/corrupt NVS; erasing NVS removes stored Wi-Fi settings. The application never automatically erases NVS on an initialization error.

## Settings recovery

With the board powered off, connect **GPIO4 to GND**, then power it on normally. Remove the jumper after startup. Connect using the default Wi-Fi and save replacement settings. The old NVS value is retained until a successful save.

GPIO0/BOOT is reserved for ROM download mode, not application recovery. GPIO19/20 are reserved for USB. Recovery GPIO can be changed under **MST-Link** in `idf.py menuconfig`, or set to `-1` to disable it. Choose an otherwise unused, input-capable pin and consult the board's schematic; flash/PSRAM pins and boot straps are not suitable. GPIO4 is available on the generic DevKit-style target but may already serve another function on a custom board.

## Build

Pinned dependencies:

- **ESP-IDF v5.5.2**, commit `30aaf64524299d3bde422ca9a2848090d1bc5d0f`.
- **TinyUSB 0.18.0**, commit `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`, also used by the pinned Pico SDK.

```sh
./scripts/bootstrap_esp32s3.sh
. external/esp-idf/export.sh
python3 build.py --target esp32s3
python3 tests/check_esp32s3.py
```

The bootstrap installs Espressif's cross-toolchain for the host and initializes SDK submodules. An existing SDK may be selected using `IDF_PATH`; its revision must match. Build outputs are under `build/esp32s3`, and the distributable is `dist/mst-link-esp32s3.zip` with `dist/ESP32S3-SHA256SUMS`.

For menuconfig, after bootstrap/export:

```sh
idf.py -C platform/esp32s3 -B build/esp32s3 menuconfig
python3 build.py --target esp32s3
```

`sdkconfig` is a local generated file; reviewed defaults live in `sdkconfig.defaults`. Do not enable USB serial/JTAG console, CDC, PSRAM, single-core mode, or disable TCP/IP core locking/thread checks without understanding the resulting board/driver changes.

## Concurrency

The common raw-lwIP services and ASIX adapter state are protected by the ESP-IDF TCP/IP core mutex. The Wi-Fi task and the application USB loop share that mutex, so callbacks cannot modify the adapter queues or PCB state concurrently. TinyUSB uses FreeRTOS event queues but is polled with zero timeout; no blocking USB task owns the network mutex. NVS commits complete before the delayed restart is scheduled.

This follows Espressif's requirement to check/lock non-socket lwIP access; see its [lwIP threading documentation](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32/api-guides/lwip.html). The ESP-IDF raw API is an adapted interface; hardware stress testing remains part of acceptance.
