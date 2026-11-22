#!/bin/sh
# lua/test/run.sh [NAME ...] -- the Lua runtime on the console's model, as
# test/run.sh runs the kit's examples: built for this computer against
# test/host/host.c, run with a script of inputs, and its log, its picture
# and (with MAME) the real kernel's picture checked.
#
# Most tests run a script as a Lua app, built into the plugin the way
# parrotcart_lua() builds one in: no card needed. The card tests (the file
# list, a script opened from the menu, files and saves) need macOS's
# hdiutil for their disk image, and are skipped elsewhere; sim/test.sh
# runs the same scripts from a folder on every system.
#
#   lua/test/run.sh                 everything
#   lua/test/run.sh api snake       just those
#   NO_MAME=1 lua/test/run.sh       without the MAME step
#
# The names: heap api hello snake sprites colours sound input midi error stuck memory
# nothing syntax nocard card. Output: $TMPDIR/parrotcart-test/lua-*.
set -e
here=$(cd "$(dirname "$0")" && pwd)
kit=$(cd "$here/../.." && pwd)
lua=$kit/lua
out=${TMPDIR:-/tmp}/parrotcart-test
mkdir -p "$out"
status=0
export LUA_SEED=1               # math.random the same every run
have_mame=1
[ -n "$NO_MAME" ] && have_mame=0
command -v mame >/dev/null 2>&1 && command -v sjasmplus >/dev/null 2>&1 || have_mame=0
[ $have_mame = 1 ] || echo "(no MAME step: NO_MAME is set, or mame or sjasmplus isn't installed)"
ONLY="$*"
wanted() { [ -z "$ONLY" ] && return 0; case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }
fail() { echo "  FAIL: $*"; status=1; }

# app NAME MAIN.lua [FILE ...] -- a plugin folder for test/host/build.sh:
# the runtime's own files, with the scripts built in (tools/lua-embed.py)
app() {
    name=$1; shift
    dir=$out/lua-$name-src
    rm -rf "$dir"; mkdir -p "$dir"
    python3 -I "$kit/tools/lua-embed.py" "$dir/app.h" "$(dirname "$1")" "$@"
    for f in api card heap port lua_vm lua_libs sdk_cvfont sdk_cvedit; do echo "#include \"$lua/$f.c\"" > "$dir/$f.c"; done
    printf '#define CVLUA_APP_HEADER "%s/app.h"\n#include "%s/main.c"\n' "$dir" "$lua" > "$dir/main.c"
    cp "$lua/CMakeLists.txt" "$dir/CMakeLists.txt"      # (its cvcart_features line: the USB switches)
}

# run NAME DIR [PATTERN ...] -- with PC_* in the environment; the log must
# match each pattern (grep -E)
run() {
    name=$1; dir=$2; shift 2
    o=$out/lua-$name
    rm -rf "$o"; mkdir -p "$o"
    printf '%s\n' "$name"
    if ! "$kit/test/host/build.sh" "$dir" "$o/run" 2>"$o/build.log"; then
        echo "  the build FAILED:"; { grep -E 'error|undefined' "$o/build.log" || head -20 "$o/build.log"; } | sed 's/^/    /' | head -20; status=1
        unset PC_SCRIPT PC_OPEN PC_CARD PC_NO_PSRAM PC_END PC_NO_LAUNCH; return 0
    fi
    if grep -v '^ld: warning' "$o/build.log" | grep -q 'warning:'; then fail "warnings:"; grep 'warning:' "$o/build.log" | sed 's/^/    /' | head; fi
    code=0
    PC_OUT=$o "$o/run" > "$o/out.txt" 2>&1 || code=$?
    grep -E '^ +[0-9]+ ms|^FAIL' "$o/out.txt" || true
    # (the models' rules: the test says when it means to break one)
    [ $code = 0 ] || [ -n "$MAY_FAIL" ] || { fail "the run ended with status $code"; tail -5 "$o/out.txt" | sed 's/^/    /'; }
    unset PC_SCRIPT PC_OPEN PC_CARD PC_NO_PSRAM PC_END PC_NO_LAUNCH MAY_FAIL
    for want in "$@"; do
        grep -Eq -- "$want" "$o/log.txt" || fail "nothing in $o/log.txt matches: $want"
    done
    python3 -I "$kit/tools/png256.py" check "$o/screen.png" >/dev/null || fail "screen.png isn't a 256x192 screenshot"
    if [ $have_mame = 1 ]; then
        if "$kit/tools/mame-replay.sh" "$o/lists.bin" "$o" 120 >"$o/mame.log" 2>&1 &&
           python3 -I "$kit/tools/png256.py" same "$o/screen.png" "$o/mame-256x192.png" >>"$o/mame.log" 2>&1; then
            echo "  MAME: the real kernel shows the same picture ($(grep -o '[0-9]* of [0-9]* lists run' "$o/mame.log"))"
        else
            echo "  FAIL in MAME:"; tail -3 "$o/mame.log" | sed 's/^/    /'; status=1
        fi
    fi
}
absent() { grep -Eq -- "$1" "$o/log.txt" && fail "$o/log.txt has: $1"; return 0; }

s=$here/scripts

# ---- the allocator by itself: random allocations, checked byte for byte ----
if wanted heap; then
    echo heap
    mkdir -p "$out/lua-heap"
    if cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I"$lua" "$here/heap_test.c" "$lua/heap.c" -o "$out/lua-heap/run" 2>"$out/lua-heap/build.log"; then
        "$out/lua-heap/run" | sed 's/^/  /' || fail "the allocator"
    else fail "the build:"; head -20 "$out/lua-heap/build.log"; fi
fi

# ---- scripts built in ----
if wanted api; then
    app api "$s/api.lua" "$s/helper.lua" "$s/data.txt"
    PC_END=1500 run api "$dir" 'note "api [0-9]+ passed"'
    absent 'check failed'
fi
if wanted hello; then
    app hello "$lua/examples/hello.lua"
    PC_END=4000 PC_SCRIPT="1000:joy=left 1100:joy= 2000:snap=moving" run hello "$dir" 'sound [0-9]+Hz@5 - - noise@15'
fi
if wanted snake; then
    app snake "$lua/examples/snake.lua"
    PC_END=9000 PC_SCRIPT="1000:joy=fire 1100:joy= 1500:joy=down 1600:joy= 2500:joy=left 2600:joy= 3000:snap=play 3200:joy=up 3300:joy=" \
        run snake "$dir" 'note "Snake: [0-9]+ points"' 'noise control E6 \(white noise, rate 2\)' 'sound - - - noise@3'
fi
if wanted sprites; then
    app sprites "$lua/examples/sprites.lua"
    PC_END=4000 PC_SCRIPT="2000:snap=moving" run sprites "$dir" 'sound [0-9]+Hz@7 - - noise@15'
    # a new picture nearly every TV frame: sprites cost a short list each
    n=$(grep -o '[0-9]* lists' "$o/out.txt" | head -1 | cut -d' ' -f1); [ "${n:-0}" -ge 150 ] || fail "only ${n:-0} pictures in 4 seconds"
fi
if wanted colours; then
    app colours "$lua/examples/colours.lua"
    PC_END=6000 run colours "$dir"
fi
if wanted sound; then
    app sound "$s/sound.lua"
    PC_END=2500 run sound "$dir" 'sound 440Hz@0 - - noise@15' 'sound - 26[12]Hz@2 - noise@15' \
        'sound - - 440Hz@5 noise@3' 'noise control E5 \(white noise, rate 1\)' 'noise control E3 \(periodic noise, rate 3\)' \
        'sound - - 440Hz@5 noise@0' 'sound 109Hz@0 - - noise@15' 'note "sound done"'
    # C4 stops by itself after its 6 updates, and the hiss after 3
    n=$(grep -c 'sound - - - noise@15' "$o/log.txt"); [ "$n" -ge 3 ] || fail "the timed notes didn't stop ($n silences)"
fi
if wanted input; then
    app input "$s/input.lua"
    PC_END=4000 PC_SCRIPT="300:joy=up+fire 500:joy= 600:joy=fire2 700:joy= 800:key=5 900:key= 1000:key=* 1100:key= \
1200:pad=south+l 1250:lstick=1500,-900 1400:pad= 1500:pad2=east 1600:pad2= 1700:keydown=04 1750:keyup=04 1800:mods=02 1820:keydown=05 1850:keyup=05 1870:mods=0 \
1900:keydown=28 1950:keyup=28 2000:keydown=52 2050:keyup=52 2200:mouse=10,-5,1,2 2400:midiin=90,3C,64" \
        run input "$dir" 'note "usb host"' 'note "pressed up held up fire "' 'note "pressed fire2 held fire2 "' \
        'note "keypad 5 5"' 'note "keypad \* \*"' 'note "pad1 SWITCH PRO south true l tru' 'note "pad2 east"' \
        'note "key a char a down true"' 'note "up a down false"' 'note "key b char B down true"' 'note "key return char' \
        'note "key up char nil down true"' 'note "mouse 138,91 d 10,-5 left true w' 'note "midi 144 60 100"' \
        'midi 90 3C 64 \(to the MIDI device\)'
fi
if wanted midi; then
    app midi "$s/midi.lua"
    PC_END=2500 PC_SCRIPT="1000:midiin=90,40,50 1500:midiin=F8" run midi "$dir" 'note "usb host midi' \
        'note "midi 144 64 80"' 'midi 90 41 50 \(to the MIDI device\)' 'note "midi 248 nil nil"'
fi

# ---- scripts that go wrong ----
if wanted error; then
    app error "$s/error.lua"
    PC_END=3000 PC_SCRIPT="1500:snap=stopped 2000:joy=fire 2100:joy=" run error "$dir" 'note "error.lua:2: attempt to index a "'
    # fire runs it again, and it stops again
    n=$(grep -c 'note "error.lua:2:' "$o/log.txt"); [ "$n" = 2 ] || fail "fire didn't run the script again (stopped $n times)"
fi
if wanted stuck; then
    app stuck "$s/stuck.lua"
    PC_END=8000 run stuck "$dir" 'note "stuck.lua:[67]: stopped after 5 se' 'RESET: back to the menu'
    # (5 seconds after the tenth update, not sooner or much later)
    at=$(grep 'note "stuck.lua' "$o/log.txt" | awk '{print $1}'); [ "${at:-0}" -ge 5000 ] && [ "${at:-0}" -le 6500 ] || fail "stopped at ${at:-?} ms"
fi
if wanted memory; then
    app memory "$s/memory.lua"
    PC_END=20000 run memory "$dir" 'note "free over 4 MB"' 'note "not enough memory"'
    PC_NO_PSRAM=1 PC_END=6000 run memory-no-psram "$dir" 'note "free under 4 MB"' 'note "not enough memory"'
fi
if wanted nothing; then
    app nothing "$s/nothing.lua"
    PC_END=1500 run nothing "$dir" 'note "nothing.lua has no update\(\) or '
fi
if wanted syntax; then
    app syntax "$s/syntax.lua"
    PC_END=1500 run syntax "$dir" 'note "syntax.lua:3: unexpected symbol'
fi

# ---- the Lua plugin as it ships: the editor, and scripts from the card ----
if wanted nocard; then
    PC_END=1500 run nocard "$lua" 'RESET: back to the menu'
fi
if wanted card; then
    if command -v hdiutil >/dev/null 2>&1; then
        for spec in "MS-DOS FAT32:MBRSPUD:fat32" "ExFAT:GPTSPUD:exfat"; do
            fsname=${spec%%:*}; rest=${spec#*:}; layout=${rest%%:*}; cname=${rest#*:}
            tree=$out/lua-tree
            rm -rf "$tree" "$out/lua-card-$cname.cdr"
            mkdir -p "$tree/LUA/SUB" "$tree/OTHER"
            cp "$lua/examples/hello.lua" "$lua/examples/snake.lua" "$s/files.lua" "$s/error.lua" "$tree/LUA/"
            printf 'return {answer = 42}\n' > "$tree/LUA/lib.lua"
            printf 'one\ntwo\nthree\n' > "$tree/LUA/notes.txt"
            printf 'deep\n' > "$tree/LUA/SUB/deep.txt"
            hdiutil create -quiet -srcfolder "$tree" -fs "$fsname" -layout "$layout" -size 64m -format UDTO -o "$out/lua-card-$cname"
            img=$out/lua-card-$cname.cdr
            # started by itself: the editor's list of /LUA (error, files, hello, lib, snake). Down four
            # times to snake.lua, fire opens it, keypad 4 runs it, fire starts the game, * held stops it
            PC_CARD=$img PC_END=9000 PC_SCRIPT="1500:snap=list 1600:joy=down 1700:joy= 1800:joy=down 1900:joy= 2000:joy=down 2100:joy= 2200:joy=down 2300:joy= 2500:joy=fire 2600:joy= 3200:snap=editor 3300:key=4 3400:key= 4000:joy=fire 4100:joy= 5000:snap=game 5200:key=* 7600:key= 8000:snap=stopped" \
                run "card-$cname-list" "$lua" 'noise control|sound - - - noise@15'
            cmp -s "$o/game.png" "$o/editor.png" && fail "keypad 4 didn't run the script"
            cmp -s "$o/game.png" "$o/stopped.png" && fail "holding * didn't stop the script"
            # opened from the menu, run with keypad 4, three times: the save counts the runs
            for n in 1 2 3; do
                PC_CARD=$img PC_OPEN=/LUA/files.lua PC_END=4000 PC_SCRIPT="1500:key=4 1600:key=" run "card-$cname-files-$n" "$lua" "note \"files run $n\""
            done
            # a script with a mistake: back in the editor, on its line. Then typed into from a USB
            # keyboard (a line "x" at the top), Ctrl+S: the file on the card is two bytes longer
            PC_CARD=$img PC_OPEN=/LUA/error.lua PC_END=6000 PC_SCRIPT="1500:key=4 1600:key= 3500:snap=mistake 3600:mods=01 3620:keydown=4A 3640:keyup=4A 3660:mods=0 3700:keydown=1B 3720:keyup=1B 3740:keydown=28 3760:keyup=28 3800:mods=01 3820:keydown=16 3840:keyup=16 3860:mods=0 5000:snap=saved" \
                run "card-$cname-error" "$lua" 'note "error.lua:2: attempt to index a "'
            # macOS's opinion of the image afterwards, and of the files the scripts and the editor made
            dev=$(hdiutil attach -nomount -readonly "$img" | awk '$1 ~ /s[0-9]+$/ {d = $1} END {print d}')
            if [ "$fsname" = ExFAT ]; then fsck_exfat -n "$dev" >"$out/lua-fsck-$cname.log" 2>&1 || fail "fsck_exfat"
            else fsck_msdos -n "$dev" >"$out/lua-fsck-$cname.log" 2>&1 || fail "fsck_msdos"; fi
            hdiutil detach -quiet "${dev%s*}" 2>/dev/null || { sleep 1; hdiutil detach -quiet -force "${dev%s*}"; }
            mnt=$out/lua-mnt-$cname
            mkdir -p "$mnt"
            hdiutil attach -quiet -readonly -nobrowse -mountpoint "$mnt" "$img"
            got=$(python3 -I "$here/cardcheck.py" "$mnt/LUA" "$s/error.lua" 2>&1) || true
            hdiutil detach -quiet "$mnt" 2>/dev/null || { sleep 1; hdiutil detach -quiet -force "$mnt"; }
            if [ "$got" = ok ]; then echo "  macOS: fsck clean, and the files are as the scripts and the editor wrote them"
            else fail "the files on the Mac: $got"; fi
        done
    else
        echo "card"; echo "  (skipped: the card image needs hdiutil, which is macOS. sim/test.sh runs these from a folder)"
    fi
fi

[ $status = 0 ] && echo "all Lua checks passed (pictures: $out/lua-*/screen.png)"
exit $status
