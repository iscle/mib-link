#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sdk="${PICO_SDK_PATH:-$PWD/external/pico-sdk}"
revision=079c6f39023649b154152db30f1d781e884879bc
if [ ! -d "$sdk/.git" ]; then
    git clone https://github.com/raspberrypi/pico-sdk.git "$sdk"
fi
# Do not overwrite changes or a caller's independently managed SDK.
if [ "$(git -C "$sdk" rev-parse HEAD)" != "$revision" ]; then
    if [ -n "${PICO_SDK_PATH:-}" ]; then
        echo 'PICO_SDK_PATH must point at the documented revision' >&2
        exit 1
    fi
    git -C "$sdk" checkout "$revision"
fi
git -C "$sdk" submodule update --init lib/lwip lib/tinyusb lib/cyw43-driver lib/mbedtls
printf 'SDK ready. Run python3 build.py\n'
