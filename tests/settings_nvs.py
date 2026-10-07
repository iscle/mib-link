#!/usr/bin/env python3
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
(ROOT / "build/tests").mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        "cc",
        "-std=c11",
        "-g",
        "-O1",
        "-fsanitize=address,undefined",
        "-DMIB_NVS_TEST",
        "-Isrc",
        "-Itests/host",
        "src/settings.c",
        "platform/esp32s3/main/settings.c",
        "tests/host/settings_nvs.c",
        "-o",
        "build/tests/settings_nvs",
    ],
    cwd=ROOT,
    check=True,
)
subprocess.run([str(ROOT / "build/tests/settings_nvs")], check=True)
