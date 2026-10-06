#!/bin/sh
set -eu
test -n "$MHI2_RUN_DIR"
echo "SD runner started on $(uname -s) $(uname -m)"
echo "RAM-only demonstration; no AA or cluster hooks"
echo active > "$MHI2_RUN_DIR/example-active"
