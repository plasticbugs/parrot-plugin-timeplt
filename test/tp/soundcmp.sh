#!/bin/zsh
# soundcmp.sh [frames] -- the sound against MAME's, and how loud it is.
#
# A game (a coin, one player, tp_host's bot, its inputs recorded as a
# script) in MAME's timeplt (headless, -wavwrite), in tp_host (tpay.c, a
# tick at a time at 48 kHz) and in cart_host three ways (tpsnd.h): the
# plugin's samples through the DAC (pcm), the AYs' loudest three tones as
# the SN76489's (tones), the loudest unfiltered tone and the rest as
# samples (mix). Each of cart_host's goes as lists through the SDK's real
# kernel on MAME's ColecoVision (tools/mame-replay.sh with MAME_WAV), as
# the console would play it; pcm also as the DAC's levels straight from
# the lists (no MAME). Each against MAME's timeplt (sndcmp.c), and each
# replay's loudness (tools/wavlevel.py) against an ordinary cartridge's,
# about -22 dBFS rms (docs/testing.md). Everything goes under $TMPDIR
# (TP_WORK). TP_ROMS is the timeplt.zip; MAME_ROMPATH a folder with the
# ColecoVision BIOS (coleco.zip), if MAME's own rompath has none.
set -e
here=${0:A:h}
src="$here/../../src"
kit="$here/../.."
frames=${1:-3600}
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
set_dir=$("$here/setup.sh")
out="$work/sound"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/tp_host" "$here/tp_host.c" "$src/tp.c" "$src/tpay.c" "$src/tpz80.c" -lm
cc -O2 -Wall -Wno-unused-function -I"$src" -I"$kit/sdk/firmware/revc/sdk" -o "$out/cart_host" "$here/cart_host.c" \
    "$src/tp.c" "$src/tpay.c" "$src/tpz80.c" "$src/tpsnd.c" "$src/tpcv.c" "$src/tprun.c" "$kit/sdk/firmware/revc/sdk/cvscale.c" -lm
cc -O2 -Wall -o "$out/sndcmp" "$here/sndcmp.c" -lm
printf '300 in0 01\n306 in0 00\n360 in0 08\n366 in0 00\n' > "$out/start.txt"
"$out/tp_host" "$set_dir" -frames "$frames" -script "$out/start.txt" -bot 420 -record "$out/script.txt" 2>/dev/null
"$out/tp_host" "$set_dir" -frames "$frames" -script "$out/script.txt" -wav "$out/model.wav" 2>/dev/null
TP_WAV="$out/mame.wav" "$here/mamerun.sh" "$out/mame" "$out/script.txt" "$frames"
lists=$(( frames * 1000 / 1011 ))       # (a list is 16.77 ms, a frame 16.50)
secs=$(( frames / 60 + 20 ))
for m in pcm tones mix; do
    extra=(); [[ $m == pcm ]] && extra=(-wav "$out/levels.wav")
    "$out/cart_host" "$set_dir" -lists "$lists" -fscript "$out/script.txt" -sound $m -out "$out/$m.bin" $extra > "$out/$m.log"
    MAME_WAV="$out/$m-raw.wav" "$kit/tools/mame-replay.sh" "$out/$m.bin" "$out/replay-$m" "$secs" > /dev/null
    # the replay starts later and runs 0.5% fast (wavalign.py): stretched
    # back and lined up by the envelopes (sndcmp finds the rest, within 50 ms)
    python3 -I "$here/wavalign.py" "$out/mame.wav" "$out/$m-raw.wav" "$out/$m.wav" 60036/59736
done
echo "the model (tpay.c at 48 kHz), sample for sample:"; "$out/sndcmp" -raw "$out/mame.wav" "$out/model.wav"
echo "the model, at the cart's rate:"; "$out/sndcmp" "$out/mame.wav" "$out/model.wav"
echo "pcm, the DAC's levels from the lists:"; "$out/sndcmp" "$out/mame.wav" "$out/levels.wav"
for m in pcm tones mix; do echo "$m, the lists on MAME's ColecoVision:"; "$out/sndcmp" "$out/mame.wav" "$out/$m.wav"; done
echo "how loud (the replays; an ordinary cartridge is about -22 dBFS rms):"
python3 -I "$kit/tools/wavlevel.py" "$out/pcm-raw.wav" "$out/tones-raw.wav" "$out/mix-raw.wav"
