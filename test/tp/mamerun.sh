#!/bin/sh
# mamerun.sh OUTDIR SCRIPT FRAMES [PIXEVERY] -- MAME's timeplt, headless,
# with tpdump.lua: OUTDIR/mame.dump (and mame.pix). TP_WAV set: MAME's
# sound to that file too (-wavwrite, 48 kHz). TP_ROMS is the set's zip.
#
# MAME runs headless, always: no window, no sound, as fast as it goes, and
# a time limit so it can't hang.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$1; script=$2; frames=$3; pixevery=${4:-0}
roms=${TP_ROMS:?TP_ROMS is the timeplt.zip to use}
mkdir -p "$out"
rp=$(mktemp -d "${TMPDIR:-/tmp}/tprp.XXXXXX")
cp "$roms" "$rp/timeplt.zip"
mkdir "$rp/cfg" "$rp/nvram"
pix=
[ "$pixevery" -gt 0 ] && pix="$out/mame.pix"
secs=$(( frames / 60 + 30 ))
TP_SCRIPT="$script" TP_DUMP="$out/mame.dump" TP_FRAMES="$frames" TP_PIX="$pix" TP_PIXEVERY="$pixevery" \
SDL_VIDEODRIVER=dummy mame timeplt -rompath "$rp" \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -frameskip 0 -skip_gameinfo -seconds_to_run "$secs" \
    -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$rp/snap" \
    -autoboot_delay 0 -autoboot_script "$here/tpdump.lua" ${TP_WAV:+-wavwrite "$TP_WAV"} >"$out/mame.log" 2>&1 || true
rm -rf "${rp:?}"
