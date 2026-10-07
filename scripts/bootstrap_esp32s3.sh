#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
revision=30aaf64524299d3bde422ca9a2848090d1bc5d0f
idf="${IDF_PATH:-$PWD/external/esp-idf}"
if [ ! -d "$idf/.git" ]; then
    git clone --branch v5.5.2 --depth 1 https://github.com/espressif/esp-idf.git "$idf"
fi
if [ "$(git -C "$idf" rev-parse HEAD)" != "$revision" ]; then
    echo 'Use the pinned ESP-IDF v5.5.2 revision (see docs/esp32s3.md)' >&2; exit 1
fi
git -C "$idf" submodule update --init --recursive --depth 1
tusb="$PWD/external/tinyusb"
if [ ! -d "$tusb/.git" ]; then
    git clone --branch 0.18.0 --depth 1 https://github.com/hathach/tinyusb.git "$tusb"
    git -C "$tusb" checkout 86ad6e56c1700e85f1c5678607a762cfe3aa2f47
fi
if [ "$(git -C "$tusb" rev-parse HEAD)" != 86ad6e56c1700e85f1c5678607a762cfe3aa2f47 ]; then
    echo 'Unexpected TinyUSB revision' >&2; exit 1
fi
"$idf/install.sh" esp32s3
printf '\nNext: source %s/export.sh\nThen: python3 build.py --target esp32s3\n' "$idf"
