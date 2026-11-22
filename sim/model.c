// model.c -- test/host/host.c, the console's models, built into the
// simulator as they are.
//
// host.c is the cart's stand-in for test/run.sh: the screen kernel, the
// frame kernel and their rules, the card through the SDK's file system, the
// menu's launch block, USB, the PSRAM. It is included here whole, with:
//
//   - its SIM_* hooks (top of host.c) wired to the simulator: each TV frame,
//     each PSG byte, each rule broken, the end, the MIDI the plugin sends
//   - its main() renamed host_main (unused): the simulator's main.c sets up
//     what host.c's main() would, from the options instead of PC_* variables
//   - its card's pread() and pwrite() pointed at card.c
//
// Everything that reaches into host.c's own variables is in this file, so
// a change to host.c's models shows up here and nowhere else.
#define _DEFAULT_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>         // (sim/compat/msvc has one for MSVC)
#include <zlib.h>           // (sim/compat/zlib has one for a machine without)

#include "sim.h"

static void model_psg(const uint8_t *b, unsigned n, uint32_t t);
static void model_mailbox(void);

#define SIM_HOOKS
#define SIM_FRAME()         sim_on_frame(next_frame - FRAME_US)
#define SIM_PSG(b, n, t)    model_psg((b), (n), (t))
#define SIM_MAILBOX()       model_mailbox()
#define SIM_FAIL()          sim_on_fail()
#define SIM_END()           sim_on_end(fails)
#define SIM_MIDI(b, n, dev) midi_send((b), (n), (dev), now_us)
#define SIM_EVENT(w, v)     sim_on_event((w), (v))

#undef _DEFAULT_SOURCE                  // (host.c defines it again; the headers have it already)
#define main host_main
#define pread card_pread
#define pwrite card_pwrite
#define open card_fopen
#define off_t int64_t
#include "../test/host/host.c"
#undef main
#undef pread
#undef pwrite
#undef open
#undef off_t

#ifdef _WIN32
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

// ---- sound: what the console writes to its PSG ---------------------------------------
#define T_US (1e6 / PSG_CLOCK)              // a Z80 cycle

// the frame kernel: a list's PSG bytes, t cycles into the frame (gbkernel.asm: 75 + 52 a byte)
static void model_psg(const uint8_t *b, unsigned n, uint32_t t) {
    const double frame = (double)(next_frame - FRAME_US);
    for (unsigned i = 0; i < n; i++) audio_write(frame + (t + 75 + 52.0 * i) * T_US, b[i]);
}

// the screen kernel: as midihello.asm's sound loop, once SND_SEQ changes,
// the noise control if NSEQ changed, then the 10 bytes (an OTIR with the
// PSG's wait: about 50 cycles a byte)
static void model_mailbox(void) {
    static uint8_t nseq_sent;
    if (mbox[MB_SND_SEQ] == mbox_live[MB_SND_SEQ]) return;
    double t = (double)now_us;
    if (mbox[MB_NSEQ] != nseq_sent) {
        nseq_sent = mbox[MB_NSEQ];
        audio_write(t, mbox[MB_NCTL]);
        t += 50 * T_US;
    }
    for (int i = 0; i < MB_SND_LEN; i++) audio_write(t + 50 * T_US * i, mbox[MB_SND + i]);
}

// ---- what host.c's main() does, from the options ---------------------------------------
void model_init(void) {
    static char path[4096];
    out_dir = opt.out;
    end_us = opt.end_us < 0 ? UINT64_MAX : (uint64_t)opt.end_us;
    snprintf(path, sizeof path, "%s/log.txt", out_dir);
    log_file = fopen(path, "w");
    if (opt.lists) snprintf(path, sizeof path, "%s/lists.bin", out_dir);
    else snprintf(path, sizeof path, "%s", NULL_DEVICE);
    listf = fopen(path, "wb");
    if (!log_file || !listf) { perror(out_dir); exit(2); }
    if (opt.script) script_at = script = strdup(opt.script);
    if (opt.no_psram) {
        static char no_psram[] = "PC_NO_PSRAM=1";
        putenv(no_psram);
    }
    if (opt.usb == USB_HOST) computer = false;          // a hub on the port, not a computer
    if (opt.usb == USB_ASK) { computer = false; computer_asks = true; }
    if (card_present()) {
        card_fd = 3;
        sd_info.mb = card_mb();
        snprintf(sd_info.name, sizeof sd_info.name, "SIM");
    }
    if (!opt.no_launch) launch(opt.id ? opt.id : "sim.plugin", opt.open);
}

void model_run(void) {
    plugin_main();
    fail("the plugin's main() returned");
    finish();
}

// ---- the TV, the controls --------------------------------------------------------------
void model_picture(uint8_t pic[SIM_H][SIM_W]) { picture(pic); }

void model_console(uint8_t held, int key) {
    joy_held = held;
    key_held = key;
}

void model_usb_pad(int n, bool in, const gamepad_t *p, bool hid) {
    if (n < 0 || n >= CVUSB_PAD_MAX) return;
    pad_in[n] = in;
    pad_kind[n] = hid ? "HID PAD" : "SWITCH PRO";
    if (in) pad_state[n] = *p;
    else memset(&pad_state[n], 0, sizeof pad_state[n]);
}

// host.c's keyboard and mouse: boot reports for the SDK's own reader
// (cvusb_keys.c, behind cvusb_key() and cvusb_mouse()), and the
// usb-keyboard-mouse example's own queue (usbin_*), as the script's keydown=
// and mouse= feed them
void model_usb_key(uint8_t usage, bool down) { usb_key(usage, down); }

void model_usb_keys_up(void) {
    for (int i = 0; i < 6; i++)
        if (kb_down[i]) usb_key(kb_down[i], false);
    for (int b = 0; b < 8; b++)
        if (kb_mods >> b & 1) usb_key((uint8_t)(0xE0 + b), false);
}

void model_usb_mouse(int dx, int dy, uint8_t buttons, int wheel) { usb_mouse(dx, dy, buttons, wheel); }

// MIDI from the desktop's ports: as the script's midiin= (a MIDI device on
// the hub, or the computer)
void model_midi_in(const uint8_t *bytes, unsigned n) { usb_midi_bytes(bytes, n); }

// a MIDI device on the hub from the start (a port is open), not only once it
// has played something
void model_midi_device(void) { usb_midi_dev = true; }

int model_usb_mode(void) { return (int)usb_mode; }

void model_end(void) { finish(); }
const char *model_note(void) { return note; }
int model_fails(void) { return fails; }

// host.c's PNG writer, to any path ending .png
void model_png(const char *path, uint8_t pic[SIM_H][SIM_W]) {
    static char dir[4096], name[1024];
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
#endif
    if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
    else snprintf(dir, sizeof dir, ".");
    snprintf(name, sizeof name, "%s", slash ? slash + 1 : path);
    const size_t n = strlen(name);
    if (n > 4 && !strcmp(name + n - 4, ".png")) name[n - 4] = 0;
    const char *keep = out_dir;
    out_dir = dir[0] ? dir : "/";
    png(name, pic);
    out_dir = keep;
}

uint8_t model_backdrop(void) { return (f_on ? f_reg : k_reg)[7] & 15; }
