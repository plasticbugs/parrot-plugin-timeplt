// test/usb: the SDK's USB features (cvusb.h), all of them on, each input
// written to the menu's note (cvk_note) as it comes, so that a test's
// log.txt has it; and each MIDI message that comes in, sent straight back.
//
// Not an example to copy (tools/new-plugin.py --features all makes one of
// those): a plugin for test/run.sh and sim/test.sh, whose scripts plug
// things in (test/host/host.c: keydown=, mouse=, pad=, midiin= ...) and
// look for these in the log:
//
//   note "usb HOST"                    the port's mode, when it changes
//   note "key 04 dn 61 m00"            a key: its usage, down or up, its
//                                      character (hex, 00 for none), the modifiers
//   note "mouse 40,20 w1 b1"           the mouse: moved since the last, the wheel, its buttons
//   note "pad1 SWITCH PRO 00000110"    a pad: what it is, its buttons (gamepad.h's GP_*)
//   note "pad1 at 1500,-900 -2048,2047"    its sticks: left x,y, then right x,y
//   note "midi 90 3C 64"               a MIDI message that came in (and went back out)
//   note "joy 11 key 5"                the console's controller: wired.h's WIRED_* held (hex), the keypad
//
// The TV shows the last of them.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvkernel.h"
#include "cvscreen.h"
#include "cvusb.h"
#include "handoff.h"
#include "mailbox.h"             // REPORT_KIND_*
#include "rom_image.h"
#include "wired.h"

static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];
static char last[48] = "NOTHING YET";

static void note(const char *s) {
    cvk_note(s);
    snprintf(last, sizeof last, "%s", s);
}

static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    cvs_line(c, 16, 16, 224, "USB TEST", C_LYELLOW, C_BLACK, 1);
    cvs_line(c, 16, 40, 224, cvusb_mode_name(cvusb_mode()), C_WHITE, C_BLACK, 1);
    cvs_line(c, 0, 64, 256, last, C_LGREEN, C_BLACK, 1);
}

int main(void) {
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    cvusb_start();
    cvusb_mode_t mode = CVUSB_NONE;
    gamepad_t pads[CVUSB_PAD_MAX];
    bool have[CVUSB_PAD_MAX] = {false, false};
    const char *kinds[CVUSB_PAD_MAX] = {NULL, NULL};
    uint8_t buttons = 0;
    wired_t wired;
    wired_init(&wired);
    uint8_t joy = 0;
    int key = -1;
    uint32_t t_screen = 0;
    char s[48];                         // (32 characters at most, as cvk_note takes)
    for (;;) {
        const uint32_t t = time_us_32();
        cvk_task(t);
        cvusb_task();
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits))
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK) cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
            else wired_report(&wired, kind, bits);
        if (wired.held != joy || wired.key != key) {
            joy = wired.held;
            key = wired.key;
            snprintf(s, sizeof s, "joy %02X key %d", joy, key);
            note(s);
        }

        if (cvusb_mode() != mode) {
            mode = cvusb_mode();
            snprintf(s, sizeof s, "usb %s", cvusb_mode_name(mode));
            note(s);
        }
        cvkey_t k;
        while (cvusb_key(&k)) {
            snprintf(s, sizeof s, "key %02X %s %02X m%02X", k.usage, k.down ? "dn" : "up", (uint8_t)k.ascii, k.mods);
            note(s);
        }
        cvmouse_t m;
        if (cvusb_mouse(&m) && (m.dx || m.dy || m.wheel || m.buttons != buttons)) {
            buttons = m.buttons;
            snprintf(s, sizeof s, "mouse %ld,%ld w%ld b%u", (long)m.dx, (long)m.dy, (long)m.wheel, m.buttons);
            note(s);
        }
        for (int n = 0; n < CVUSB_PAD_MAX; n++) {
            gamepad_t p;
            const bool in = cvusb_pad(n, &p);
            const char *what = cvusb_pad_kind(n);
            if (in != have[n] || what != kinds[n] || (in && p.buttons != pads[n].buttons)) {
                snprintf(s, sizeof s, "pad%d %s %08lX", n + 1, in ? what : "OFF", in ? (unsigned long)p.buttons : 0ul);
                note(s);
            }
            if (in && (!have[n] || p.lx != pads[n].lx || p.ly != pads[n].ly || p.rx != pads[n].rx || p.ry != pads[n].ry)) {
                snprintf(s, sizeof s, "pad%d at %d,%d %d,%d", n + 1, p.lx, p.ly, p.rx, p.ry);
                note(s);
            }
            have[n] = in;
            kinds[n] = what;
            if (in) pads[n] = p;
        }
        uint8_t msg[3];
        int len;
        while ((len = cvusb_midi_read(msg)) > 0) {
            int o = snprintf(s, sizeof s, "midi");
            for (int i = 0; i < len; i++) o += snprintf(s + o, sizeof s - (size_t)o, " %02X", msg[i]);
            note(s);
            cvusb_midi_send(msg, (uint32_t)len);
        }

        if (t - t_screen >= 20000) {
            t_screen = t;
            const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
            if (n) cvk_list(list, n, screen.out_full, screen.seq);
        }
    }
}
