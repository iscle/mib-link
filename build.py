#!/usr/bin/env python3
"""Build MST-Link for Pico W or ESP32-S3 with its pinned SDK."""
import os
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
from scripts.generate import generate

ROOT = Path(__file__).resolve().parent
SDK_COMMIT = "079c6f39023649b154152db30f1d781e884879bc"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=["pico-w", "esp32s3"], default="pico-w")
    if parser.parse_args().target == "esp32s3":
        from scripts.build_esp32s3 import build

        build()
        return
    sdk = Path(os.environ.get("PICO_SDK_PATH", ROOT / "external/pico-sdk")).resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != SDK_COMMIT:
        raise SystemExit("Unexpected Pico SDK revision; run scripts/bootstrap.sh")
    os.environ["PICO_SDK_PATH"] = str(sdk)
    generate()
    args = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(ROOT / "build"),
        "-G",
        "Ninja",
        "-DPICO_BOARD=pico_w",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DPICOTOOL_GIT_BRANCH=2.3.0",
    ]
    if os.environ.get("PICO_TOOLCHAIN_PATH"):
        args += ["-DPICO_TOOLCHAIN_PATH=" + os.environ["PICO_TOOLCHAIN_PATH"]]
    subprocess.run(args, check=True)
    subprocess.run(["cmake", "--build", str(ROOT / "build"), "-j", "4"], check=True)
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    for ext in ["uf2", "bin", "elf"]:
        shutil.copy2(ROOT / "build" / ("mst_link." + ext), dist / ("mst-link-pico-w." + ext))
    uf2 = dist / "mst-link-pico-w.uf2"
    digest = hashlib.sha256(uf2.read_bytes()).hexdigest()
    (dist / "SHA256SUMS").write_text(digest + "  " + uf2.name + "\n")
    print("Built", uf2)


if __name__ == "__main__":
    main()
