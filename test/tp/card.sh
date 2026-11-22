#!/bin/zsh
# card.sh -- the plugin's card code (tpcard.c, with fsro) on disk images made
# here from the tests' copy of the set (setup.sh) and MAME's zip itself
# (TP_ROMS; everything under $TMPDIR, never commit any of it): the zip in
# /TIMEPLT as it is, the set loose in /TIMEPLT, the zip at the top as
# /TIMEPLT.ZIP, one ROM short, none at all; on exFAT and FAT32.
# (Donkey Kong's.)
set -e
here=${0:A:h}
src="$here/../../src"
kit="$here/../../sdk/firmware/revc"
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
roms=${TP_ROMS:?TP_ROMS is the timeplt.zip to use}
set_dir=$("$here/setup.sh")
out="$work/card"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -I"$src" -I"$kit/sdk" -I"$kit/sdplayer" -o "$out/card_host" \
    "$here/card_host.c" "$src/tpcard.c" "$src/tproms.c" "$src/swzip.c" "$src/tp.c" "$src/tpz80.c" "$src/tpay.c" \
    "$src/tpsnd.c" "$src/tpcv.c" "$src/tprun.c" "$kit/sdk/cvscale.c" "$kit/sdplayer/fsro.c" -lm 2>&1 | grep -v "ld: warning" || true
rc=0
image() {                               # image name fs: the tree $out/$name as $out/$name.cdr
    hdiutil create -quiet -srcfolder "$out/$1" -fs "$2" -layout MBRSPUD -size 64m -format UDTO -o "$out/$1"
}
mkdir -p "$out/zipped/TIMEPLT" "$out/loose/TIMEPLT" "$out/loose/NES" "$out/rootzip/PLUGINS" "$out/short/TIMEPLT" \
    "$out/fat/TIMEPLT" "$out/none/TIMEPLT"
cp "$roms" "$out/zipped/TIMEPLT/timeplt.zip"
cp "$set_dir"/* "$out/loose/TIMEPLT/"; print "not a ROM" > "$out/loose/TIMEPLT/readme.txt"
cp "$roms" "$out/rootzip/TIMEPLT.ZIP"
cp "$set_dir"/* "$out/short/TIMEPLT/"; rm "$out/short/TIMEPLT/tm4"
cp "$roms" "$out/fat/TIMEPLT/timeplt.zip"
print "not a ROM" > "$out/none/TIMEPLT/readme.txt"
image zipped ExFAT; image loose ExFAT; image rootzip ExFAT; image short ExFAT; image fat "MS-DOS FAT32"; image none ExFAT
for n in zipped loose rootzip fat; do
    print "== $n"; TP_SET="$set_dir" "$out/card_host" "$out/$n.cdr" | tee "$out/$n.log" | sed 's/^/  /'
    grep -q "(0 missing)" "$out/$n.log" && grep -q "byte for byte" "$out/$n.log" && grep -q "ran 10 s" "$out/$n.log" ||
        { print "  FAILED"; rc=1 }
done
print "== short (tm4 missing: it must say so)"
"$out/card_host" "$out/short.cdr" | tee "$out/short.log" | sed 's/^/  /' || true
grep -q "found 7DF (1 missing)" "$out/short.log" || { print "  FAILED"; rc=1 }
print "== none (no ROMs: it must say so)"
"$out/card_host" "$out/none.cdr" | tee "$out/none.log" | sed 's/^/  /' || true
grep -q "found 000 (11 missing)" "$out/none.log" || { print "  FAILED"; rc=1 }
[[ $rc == 0 ]] && print "card: all passed"
exit $rc
