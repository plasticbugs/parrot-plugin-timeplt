// tpcard.h -- the ROMs on the card, through fsro: loose or zipped in
// /TIMEPLT (MAME's timeplt.zip, as it is), or /TIMEPLT.ZIP. Portable: the
// plugin and the Mac test (test/tp/card.sh, on disk images) share it.
// (Donkey Kong's dkcard.h.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cvsd.h"                       // (fs_t: the SDK's sdplayer/fsro.h)
#include "tproms.h"

#define TPCARD_HOME "/TIMEPLT"
#define TPCARD_ZIP  "/TIMEPLT.ZIP"

// Every file in TPCARD_HOME, then TPCARD_ZIP, until all are found: each
// ROM to its place (tproms.h). A zip is never read whole: its directory,
// then each wanted entry's packed bytes into buf and unpacked after them.
// buf: max bytes (the directory, and a packed ROM with the ROM after it,
// must fit); tick: called between sectors (the plugin serves the
// console), or NULL. Returns tproms' found bits.
uint32_t tpcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void));
