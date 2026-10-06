#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
python3 scripts/generate.py
cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Isrc src/asix_protocol.c tests/test_protocol.c -o build/tests/test_protocol
build/tests/test_protocol
python3 tests/settings.py
python3 tests/access.py
python3 tests/sd_runner.py
