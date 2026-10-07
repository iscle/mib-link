#!/usr/bin/env python3
import hashlib, json, subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
value = {
    "sha256": hashlib.sha256((root / "assets/sd-runner.so").read_bytes()).hexdigest(),
    "compiler": subprocess.check_output(
        ["arm-none-eabi-gcc", "-dumpfullversion"], text=True
    ).strip(),
    "target": "QNX 6.5 ARM little-endian, soft-float",
    "sources": {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted((root / "runner").glob("*.[ch]"))
    },
}
(root / "assets/runner.json").write_text(json.dumps(value, indent=2) + "\n")
