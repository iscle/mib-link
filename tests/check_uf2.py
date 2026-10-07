#!/usr/bin/env python3
"""Validate RP2040 UF2 block structure and reconstructed flash bytes."""
from pathlib import Path
import hashlib
import struct
import argparse

ROOT = Path(__file__).resolve().parents[1]
stem = "mib-link-pico-w"
path = ROOT / "dist" / (stem + ".uf2")
raw = path.read_bytes()
binary = (ROOT / "dist" / (stem + ".bin")).read_bytes()
assert len(raw) % 512 == 0
flash = {}
count = len(raw) // 512
for number in range(count):
    block = raw[number * 512 : (number + 1) * 512]
    a, b, flags, address, size, index, total, family = struct.unpack("<8I", block[:32])
    assert (a, b) == (0x0A324655, 0x9E5D5157)
    assert struct.unpack("<I", block[508:])[0] == 0x0AB16F30
    assert flags == 0x2000 and family == 0xE48BFF56 and size == 256
    assert index == number and total == count
    assert 0x10000000 <= address < 0x101FE000 and address % 256 == 0
    assert address not in flash
    flash[address] = block[32 : 32 + size]
restored = b"".join(flash[at] for at in range(0x10000000, max(flash) + 256, 256))
assert restored[: len(binary)] == binary
assert not any(restored[len(binary) :])
assert b"MIB-Link" in binary and b"miblink1" in binary
assert b"test-only-password" not in binary
digest = hashlib.sha256(raw).hexdigest()
assert (ROOT / "dist" / "SHA256SUMS").read_text().split()[0] == digest
print(
    f"PASS: RP2040 UF2, {count} blocks, {len(binary)} flash bytes, bin round-trip and SHA256 match"
)
