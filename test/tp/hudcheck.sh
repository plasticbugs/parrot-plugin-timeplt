#!/bin/sh
# hudcheck.sh -- test/tp/hudcheck.c: every piece of the margins checked for
# strokes and gaps lost on the way to the console. TP_ROMS is the
# timeplt.zip; everything goes under $TMPDIR (TP_WORK).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
kit=$(cd "$here/../.." && pwd)
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
set_dir=$("$here/setup.sh")
mkdir -p "$work"
cc -O2 -Wall -Wno-unused-function -I"$src" -I"$kit/sdk/firmware/revc/sdk" -o "$work/hudcheck" "$here/hudcheck.c" \
    "$src/tp.c" "$src/tpay.c" "$src/tpz80.c" "$src/tpsnd.c" "$kit/sdk/firmware/revc/sdk/cvscale.c" -lm
"$work/hudcheck" "$set_dir"
