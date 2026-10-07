#!/usr/bin/env python3
"""Format project-owned source files, or check them without modifying files."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {
    "vendor",
    "licenses",
    "external",
    "build",
    "dist",
    "generated",
    "node_modules",
    "private",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="fail if formatting changes are needed"
    )
    args = parser.parse_args()
    os.chdir(ROOT)
    # Include new source files before their first commit, but honor .gitignore.
    listed = (
        subprocess.check_output(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
        )
        .decode()
        .split("\0")
    )
    files = sorted(
        {
            name
            for name in listed
            if name and Path(name).is_file() and Path(name).parts[0] not in EXCLUDED
        }
    )
    groups = {
        "clang-format": [f for f in files if Path(f).suffix in {".c", ".h"}],
        "black": [f for f in files if Path(f).suffix == ".py"],
        "cmake-format": [
            f for f in files if Path(f).name == "CMakeLists.txt" or Path(f).suffix == ".cmake"
        ],
        "shfmt": [f for f in files if Path(f).suffix == ".sh"],
        "prettier": [
            f for f in files if Path(f).suffix in {".js", ".json", ".html", ".css", ".yml", ".yaml"}
        ],
    }
    # Prefer the pinned Python environment containing this interpreter.
    env = dict(os.environ)
    env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env.get("PATH", "")
    commands = {
        "clang-format": ["--dry-run", "--Werror"] if args.check else ["-i"],
        "black": ["--check"] if args.check else [],
        "cmake-format": ["--check"] if args.check else ["-i"],
        "shfmt": ["-i", "4", "-sr", "-d" if args.check else "-w"],
        "prettier": ["--check" if args.check else "--write"],
    }
    failed = False
    for tool, paths in groups.items():
        binary = (
            str(ROOT / "node_modules/.bin/prettier")
            if tool == "prettier"
            else shutil.which(tool, path=env["PATH"])
        )
        if not binary or not Path(binary).is_file():
            parser.error(
                f"{tool} is missing; install scripts/format-requirements.txt and run npm ci"
            )
        if paths:
            print(f"{tool}: {len(paths)} files", flush=True)
            result = subprocess.run([binary, *commands[tool], *paths], env=env)
            failed |= result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
