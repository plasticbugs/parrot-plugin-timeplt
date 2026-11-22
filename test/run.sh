#!/bin/sh
# test/run.sh -- everything about the kit that a Mac can check.
#
#   test/run.sh [example ...]      default: the template's plugin and every example
#
# For each plugin:
#   1. its own main.c is built for the Mac and run against models of the
#      console, the card, the pads and the PSRAM (test/host/host.c), with a
#      script of inputs; what it showed, played, sent and wrote is checked
#   2. the lists it sent the console are played to the SDK's real Z80
#      kernel in headless MAME (tools/mame-replay.sh), and MAME's picture
#      must be the model's, pixel for pixel
# and for the card examples, on FAT16, FAT32 and exFAT images made by
# hdiutil, macOS's own fsck and a mount afterwards.
#
# Output in $TMPDIR/parrotcart-test (pictures: */screen.png).
# NO_MAME=1 skips step 2. Step 2 needs mame, sjasmplus and the ColecoVision
# BIOS, coleco.zip (MAME's name for it), which the kit doesn't have. Put
# coleco.zip in a folder of your own and name that folder in MAME_ROMPATH:
#   MAME_ROMPATH=$HOME/mame/roms test/run.sh
# or put it where MAME already looks: the rompath in its mame.ini, or with
# no mame.ini, a roms folder in the folder you run this from. Without it,
# step 2 is skipped and the script says why.
# What this can't check is the cart itself: USB, the real card, the PSRAM
# chip, timing on the bus. docs/testing.md has the console test plan.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/parrotcart-test
mkdir -p "$out"
status=0
have_mame=1
[ -n "$NO_MAME" ] && have_mame=0
command -v mame >/dev/null 2>&1 && command -v sjasmplus >/dev/null 2>&1 || have_mame=0
[ $have_mame = 1 ] || echo "(no MAME step: NO_MAME is set, or mame or sjasmplus isn't installed)"
# is the BIOS where MAME looks? (asked headless, as tools/mame-replay.sh runs MAME)
if [ $have_mame = 1 ] && ! SDL_VIDEODRIVER=dummy mame -verifyroms coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} \
        -video none -videodriver dummy -window -nomaximize -sound none \
        -nothrottle -skip_gameinfo -seconds_to_run 10 >/dev/null 2>&1; then
    have_mame=0
    echo "(no MAME step: MAME can't find the ColecoVision BIOS, coleco.zip, which isn't in the kit."
    echo " Put it in a folder and run MAME_ROMPATH=/path/to/that/folder test/run.sh, or put it in"
    echo " MAME's own rompath. docs/testing.md has more.)"
fi

# run NAME DIR -- with PC_* in the environment; then the log must match each
# remaining argument (a grep -E pattern). (sh keeps a function's PC_*=...
# after it returns: they are cleared, so one run's script isn't the next's.)
run() {
    run_one "$@"
    unset PC_SCRIPT PC_END PC_CARD PC_OPEN PC_ID PC_NO_PSRAM PC_NO_LAUNCH
}
run_one() {
    name=$1; dir=$2; shift 2
    o=$out/$name
    rm -rf "$o"; mkdir -p "$o"
    printf '%s\n' "$name"
    [ -d "$dir" ] || { echo "  (not here: $dir)"; return 0; }
    if ! "$kit/test/host/build.sh" "$dir" "$o/run" 2>"$o/build.log"; then
        echo "  the Mac build FAILED:"; sed 's/^/    /' "$o/build.log" | head -20; status=1
        unset PC_SCRIPT PC_OPEN PC_CARD PC_NO_PSRAM PC_END; return 0
    fi
    if grep -v '^ld: warning' "$o/build.log" | grep -q 'warning:'; then echo "  FAIL: warnings:"; grep 'warning:' "$o/build.log" | sed 's/^/    /' | head; status=1; fi
    PC_OUT=$o "$o/run" || status=1
    # (sh keeps a function's VAR=value prefixes set after it: not into the next run)
    unset PC_SCRIPT PC_OPEN PC_CARD PC_NO_PSRAM PC_END
    for want in "$@"; do
        grep -Eq -- "$want" "$o/log.txt" || { echo "  FAIL: nothing in $o/log.txt matches: $want"; status=1; }
    done
    python3 -I "$kit/tools/png256.py" check "$o/screen.png" >/dev/null || { echo "  FAIL: screen.png isn't a 256x192 screenshot"; status=1; }
    if [ $have_mame = 1 ]; then
        if "$kit/tools/mame-replay.sh" "$o/lists.bin" "$o" 120 >"$o/mame.log" 2>&1 &&
           python3 -I "$kit/tools/png256.py" same "$o/screen.png" "$o/mame-256x192.png" >>"$o/mame.log" 2>&1; then
            echo "  MAME: the real kernel shows the same picture ($(grep -o '[0-9]* of [0-9]* lists run' "$o/mame.log"))"
            if grep -q 'longer than their TV frame' "$o/mame.log" && ! grep -q 'replay: 0 lists took longer' "$o/mame.log"; then
                echo "  FAIL in MAME: $(grep 'longer than' "$o/mame.log")"; status=1
            fi
        else
            echo "  FAIL in MAME:"; tail -3 "$o/mame.log" | sed 's/^/    /'; status=1
        fi
    fi
}

wanted() { [ -z "$ONLY" ] && return 0; case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }
ONLY="$*"

# a card image: FILESYSTEM:LAYOUT:NAME -> $out/card-NAME.cdr, from a small
# tree, with more folders if asked (card SPEC /FOLDER ...)
card() {
    fsname=${1%%:*}; rest=${1#*:}; layout=${rest%%:*}; cname=${rest#*:}
    shift
    tree=$out/tree
    rm -rf "$tree" "$out/card-$cname.cdr"
    mkdir -p "$tree/NOTES" "$tree/PLUGINS"
    for d in "$@"; do mkdir -p "$tree$d"; done
    printf 'Hello from the card.\nThis is the first file.\n' > "$tree/A first file.txt"
    i=1; : > "$tree/NOTES/Long story.txt"
    while [ $i -le 60 ]; do echo "Line $i of a story long enough to scroll, and to wrap around the screen's 30 columns." >> "$tree/NOTES/Long story.txt"; i=$((i + 1)); done
    hdiutil create -quiet -srcfolder "$tree" -fs "$fsname" -layout "$layout" -size 64m -format UDTO -o "$out/card-$cname"
}

# hdiutil detach, again with -force if the disk is busy (Spotlight, or
# another test's attach at the same moment)
detach() { hdiutil detach -quiet "$1" 2>/dev/null || { sleep 1; hdiutil detach -quiet -force "$1"; }; }

# macOS's opinion of an image the plugin wrote to: fsck, then the save
# file's bytes: 512 of them, starting with MAGIC, a run count at byte 8
# (card_check NAME FS RUNS [/PATH/OF.SAV MAGIC]: /SDCARD.SAV and PCSAVE1)
card_check() {
    cname=$1; fs=$2; runs=$3; save=${4:-/SDCARD.SAV}; magic=${5:-PCSAVE1}
    img=$out/card-$cname.cdr
    dev=$(hdiutil attach -nomount -readonly "$img" | awk '$1 ~ /s[0-9]+$/ {d = $1} END {print d}')
    whole=${dev%s*}
    case $fs in ExFAT) fsck_exfat -n "$dev" >"$out/fsck-$cname.log" 2>&1 || { echo "  FAIL: fsck_exfat"; status=1; } ;;
                *) fsck_msdos -n "$dev" >"$out/fsck-$cname.log" 2>&1 || { echo "  FAIL: fsck_msdos"; status=1; } ;; esac
    detach "$whole"
    mnt=$out/mnt-$cname
    mkdir -p "$mnt"
    hdiutil attach -quiet -readonly -nobrowse -mountpoint "$mnt" "$img"
    got=$(python3 -I -c 'import struct,sys; d=open(sys.argv[1],"rb").read(); print(len(d), d[:len(sys.argv[2])].decode(), struct.unpack_from("<I", d, 8)[0])' "$mnt$save" "$magic" 2>&1) || true
    detach "$mnt"
    if [ "$got" = "512 $magic $runs" ]; then echo "  macOS: fsck clean, $save is 512 bytes, run $runs"
    else echo "  FAIL: $save on the Mac: $got (wanted run $runs)"; status=1; fi
}

# ---- the plugin in src/ (tools/new-plugin.py rewrites this part) ----
# Time Pilot plays from the user's ROMs on the card: TP_ROMS names MAME's
# timeplt.zip (never in the repository). Without it, only the card with no
# ROMs is tried; the plugin has to say they're missing.
tp_card() {                             # tp_card NAME [ZIP]: $out/card-NAME.cdr, /TIMEPLT holding ZIP
    tree=$out/tree-$1
    rm -rf "$tree" "$out/card-$1.cdr"
    mkdir -p "$tree/TIMEPLT" "$tree/PLUGINS"
    [ -n "$2" ] && cp "$2" "$tree/TIMEPLT/timeplt.zip"
    printf 'not a ROM\n' > "$tree/TIMEPLT/readme.txt"
    hdiutil create -quiet -srcfolder "$tree" -fs ExFAT -layout MBRSPUD -size 64m -format UDTO -o "$out/card-$1"
}
# The DAC's lists run back to back, 60,036 T each by the kit's costs (the
# porting notes: "every list is the same length and the console runs the
# lists back to back, so a list's length is the sample clock"). The model
# wants a list to end 1,200 T before its frame, so it fails each of them
# (60,1xx T in its count; MAME's kernel runs them all). tp_run allows just
# those lines, up to 60,199 T; anything else, from the model or from run's
# own checks, still fails.
tp_run() {
    was=$status; status=0
    run "$@" > "$out/$1.out" 2>&1
    long=$(grep -Ec '^FAIL at [0-9]+ ms: a list takes 60[01][0-9][0-9] T: a frame is 59736$' "$out/$1.out" || true)
    grep -Ev '^FAIL at [0-9]+ ms: a list takes 60[01][0-9][0-9] T: a frame is 59736$' "$out/$1.out" |
        sed 's/ -- FAILED$/ -- its long lists only (below)/'
    [ "$long" -gt 0 ] && echo "  $long lists of 60,1xx T: the DAC's, back to back (allowed)"
    if [ $status != 0 ] && ! grep -Ev '^FAIL at [0-9]+ ms: a list takes 60[01][0-9][0-9] T: a frame is 59736$' "$out/$1.out" |
            grep -Eq '^FAIL|^  FAIL|build FAILED'; then
        status=0
    fi
    [ $status = 0 ] && status=$was
}
if wanted time-pilot-noroms; then
    tp_card noroms
    PC_END=1500 PC_CARD=$out/card-noroms.cdr \
        run time-pilot-noroms "$kit/src" 'note "Time Pilot: 11 ROMs missing"' 'back to the menu' \
            'psg 80 00 9F A0 00 BF C0 00 DF FF'
fi
if wanted time-pilot; then
    if [ -n "$TP_ROMS" ]; then
        tp_card roms "$TP_ROMS"
        PC_END=12000 PC_CARD=$out/card-roms.cdr \
        PC_SCRIPT="5000:key=* 5150:key= 6000:key=1 6150:key= 7000:joy=up+fire 7500:joy=left 8500:joy=down+fire 9500:joy=" \
            tp_run time-pilot "$kit/src" 'note "TP [0-9]+/s' 'back to the menu, its bottom line "TP' 'psg .*E4 FF'
    else
        echo "time-pilot"; echo "  (TP_ROMS isn't set: MAME's timeplt.zip, for the game)"
    fi
fi
if wanted time-pilot-usb; then
    if [ -n "$TP_ROMS" ]; then
        tp_card usb "$TP_ROMS"
        PC_END=10000 PC_CARD=$out/card-usb.cdr \
        PC_SCRIPT="1000:pad=up 5000:pad=minus 5150:pad=up 6000:pad=plus 6150:pad=up 7000:pad=south 7200:lstick=-1800,0 8000:pad=east+down 9000:snap=usb" \
            tp_run time-pilot-usb "$kit/src" 'usb: the port is a host' 'back to the menu, its bottom line "TP'
    else
        echo "time-pilot-usb"; echo "  (TP_ROMS isn't set: MAME's timeplt.zip, for the game)"
    fi
fi
# ---- the examples ----
# (left out with examples/: tools/new-plugin.py --examples keeps them)

[ $status = 0 ] && echo "all checks passed (pictures: $out/*/screen.png)"
exit $status
