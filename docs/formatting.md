# Code style and formatting

Project-owned source is formatted automatically. C uses the [LLVM clang-format base](https://clang.llvm.org/docs/ClangFormatStyleOptions.html), with four-space indentation, a 100-column limit, Linux-style function braces, and expanded control flow. This borrows the function-brace convention from [Linux kernel style](https://www.kernel.org/doc/html/latest/process/coding-style.html); it does not claim to follow every kernel rule. Include ordering is preserved because platform headers can depend on it.

| Files | Formatter | Configuration |
| --- | --- | --- |
| C and headers | clang-format 18.1.8 | `.clang-format` |
| Python | Black 25.1.0 | `pyproject.toml` |
| JavaScript, HTML, CSS, JSON, YAML | Prettier 3.6.2 | `.prettierrc.json` |
| Shell scripts | shfmt 3.11.0 | Four spaces, spaced redirects |
| CMake | cmake-format 0.6.13 | `.cmake-format.json` |

`.editorconfig` supplies editor defaults. Formatter versions are pinned in `scripts/format-requirements.txt` and `package-lock.json`. Use Python 3.9 or later and Node.js 20 or later:

```sh
python3 -m venv build/format-venv
. build/format-venv/bin/activate
python3 -m pip install -r scripts/format-requirements.txt
npm ci
npm run format
npm run format:check
```

The equivalent direct commands are `python3 scripts/format.py` and `python3 scripts/format.py --check`. CI runs the check without changing files. It includes tracked source and new, non-ignored files, so new files are checked even before staging.

SDKs, vendored upstream code, license texts, generated files and build artifacts are excluded. Keep their original formatting. Generated headers and embedded payloads should be regenerated through the build scripts, not edited by hand.

Formatting `runner/` changes the source hashes recorded in `assets/runner.json`. Rebuild it with `scripts/build_runner.sh` using the QNX headers and Arm toolchain described in [building.md](building.md); do not just replace hashes without rebuilding. The normal firmware build verifies this provenance before embedding the runner.

Keep formatting changes separate from behavioral changes where practical. Automated formatting handles layout; clear names, useful comments and understandable control flow still need review.
