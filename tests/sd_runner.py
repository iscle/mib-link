#!/usr/bin/env python3
"""Exercise the actual HU runner against disposable mounted-volume fixtures."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from make_sd_bundle import generate

with tempfile.TemporaryDirectory(prefix="mhi2-sd-test-") as tmp:
    tmp = Path(tmp).resolve()
    fs = tmp / "fs"
    fs.mkdir()
    ram = tmp / "ram"
    firmware = tmp / "jxe"
    firmware.write_bytes(b"original-test-jxe")
    binary = tmp / "runner"
    subprocess.run(
        [
            "cc",
            "-std=gnu11",
            "-g",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-misleading-indentation",
            "-fsanitize=address,undefined",
            "-DSD_HOST_TEST",
            "-DSD_SCRIPT_TIMEOUT=500",
            f'-DSD_FS="{fs}"',
            f'-DSD_RAM="{ram}"',
            f'-DSD_FIRMWARE="{firmware}"',
            str(ROOT / "runner/runner.c"),
            str(ROOT / "runner/manifest.c"),
            str(ROOT / "runner/sha256.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )

    def run(*args, ok=True):
        p = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=5)
        assert (p.returncode == 0) == ok, (args, p.returncode, p.stdout, p.stderr)
        assert not p.stderr, p.stderr
        return p.stdout

    def bundle(slot="sda0", start=None, stop=None, profile="any"):
        root = fs / slot / "mhi2"
        root.mkdir(parents=True, exist_ok=True)
        (root / "start.sh").write_text(
            start or '#!/bin/sh\necho hello\necho yes > "$MHI2_RUN_DIR/marker"\n'
        )
        (root / "stop.sh").write_text(
            stop or '#!/bin/sh\nrm -f "$MHI2_RUN_DIR/marker"\necho stopped\n'
        )
        return root, generate(root, "runner-test", profile)

    import random

    random_bytes = random.Random(42)
    for length in [
        0,
        1,
        3,
        55,
        56,
        63,
        64,
        65,
        119,
        120,
        127,
        128,
        129,
        8191,
        8192,
        8193,
        65537,
        3 * 1024 * 1024,
    ]:
        data = bytes(random_bytes.randrange(256) for _ in range(length))
        fixture = tmp / "hash-input"
        fixture.write_bytes(data)
        assert run("hash", str(fixture)).strip() == hashlib.sha256(data).hexdigest(), length
    assert "PICOSD_COUNT 0" in run("scan")
    root, digest = bundle()
    assert digest in run("scan")
    assert "active" in run("run", "sda0", digest)
    assert (ram / "active/marker").read_text() == "yes\n"
    run("run", "sda0", digest, ok=False)
    shutil.rmtree(fs / "sda0")
    assert "active" in run("status")
    record = "v1 copying mmx-emmc 1024 0 0 8192 1 1024 8192 1000 1"
    (ram / "progress").write_text(record + "\n")
    assert "PICOSD_PROGRESS " + record in run("status")
    (ram / "progress").write_text(record + "\nPICOSD_STATE inactive\n")
    assert "PICOSD_PROGRESS" not in run("status")
    (ram / "progress").unlink()
    (ram / "progress").symlink_to(firmware)
    assert "PICOSD_PROGRESS" not in run("status")
    (ram / "progress").unlink()

    assert "inactive" in run("stop")
    assert not (ram / "active").exists()
    run("stop")
    root, digest = bundle()
    (root / "start.sh").write_text("echo changed\n")
    assert "file-missing-changed-or-unsafe" in run("run", "sda0", digest, ok=False)
    assert not (ram / "active/marker").exists()
    root, digest = bundle()
    generate(root, "changed-version")
    assert "bundle-changed" in run("run", "sda0", digest, ok=False)
    root, digest = bundle(profile="0" * 64)
    assert "unsupported" in run("scan")
    run("run", "sda0", digest, ok=False)
    root, digest = bundle(profile=hashlib.sha256(firmware.read_bytes()).hexdigest())
    run("run", "sda0", digest)
    run("stop")
    root, digest = bundle(start='#!/bin/sh\necho partial > "$MHI2_RUN_DIR/marker"\nexit 2\n')
    assert "start-failed-cleanup-complete" in run("run", "sda0", digest, ok=False)
    assert "inactive" in run("status")
    root, digest = bundle(start="#!/bin/sh\nexit 3\n", stop="#!/bin/sh\nexit 4\n")
    assert "start-and-cleanup-failed" in run("run", "sda0", digest, ok=False)
    assert "cleanup-required" in run("status")
    run("run", "sda0", digest, ok=False)
    (ram / "active/stop.sh").write_text("exit 0\n")
    run("stop")
    root, digest = bundle(start="#!/bin/sh\nwhile :; do echo flooding; done\n")
    run("run", "sda0", digest, ok=False)
    assert (ram / "log").stat().st_size <= 65536
    root, digest = bundle()
    outside = tmp / "outside"
    outside.write_text("echo unsafe\n")
    (root / "start.sh").unlink()
    (root / "start.sh").symlink_to(outside)
    run("run", "sda0", digest, ok=False)
    assert not (ram / "active").exists()
    (root / "start.sh").unlink()
    root, digest = bundle()
    manifest = json.loads((root / "manifest.json").read_text())
    for bad in [
        "../escape",
        "/tmp/escape",
        "payload/../../escape",
        "payload//x",
        "x;reboot",
        "manifest.json",
    ]:
        bad_m = json.loads(json.dumps(manifest))
        bad_m["files"][0]["path"] = bad
        (root / "manifest.json").write_text(json.dumps(bad_m))
        assert "PICOSD_COUNT 0" in run("scan"), bad
    for change in ("duplicate", "oversize", "unknown", "nul"):
        m = json.loads(json.dumps(manifest))
        if change == "duplicate":
            m["files"].append(m["files"][0])
        elif change == "oversize":
            m["files"][0]["size"] = 1024 * 1024 + 1
        elif change == "unknown":
            m["unexpected"] = True
        data = json.dumps(m).encode() + (b"\0" if change == "nul" else b"")
        (root / "manifest.json").write_bytes(data)
        assert "PICOSD_COUNT 0" in run("scan"), change
    root, digest = bundle()
    bundle("sdb0")
    assert "PICOSD_COUNT 2" in run("scan")
    shutil.rmtree(fs / "sda0")
    run("run", "sda0", digest, ok=False)
    assert firmware.read_bytes() == b"original-test-jxe"
print(
    "PASS: native runner staging, hashes/profiles, two cards, removal, start/stop, failed-start cleanup, timeout/log bound, malformed manifests and symlinks (ASan/UBSan)"
)
