# Building MST-Link

## Prerequisites

- Python 3.9 or later, Git, CMake 3.20 or later, Ninja, and a native C/C++ compiler.
- Arm GNU `arm-none-eabi` toolchain. Release artifacts were built with **14.3.Rel1 (GCC 14.3.1)**.
- Node.js 20 or later for UI tests only.
- Raspberry Pi Pico SDK at commit **079c6f39023649b154152db30f1d781e884879bc**. `scripts/bootstrap.sh` initializes the required submodules.
- Picotool **2.3.0** is fetched by CMake to generate the UF2. The first configure therefore needs Internet access.

```sh
./scripts/bootstrap.sh
export PICO_TOOLCHAIN_PATH=/absolute/path/to/arm-gnu-toolchain/bin
python3 build.py
```

An existing SDK can be selected with `PICO_SDK_PATH`; the build rejects a different commit. `bootstrap.sh` will not switch a caller-supplied SDK revision. No dependencies are fetched by the running firmware.

Outputs:

```text
dist/mst-link-pico-w.uf2   # flash this file
dist/mst-link-pico-w.bin   # byte-for-byte firmware image
dist/mst-link-pico-w.elf   # debugging symbols
dist/SHA256SUMS
```

The final 8 KiB of 2 MiB flash are reserved for settings by a linker-script override. UF2 validation rejects any firmware block in that area. Wi-Fi defaults are in `src/settings.c`; runtime overrides are journaled on the Pico. Credentials for the head unit are never part of the build.

## Tests

```sh
./tests/run.sh
python3 tests/check_uf2.py
npm ci
npx playwright install chromium
npm test
```

`tests/run.sh` uses the same lwIP revision as the firmware. It compiles the actual C modules into a native packet harness, then drives them with simulated AP and HU peers. Settings flash, ASIX parsing, and the native SD runner have sanitizer tests. UF2 validation requires a completed firmware build.

On macOS, browser tests use the installed Google Chrome application. On Linux, they use Playwright Chromium. Screenshots are refreshed in `docs/overview.png` and `docs/mobile.png`. API responses in these screenshots are fixtures, not a live vehicle.

To preview the embedded page locally:

```sh
python3 scripts/preview.py
```

Open `http://127.0.0.1:8765/`. This preview does not provide a head-unit backend; it displays an unreachable status outside the browser tests.

## Rebuild the QNX runner

`assets/sd-runner.so` is compiled from this repository's `runner/` sources. It is not an OEM binary. It is included so a standard Pico build needs no proprietary headers. `scripts/generate.py` checks its SHA-256 **and** each source hash before embedding it.

Rebuilding it requires your own compatible QNX 6.5 ARM SDP headers. No QNX SDK files or libraries are distributed here. The cross-compile uses the Arm GNU compiler above and links its compiler runtime; HU libc symbols are resolved on the head unit.

```sh
export PATH="$PICO_TOOLCHAIN_PATH:$PATH"
export QNX_TARGET=/absolute/path/to/sdp/target/qnx6
./scripts/build_runner.sh
python3 build.py
```

This updates `assets/sd-runner.so` and `assets/runner.json`. Commit the source, asset and provenance together. Changing the runner source without rebuilding causes the regular build to fail. The exact build flags are in `scripts/build_runner.sh`.

The ELF uses a constructor loaded into a stock QNX process. This unusual compatibility mechanism is isolated to the SD controller; raw forwarding and the browser console do not load it.

## Release process

1. Run host, browser, and UF2 checks from a clean checkout.
2. Review the hardware acceptance checklist in `validation.md`; record what was actually tested.
3. Tag the reviewed source and attach the clean UF2 and `SHA256SUMS` to a GitHub release.
4. Never include personalized firmware, credentials, captures, QNX headers, OEM files, or vehicle storage dumps.
