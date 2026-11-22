#!/bin/sh
# tools/mame-replay.sh LISTS.bin OUT_DIR [SECONDS]
#
# Plays the lists a plugin sent (test/host/host.c's lists.bin) to the SDK's
# real Z80 kernel on MAME's ColecoVision, and leaves the TV's picture in
# OUT_DIR/mame.png (as MAME snapshots it, borders and all) and, cropped to
# the console's 256x192, in OUT_DIR/mame-256x192.png.
#
# Needs mame, sjasmplus, python3 and the ColecoVision BIOS where MAME looks
# for it (MAME_ROMPATH=dir, holding coleco.zip).
#
# MAME_WAV=FILE.wav records the sound as well (MAME's -wavwrite, which works
# with -sound none), for tools/wavlevel.py to measure against a cartridge's.
#
# MAME runs headless, always: no window, no sound, as fast as it goes, and a
# time limit so it can't hang. Never run it another way from this kit.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
lists=${1:?usage: tools/mame-replay.sh LISTS.bin OUT_DIR [SECONDS]}
out=${2:?usage: tools/mame-replay.sh LISTS.bin OUT_DIR [SECONDS]}
mkdir -p "$out/mame"
out=$(cd "$out" && pwd)
lists=$(cd "$(dirname "$lists")" && pwd)/$(basename "$lists")
sdk=$kit/sdk

# the kernel the lists are for: its first record says ('K' the screen kernel, 'F' the frame kernel)
if [ "$(head -c 1 "$lists")" = F ]; then
    (cd "$out/mame" && sjasmplus --nologo "$sdk/gbcv/kernel/gbkernel.asm" >/dev/null 2>&1)
    cart=$out/mame/gbkernel.bin
else
    (cd "$out/mame" && sjasmplus --nologo "$sdk/firmware/revc/midihello/midihello.asm" >/dev/null 2>&1)
    cart=$out/mame/mhkernel.bin
fi
rm -f "$out/mame/snap.png"
# (if MAME stops, as it does without the BIOS, the check after it says why)
PC_LISTS=$lists SDL_VIDEODRIVER=dummy mame coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} -cart "$cart" \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -frameskip 0 -skip_gameinfo -seconds_to_run "${3:-60}" \
    -snapshot_directory "$out/mame" -snapname snap \
    -cfg_directory "$out/mame/cfg" -nvram_directory "$out/mame/nvram" ${MAME_WAV:+-wavwrite "$MAME_WAV"} \
    -autoboot_script "$kit/tools/mame/replay.lua" | grep '^replay:' || true
[ -f "$out/mame/snap.png" ] || { echo "mame-replay: MAME took no snapshot. Is the ColecoVision BIOS, coleco.zip, in the folder MAME_ROMPATH names, or in MAME's own rompath?" >&2; exit 1; }
cp "$out/mame/snap.png" "$out/mame.png"
python3 -I "$kit/tools/png256.py" crop "$out/mame.png" "$out/mame-256x192.png"
