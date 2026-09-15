#!/bin/sh
# Runs gravestone on a screen that has been checked first, so a broken
# WSLg never shows as a window that opens and cannot be seen.
set -e
line=$(sh tools/display.sh 2>&1 | grep '^DISPLAY=' || true)
sh tools/display.sh 2>&1 | grep -v '^DISPLAY=' >&2 || true
if [ -n "$line" ]; then
    exec env "$line" GS_LOG=debug ./build/gravestone
fi
exec env GS_LOG=debug ./build/gravestone
