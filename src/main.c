// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// Time Pilot -- Konami's 1982 arcade game on the ColecoVision: the cart
// runs the arcade board (tp.c and tpay.c, MAME 0.288's emulation: the main
// Z80, the sound board's Z80 and its two AY-3-8910s with their filters)
// from the user's ROMs on the card, and tpcv.c sends the picture and the
// sound to the console as the frame kernel's lists. RESET goes back to
// the menu.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c)
//   core 0  this loop: a list at a time (tprun.c), the board run for the
//           list's time, the changes to the picture of its last VBLANK and
//           the list's samples, handed to core 1 as soon as it will take it
//
// The console runs the lists back to back (their sound is a sample
// stream), and each list runs the board for just the list's time (16.77
// ms), so the game keeps the arcade's speed. A list that isn't ready when
// the console wants it makes the console run the last one again (its sound
// once more) and puts the game a list behind. The note counts those.
//
// The ROMs (MAME's "timeplt" set) are read from the card's /TIMEPLT folder,
// loose or zipped, or from /TIMEPLT.ZIP, each known by its size and CRC-32.
//
// Controls (the kit's arcade keys): the stick, or a USB pad's d-pad or left
// stick, turns the plane (8 ways); either fire button or any face button
// fires; keypad * or #, or -, is a coin; keypad 1 or + starts one player,
// keypad 2 two.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#if __has_include("hardware/watchdog.h")
#include "hardware/watchdog.h"
#else                                   // (the kit's model of the console, test/host: no watchdog)
static inline void watchdog_enable(uint32_t ms, bool pause) { (void)ms; (void)pause; }
static inline void watchdog_update(void) {}
#endif

#include "cvdiag.h"
#include "cvframes.h"
#include "cvinput.h"
#include "cvsd.h"
#include "cvusb.h"
#include "gbkernel.h"
#include "handoff.h"
#include "vdp.h"
#include "tpay.h"
#include "tpcard.h"
#include "tpcv.h"
#include "tprun.h"

#define OPEN_US     20000           // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS 4000            // a hang reboots into the menu
#define NOTE_US     1000000         // the menu's note is brought up to date this often
#define GAME        "Time Pilot"    // in the menu's notes
#define TAG         "TP"

static tpcv_t enc;
static tprun_t run;
static tp_roms_t roms;
static uint8_t list[TPCV_LIST_MAX];
static size_t list_len;
static bool list_pending;                   // built, not published yet
static uint32_t seen_frames;
static uint8_t *scratch;                    // tpcv's room before the game: a message's canvas, the card's reads
static size_t scratch_n;

#define CANVAS      (*(canvas_t *)(void *)scratch)

// ---- the console ---------------------------------------------------------------------
static void tasks(void) {
    cvusb_task();
    cvf_task(time_us_32());
    watchdog_update();
}

static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

// TV frames the console has started since the last call (0: none yet, or
// the last list isn't out): for the messages' lists, a TV frame each
static uint32_t new_frames(void) {
    tasks();
    publish();
    if (list_pending) return 0;
    uint32_t f = cvf_frames(), n = f - seen_frames;
    seen_frames = f;
    return n;
}

static void send(void) {
    list_len = tpcv_tv_frame(&enc, list);
    list_pending = true;
    publish();
}

// ---- messages ------------------------------------------------------------------------
static void center(int y, const char *s, uint8_t color, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    canvas_text(&CANVAS, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, color, scale);
}

static void message(const char *head, const char *l1, const char *l2, const char *l3) {
    canvas_init(&CANVAS);
    center(24, "TIME", C_LYELLOW, 3);
    center(52, "PILOT", C_LYELLOW, 3);
    if (head) center(80, head, C_WHITE, 1);
    if (l1) center(100, l1, C_GRAY, 1);
    if (l2) center(112, l2, C_GRAY, 1);
    if (l3) center(124, l3, C_GRAY, 1);
    center(176, "RESET: BACK TO THE MENU", C_LBLUE, 1);
    tpcv_canvas(&enc, &CANVAS.px[0][0]);
}

// serve the console, a list a frame, for us microseconds (forever: 0)
static void serve_for(uint32_t us) {
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frames()) continue;
        tpcv_sound_pcm(&enc, NULL, 0);
        send();
    }
}

static void __attribute__((noreturn)) fail(const char *why, const char *l2, const char *l3, const char *menu_note) {
    message("IT CAN'T START", why, l2, l3);
    cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

// between a file's sectors: the console keeps its picture
static void card_tick(void) {
    if (!new_frames()) return;
    tpcv_sound_pcm(&enc, NULL, 0);
    send();
}

// ---- the controls ----------------------------------------------------------------------
// The kit's arcade keys (.claude/skills/parrot-cart-plugin/references/
// porting.md), as the board's ports have them (active low): IN0 0x01 coin
// 1, 0x08 1P start, 0x10 2P start; IN1 0x01 left, 0x02 right, 0x04 up,
// 0x08 down, 0x10 fire. A coin is a pulse of 6 lists from the press, and
// the key has to be up for 3 lists before the next one counts.
static void controls(void) {
    uint8_t joy, fire, lines;
    cvf_input(&joy, &fire, &lines);                     // low = pressed
    const int key = cvin_decode_key(lines);
    uint32_t dirs = 0, buttons = 0;
    gamepad_t p;
    if (cvusb_pad(0, &p)) {
        dirs = cvusb_pad_dirs(&p);
        buttons = p.buttons;
    }
    const bool up = !(joy & 1) || (dirs & GP_UP), right = !(joy & 2) || (dirs & GP_RIGHT);
    const bool down = !(joy & 4) || (dirs & GP_DOWN), left = !(joy & 8) || (dirs & GP_LEFT);
    const bool shoot = (fire & 3) != 3 || (buttons & (GP_SOUTH | GP_EAST | GP_WEST | GP_NORTH));
    const bool coin = key == CVIN_STAR || key == CVIN_HASH || (buttons & GP_MINUS);
    const bool start1 = key == 1 || (buttons & GP_PLUS);
    const bool start2 = key == 2;
    // One coin a press: the coin switch down for 6 frames (lists), then up
    // for 3 before the next coin, so two quick presses are two coins.
    static bool coin_held;
    static int coins_owed, coin_down, coin_up;
    if (coin && !coin_held && coins_owed < 2) coins_owed++;
    coin_held = coin;
    if (!coin_down && !coin_up && coins_owed) { coins_owed--; coin_down = 6; }
    const bool coin_switch = coin_down > 0;
    if (coin_down) { if (!--coin_down) coin_up = 3; }
    else if (coin_up) coin_up--;
    uint8_t in0 = 0, in1 = 0;
    if (coin_switch) in0 |= 0x01;
    if (start1) in0 |= 0x08;
    if (start2) in0 |= 0x10;
    if (left && !right) in1 |= 0x01;
    if (right && !left) in1 |= 0x02;
    if (up && !down) in1 |= 0x04;
    if (down && !up) in1 |= 0x08;
    if (shoot) in1 |= 0x10;
    tp_in.in0 = (uint8_t)~in0;
    tp_in.in1 = (uint8_t)~in1;
}

// ---- the game ------------------------------------------------------------------------
static uint32_t published, frames_at_start, us_max, pics_at_note;

// one list: the controls, the board for the list's time, the list made,
// then handed over as soon as core 1 takes it
static void game_list(void) {
    uint32_t t0 = time_us_32();
    controls();
    list_len = tprun_list(&run, list);
    uint32_t us = time_us_32() - t0;
    if (us > us_max) us_max = us;
    while (!cvf_publish(list, list_len)) tasks();
    published++;
    tasks();
}

// the menu's bottom line, should RESET come: pictures a second, lists the
// console ran again for want of a new one (since the start), the slowest
// list this second, the bus's odd selects
static void update_note(void) {
    static uint32_t t_last;
    uint32_t t = time_us_32();
    if (t - t_last < NOTE_US) return;
    int32_t again = (int32_t)(cvf_frames() - frames_at_start) - (int32_t)(published - 1);
    if (again < 0) again = 0;
    unsigned per_s = MIN(run.pictures - pics_at_note, 99u), ms = MIN(us_max / 1000, 99u), tenths = us_max / 100 % 10;
    uint32_t unsel, early, unhdr;
    cvf_bus_counts(&unsel, &early, &unhdr);
    char n[48];
    snprintf(n, sizeof n, TAG " %u/s %ldL %u.%ums s%u h%u", per_s, (long)MIN(again, 9999), ms, tenths,
             (unsigned)MIN(unsel, 999u), (unsigned)MIN(unhdr, 999u));
    cvf_note(n);
    t_last = t;
    pics_at_note = run.pictures;
    us_max = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    cvd_stage(1);
    cvusb_start();                          // after the kernel's start: a host, for what is plugged into the hub
    tpcv_init(&enc);
    scratch = tpcv_scratch(&scratch_n);
    message("LOOKING FOR THE ROMS", "IN " TPCARD_HOME " ON THE CARD", NULL, NULL);
    serve_for(OPEN_US);

    if (!cvsd_card_in() || !cvsd_init()) fail(cvsd_card_in() ? "THE CARD DIDN'T ANSWER" : "NO CARD IN THE SLOT", NULL, NULL, GAME ": no card");
    // the card's state, its reads and the graphics' ROMs in the scratch too
    // (no message is drawn until they are done with: fail() draws over
    // them, and tp_init decodes the graphics before the first picture)
    fs_t *fs = (fs_t *)(void *)scratch;
    size_t fs_room = (sizeof(fs_t) + 7) & ~(size_t)7;
    tp_rom_gfx = scratch + scratch_n - TP_GFX_BYTES;
    if (fs_mount(fs, cvsd_read) != FS_OK) fail("NO FAT OR EXFAT ON THE CARD", NULL, NULL, GAME ": card didn't mount");
    cvd_stage(2);
    watchdog_enable(WATCHDOG_MS, true);
    uint32_t found = tpcard_load_roms(fs, scratch + fs_room, (uint32_t)(scratch_n - fs_room - TP_GFX_BYTES), card_tick);
    cvd_stage(3);
    if (found != TPROMS_ALL) {
        int missing = 0;
        const char *first = "";
        for (int i = 0; i < TP_ROM_FILES; i++)
            if (!(found & 1u << i) && !missing++) first = tp_rom_files[i].name;
        char l2[40], note[48];
        if (!found) snprintf(l2, sizeof l2, "NONE OF ITS %d FILES IS THERE", TP_ROM_FILES);
        else snprintf(l2, sizeof l2, "%d OF %d MISSING: %.14s", missing, TP_ROM_FILES, first);
        for (char *c = l2; *c; c++)
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        snprintf(note, sizeof note, GAME ": %d ROMs missing", missing);   // (32 characters at most)
        fail("PUT MAME'S TIMEPLT ROMS, OR", "TIMEPLT.ZIP, IN " TPCARD_HOME, l2, note);
    }
    tproms_roms(&roms);
    tp_in.dsw0 = TP_DSW0;                   // MAME's defaults
    tp_in.dsw1 = TP_DSW1;
    tpay_reset();
    tp_init(&roms);                         // (decodes the graphics from the scratch, which is free after)
    tpcv_prepare();
    tprun_start(&run, &enc);
    cvd_stage(4);
    cvf_note(GAME);

    while (list_pending) tasks(), publish();          // the last message's list out first
    frames_at_start = cvf_frames();
    for (;;) {
        game_list();
        update_note();
    }
}
