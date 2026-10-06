#!/usr/bin/env python3
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
(ROOT/'build/tests').mkdir(parents=True,exist_ok=True)
subprocess.run(['cc','-std=c11','-g','-O1','-fsanitize=address,undefined','-DMST_SETTINGS_TEST','-Isrc','-Itests/host','src/settings.c','tests/host/settings_flash.c','-o','build/tests/settings'],cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/tests/settings')],check=True)
