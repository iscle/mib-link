#!/usr/bin/env python3
"""Offline visual preview only; API routes are provided by the browser tests."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from generate import generate

generate()
root = Path(__file__).resolve().parents[1] / "generated"
print("MST-Link UI preview: http://127.0.0.1:8765 (no head unit attached)", flush=True)
ThreadingHTTPServer(
    ("127.0.0.1", 8765), partial(SimpleHTTPRequestHandler, directory=str(root))
).serve_forever()
