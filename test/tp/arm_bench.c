// arm_bench.c -- the cart's code for test/tp/armcount.py: what a list (a
// TV frame) costs on the Cortex-M33, counted in instructions on Unicorn.
// Built bare (no SDK) with the plugin's flags (armcount.sh). The Python
// side puts the set's files in the arrays below, calls bench_init() once
// and bench_list() for each list. The game plays itself: a coin and 1P
// start, then tp_host's bot (it turns toward the nearest enemy and fires).
// (Donkey Kong's.)
#include <string.h>

#include "../../src/tp.h"
#include "../../src/tpay.h"
#include "../../src/tpcv.h"
#include "../../src/tprun.h"

uint8_t bench_maincpu[0x6000], bench_sound[0x1000], bench_tiles[0x2000], bench_sprites[0x4000], bench_proms[0x240];
uint8_t bench_behind;                   // (armcount.py's queue; unused here)
uint32_t bench_len, bench_pictures, bench_hurried;
uint8_t bench_out[TPCV_LIST_MAX];       // the last list (armcount.py reads it back)
static tpcv_t cv;
static tprun_t run;
static void (*run_vblank)(void);

static uint8_t bot(uint32_t n) {
    const uint8_t *s0 = tp_spriteram[0], *s1 = tp_spriteram[1];
    int px = s0[0x16] + 8, py = 241 - s1[0x17] + 8;
    int best = 1 << 30, dx = 0, dy = 0;
    for (int offs = 0x24; offs <= 0x34; offs += 2) {
        if (!s0[offs + 1]) continue;
        int col = s1[offs] & 0x3f;
        if (col == 12 || col == 44) continue;
        int ex = s0[offs] + 8, ey = 241 - s1[offs + 1] + 8;
        int ddx = -(ey - py), ddy = ex - px;
        int d2 = ddx * ddx + ddy * ddy;
        if (d2 < best) { best = d2; dx = ddx; dy = ddy; }
    }
    uint8_t v = (n & 4) ? 0x10 : 0;
    if (best == 1 << 30) return v;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax * 5 > ay * 2) v |= dx > 0 ? 0x02 : 0x01;
    if (ay * 5 > ax * 2) v |= dy > 0 ? 0x08 : 0x04;
    return v;
}

static void on_vblank(void) {
    run_vblank();
    uint32_t n = tp_frames;
    if (n == 300) tp_in.in0 = (uint8_t)~0x01;   // a coin
    if (n == 306) tp_in.in0 = 0xff;
    if (n == 360) tp_in.in0 = (uint8_t)~0x08;   // 1P start
    if (n == 366) tp_in.in0 = 0xff;
    if (n >= 440) tp_in.in1 = (uint8_t)~bot(n);
}

void bench_init(void) {
#if defined(__arm__)
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88;     // the FPU on (CP10, CP11)
    *cpacr |= 0xFu << 20;
#endif
    static tp_roms_t roms;
    roms = (tp_roms_t){bench_maincpu, bench_sound, bench_tiles, bench_sprites, bench_proms};
    tpcv_init(&cv);
    tpay_reset();
    tp_init(&roms);
    tpcv_prepare();
    tprun_start(&run, &cv);
    run_vblank = tp_on_vblank;
    tp_on_vblank = on_vblank;
}

void bench_list(void) {
    bench_len = (uint32_t)tprun_list(&run, bench_out);
    bench_pictures = run.pictures;
    bench_hurried = run.waited;
}

void _exit(int x) { (void)x; for (;;); }
