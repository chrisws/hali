#!/usr/bin/env bash
# Launch hali under GDB with output suppressed for notcurses TUI.
# Usage: ./gdb-hali.sh [extra gdb args...]

set -euo pipefail

BIN="${BIN:-./build/bin/hali}"

exec gdb \
  -q \
  -nx \
  -ex "set pagination off" \
  -ex "set confirm off" \
  -ex "set width 0" \
  -ex "set height 0" \
  -ex "set print thread-events off" \
  -ex "set print frame-arguments none" \
  -ex "set print object off" \
  -ex "set print static off" \
  -ex "set print vtbl off" \
  -ex "set print address off" \
  -ex "set print elements 0" \
  -ex "set print sevenbit-strings off" \
  -ex "set print thread-events off" \
  "$@" \
  "$BIN"
