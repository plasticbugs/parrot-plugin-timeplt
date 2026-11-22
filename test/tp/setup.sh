#!/bin/sh
# setup.sh -- the set unzipped under $TMPDIR for tp_host (prints the folder).
# TP_ROMS is the timeplt.zip to use.
set -e
roms=${TP_ROMS:?TP_ROMS is the timeplt.zip to use}
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
dir="$work/set"
mkdir -p "$dir"
unzip -o -q "$roms" -d "$dir"
echo "$dir"
