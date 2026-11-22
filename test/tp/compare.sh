#!/bin/sh
# compare.sh [attract|game] [frames] -- the board model against MAME 0.288,
# frame by frame: colour, video, work and sprite RAM, both CPUs' registers
# and the sound CPU's RAM at every VBLANK, and every 60th frame's picture.
#
#   attract  no inputs (default 3600 frames)
#   game     a coin, one player, then tp_host's bot plays (it turns to the
#            nearest enemy and fires) until its lives are gone, past the
#            first era (default 7800 frames); its inputs are recorded as a
#            script and MAME plays the same script
#
# MAME runs headless. Everything goes under $TMPDIR (TP_WORK). TP_ROMS is
# the timeplt.zip to use.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
name=${1:-attract}
case $name in
attract) frames=${2:-3600} ;;
game) frames=${2:-7800} ;;
*) echo "compare.sh: attract or game" >&2; exit 2 ;;
esac
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
set_dir=$("$here/setup.sh")
out="$work/compare-$name"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/tp_host" "$here/tp_host.c" "$src/tp.c" "$src/tpay.c" "$src/tpz80.c" -lm
if [ "$name" = game ]; then
    printf '300 in0 01\n306 in0 00\n360 in0 08\n366 in0 00\n' > "$out/start.txt"
    "$out/tp_host" "$set_dir" -frames "$frames" -script "$out/start.txt" -bot 420 -record "$out/script.txt" 2>/dev/null
else
    : > "$out/script.txt"
fi
"$out/tp_host" "$set_dir" -frames "$frames" -script "$out/script.txt" -dump "$out/model.dump" \
    -pix "$out/model.pix" -pixevery 60
"$here/mamerun.sh" "$out" "$out/script.txt" "$frames" 60
python3 -I "$here/eras.py" "$out/model.dump"
python3 -I "$here/cmpdump.py" "$out/mame.dump" "$out/model.dump" "$out/mame.pix" "$out/model.pix"
