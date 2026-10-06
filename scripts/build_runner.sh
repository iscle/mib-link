#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
: "${QNX_TARGET:?Set QNX_TARGET to the target/qnx6 directory of your QNX 6.5 ARM SDP}"
mkdir -p build/runner
flags=(-mfloat-abi=soft -mcpu=cortex-a9 -marm -mno-unaligned-access -fPIC -ffreestanding -fno-builtin -O2 -std=gnu11 -Wall -Wextra -Werror -Wno-misleading-indentation)
for unit in runner manifest sha256; do
    arm-none-eabi-gcc "${flags[@]}" -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ -isystem "$QNX_TARGET/usr/include" -c "runner/$unit.c" -o "build/runner/$unit.o"
done
runtime=$(arm-none-eabi-gcc "${flags[@]}" -print-libgcc-file-name)
arm-none-eabi-ld -shared --hash-style=sysv -init mhi2_sd_runner -soname sd-runner.so -o build/runner/sd-runner.so build/runner/{runner,manifest,sha256}.o --exclude-libs=ALL "$runtime"
if arm-none-eabi-nm -u build/runner/sd-runner.so | grep -q __aeabi; then echo 'Unresolved compiler runtime helper' >&2; exit 1; fi
cp build/runner/sd-runner.so assets/sd-runner.so
python3 scripts/runner_provenance.py
