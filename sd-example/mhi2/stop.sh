#!/bin/sh
set -eu
test -n "$MHI2_RUN_DIR"
rm -f "$MHI2_RUN_DIR/example-active"
echo "RAM-only demonstration stopped"
