#!/bin/sh
# tools/screenshot.sh OUT.png [PLUGIN_DIR] [MS] [SCRIPT]
#
# A registry screenshot of a plugin without the console: its own code run on
# the Mac (test/host/host.c) for MS milliseconds (default 2000) with SCRIPT's
# inputs (host.c's PC_SCRIPT: "500:key=1 900:joy=up+fire ..."), and the
# console's picture then, exactly 256x192.
#
#   tools/screenshot.sh screenshots/screenshot-1.png
#   tools/screenshot.sh screenshots/screenshot-2.png src 3000 "500:key=5"
#   PC_CARD=card.img PC_OPEN=/NOTES/a.txt tools/screenshot.sh shot.png examples/open-file
#
# With MAME=1 the picture comes from the SDK's real Z80 kernel in headless
# MAME instead (tools/mame-replay.sh), played the lists the plugin sent.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
png=${1:?usage: tools/screenshot.sh OUT.png [PLUGIN_DIR] [MS] [SCRIPT]}
dir=${2:-$kit/src}
work=$(mktemp -d "${TMPDIR:-/tmp}/parrotcart-shot.XXXXXX")
"$kit/test/host/build.sh" "$dir" "$work/run" 2>"$work/build.log" || { cat "$work/build.log" >&2; exit 1; }
PC_OUT=$work PC_END=${3:-2000} PC_SCRIPT=${4:-} "$work/run"
if [ -n "$MAME" ]; then
    "$kit/tools/mame-replay.sh" "$work/lists.bin" "$work" 120
    cp "$work/mame-256x192.png" "$png"
else
    cp "$work/screen.png" "$png"
fi
rm -rf "$work"
python3 -I "$kit/tools/png256.py" check "$png"
