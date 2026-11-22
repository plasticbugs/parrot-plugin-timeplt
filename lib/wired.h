// wired.h -- the console's own controller (port 1), for a plugin on the
// screen kernel (parrotcart_screen).
//
// Once a TV frame the kernel reads the joystick and the keypad and tells the
// cart; cvk_report() hands them to the plugin as REPORT_KIND_JOY and
// REPORT_KIND_KEY with 5 bits each, low = pressed, exactly as the console
// read them. This turns those into something pleasant:
//
//   wired_t pad;  wired_init(&pad);
//   while (cvk_report(t, &kind, &bits)) {
//       if (wired_report(&pad, kind, bits)) { ... something changed ... }
//   }
//   pad.held        WIRED_UP | WIRED_FIRE_L ...: what is down now
//   pad.key         the keypad key held: 0-9, WIRED_STAR, WIRED_HASH, or -1
//   wired_pressed(&pad)   what went down since the last call (each press once)
//   wired_key(&pad)       the key that went down since the last call, or -1
//
// A report counts once it has come twice in a row (two frames, 33 ms): that
// rides out a contact's bounce and a bad sample on the bus.
//
// Plain C, header-only: the plugin and the Mac tests share it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "mailbox.h"            // REPORT_KIND_* (sdk/firmware/revc/midihello)

enum {
    WIRED_UP = 1, WIRED_RIGHT = 2, WIRED_DOWN = 4, WIRED_LEFT = 8,
    WIRED_FIRE_L = 16,          // the left fire button
    WIRED_FIRE_R = 32,          // the right one (read with the keypad)
};
enum { WIRED_STAR = 10, WIRED_HASH = 11 };

typedef struct {
    uint8_t raw[2];             // the last report of each kind, as it came
    uint8_t held;               // WIRED_*, 1 = down
    uint8_t pressed;            // went down, not yet asked for
    int8_t key;                 // the key held, -1 none
    int8_t key_pressed;         // went down, not yet asked for (-1 none)
} wired_t;

static inline void wired_init(wired_t *p) {
    p->raw[0] = p->raw[1] = 0xFF;
    p->held = p->pressed = 0;
    p->key = p->key_pressed = -1;
}

// the keypad's 4 lines as read -> the key (two keys at once read as none)
static inline int wired_decode_key(uint8_t lines) {
    static const int8_t key[16] = {-1, 8, 4, 5, -1, 7, WIRED_HASH, 2, -1, WIRED_STAR, 0, 9, 3, 1, 6, -1};
    return key[lines & 15];
}

// One report from cvk_report(). True when held or key changed.
static inline bool wired_report(wired_t *p, int kind, uint8_t bits) {
    if (kind != REPORT_KIND_JOY && kind != REPORT_KIND_KEY) return false;
    const int k = kind == REPORT_KIND_KEY;
    const uint8_t v = bits & 0x1F;
    if (v != p->raw[k]) {       // new: it counts if the next one agrees
        p->raw[k] = v;
        return false;
    }
    uint8_t held = p->held;
    int8_t key = p->key;
    if (k) {
        key = (int8_t)wired_decode_key(v);
        held = (uint8_t)((held & ~WIRED_FIRE_R) | (v & 0x10 ? 0 : WIRED_FIRE_R));
    } else {
        held = (uint8_t)((held & WIRED_FIRE_R) | (~v & 0x1F));   // N E S W and the left fire, in WIRED_*'s order
    }
    const bool changed = held != p->held || key != p->key;
    p->pressed |= (uint8_t)(held & ~p->held);
    if (key >= 0 && key != p->key) p->key_pressed = key;
    p->held = held;
    p->key = key;
    return changed;
}

static inline uint8_t wired_pressed(wired_t *p) {
    const uint8_t v = p->pressed;
    p->pressed = 0;
    return v;
}

static inline int wired_key(wired_t *p) {
    const int k = p->key_pressed;
    p->key_pressed = -1;
    return k;
}
