// test/vdp: every TMS9918A mode and sprite case, for the renderer test.
//
// Not an example to copy: a plugin for test/run.sh only. It runs on the
// frame kernel and fills all 16K of video RAM with the same pseudo-random
// bytes every time, then sets the registers for a case, picked on the
// keypad (PC_SCRIPT "100:key=N"), and a sprite table made to hit the rules.
// The models draw it with test/host/tms9918.c, MAME with its own TMS9928A,
// and test/run.sh says whether the two pictures agree.
//
//   1  Graphics I, 8x8 sprites: six on one line (two over the limit), one
//      half above the top, one pushed left by the early clock, one cut off
//      at the right, a transparent one in front of a coloured one
//   2  Graphics II with its tables masked to 2K (every third the same), 16x16 sprites
//   3  Text, 40 columns (no sprites in this mode)
//   4  Multicolor, 8x8 sprites magnified
//   5  Graphics II, 16x16 sprites magnified, five on a line
//   6  the display blanked: the backdrop only
#include <string.h>

#include "pico/stdlib.h"

#include "cvframes.h"
#include "gbkernel.h"
#include "handoff.h"

#define FRAME_T     (262 * 228)
#define LIST_LIMIT  (FRAME_T - 4000)

static uint8_t want[0x4000], shown[0x4000], reg[8];
static uint8_t list[CVF_LIST_MAX];

static void sprite(int n, int y, int x, int name, int colour) {
    uint8_t *s = want + (reg[5] & 0x7F) * 128 + 4 * n;
    s[0] = (uint8_t)y;
    s[1] = (uint8_t)x;
    s[2] = (uint8_t)name;
    s[3] = (uint8_t)colour;
}

static void setup(int k) {
    uint32_t r = 12345;
    for (int i = 0; i < 0x4000; i++) {      // the same random video RAM for every case
        r = r * 1103515245u + 12345u;
        want[i] = (uint8_t)(r >> 16);
    }
    // R0: M3; R1: 16K, display, NMI, M1 M2, size, magnify; R2 names; R3 colours; R4 patterns;
    // R5 sprite attributes; R6 sprite patterns; R7 text colour and backdrop
    static const uint8_t regs[7][8] = {
        {0x02, 0xE0, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x01},      // (no key yet: the kernel's own)
        {0x00, 0xE0, 0x06, 0x80, 0x00, 0x36, 0x07, 0x04},      // 1 Graphics I, 8x8
        {0x02, 0xE2, 0x06, 0x9F, 0x00, 0x36, 0x07, 0x05},      // 2 Graphics II masked, 16x16
        {0x00, 0xF0, 0x02, 0x00, 0x01, 0x36, 0x07, 0xF4},      // 3 Text, white on dark blue
        {0x00, 0xE9, 0x06, 0x00, 0x01, 0x36, 0x07, 0x0D},      // 4 Multicolor, 8x8 magnified
        {0x02, 0xE3, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x06},      // 5 Graphics II, 16x16 magnified
        {0x02, 0xA0, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x0C},      // 6 blanked, magenta backdrop
    };
    memcpy(reg, regs[k >= 1 && k <= 6 ? k : 0], 8);
    int n = 0;
    if (k == 1) {
        for (int i = 0; i < 6; i++) sprite(n++, 40, 10 + 30 * i, i, 2 + i);   // six on lines 41-48
        sprite(n++, 0xFA, 60, 7, 15);       // above the top: lines 0-1 only
        sprite(n++, 100, 20, 8, 0x80 | 9);  // early clock: x - 32
        sprite(n++, 120, 252, 9, 11);       // past the right edge
        sprite(n++, 150, 100, 10, 0);       // transparent, in front of...
        sprite(n++, 152, 102, 11, 14);      // ...a coloured one
    } else if (k == 2) {
        for (int i = 0; i < 4; i++) sprite(n++, 60 + 20 * i, 30 + 50 * i, 4 * i, 3 + i);
        sprite(n++, 66, 40, 20, 10);        // over sprite 0, behind it
    } else if (k == 4) {
        for (int i = 0; i < 5; i++) sprite(n++, 30 + 25 * i, 20 + 40 * i, 3 + i, 2 + 2 * i);
    } else if (k == 5) {
        for (int i = 0; i < 5; i++) sprite(n++, 80, 5 + 50 * i, 4 * i, 2 + i);  // five on lines 81-112
        sprite(n++, 0xE8, 120, 40, 15);     // 32 lines high, half above the top
    }
    sprite(n, 0xD0, 0, 0, 0);               // the end of the table
}

// a list: the registers in the vertical blank, then as many changed bytes as fit
static size_t make_list(void) {
    size_t len = 0;
    uint32_t cost = 480 + 54 + 52;
    for (int r = 0; r < 8; r++) {
        list[len++] = 3;
        list[len++] = reg[r];
        list[len++] = (uint8_t)(0x80 | r);
        cost += 114;
    }
    list[len++] = 0;
    for (int i = 0; i < 0x4000;) {
        if (want[i] == shown[i]) { i++; continue; }
        if (cost + 106 + 31 > LIST_LIMIT) break;
        int n = 0;
        while (n < 255 && i + n < 0x4000 && cost + 106 + 31u * (n + 1) <= LIST_LIMIT) n++;
        list[len++] = 1;
        list[len++] = (uint8_t)i;
        list[len++] = (uint8_t)(i >> 8 | 0x40);
        list[len++] = (uint8_t)n;
        memcpy(list + len, want + i, (size_t)n);
        memcpy(shown + i, want + i, (size_t)n);
        len += (size_t)n;
        cost += 106 + 31u * (uint32_t)n;
        i += n;
    }
    list[len++] = 0;
    return len;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvf_note("VDP test");
    shown[0x1B00] = 0xD0;                   // as the kernel leaves it
    int k = 0;
    setup(k);
    size_t len = make_list();
    bool pending = true;
    uint32_t seen = 0;
    for (;;) {
        cvf_task(time_us_32());
        if (pending && cvf_publish(list, len)) pending = false;
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();
        uint8_t joy, fire, key;
        cvf_input(&joy, &fire, &key);
        static const int8_t digit[16] = {-1, 8, 4, 5, -1, 7, 11, 2, -1, 10, 0, 9, 3, 1, 6, -1};
        const int d = digit[key & 15];
        bool fresh = false;                 // new registers: one list at least
        if (d >= 1 && d <= 6 && d != k) {
            k = d;
            setup(k);
            fresh = true;
        }
        if (fresh || memcmp(want, shown, sizeof want)) {
            len = make_list();
            pending = true;
        }
    }
}
