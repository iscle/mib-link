# Third-party notices

MST-Link is GPL-3.0-or-later. SPDX identifiers apply to the project's C sources; the same project license covers its scripts, web interface, tests and documentation unless stated otherwise.

- Raspberry Pi Pico SDK: BSD-3-Clause and component-specific licenses. Pinned in `build.py`; notice in `licenses/pico-sdk.txt`.
- lwIP: BSD-style license, `licenses/lwip.txt`.
- TinyUSB: MIT, `licenses/tinyusb.txt`.
- CYW43 driver and wireless firmware: their original licenses, including Raspberry Pi Pico W-specific distribution terms, in `licenses/cyw43-driver*.txt`. Build and use this firmware for the supported Raspberry Pi Pico W.
- DHCP server: derived from MicroPython, copyright Damien P. George, MIT; full notice retained in `vendor/dhcpserver.c` and `.h`.
- GCC compiler runtime: GPLv3 with GCC Runtime Library Exception 3.1. The QNX runner links compiler support routines, not proprietary QNX libraries. The compiler/runtime source corresponding to GNU Arm 14.3.Rel1 is available from Arm's GNU toolchain distribution and GCC. See `licenses/gcc-runtime-exception.txt`.
- Playwright: Apache-2.0; development-only dependency pinned in `package-lock.json`, not included in the Pico firmware.

The prebuilt `assets/sd-runner.so` is compiled from the included `runner/` sources. Its rebuild script, source hashes, and compiler version are provided. The regular build verifies them. No OEM head-unit firmware, QNX headers, vehicle images, or credentials are redistributed.
