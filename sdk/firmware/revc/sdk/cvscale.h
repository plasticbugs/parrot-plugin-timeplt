// cvscale.h -- a picture's pens scaled by 3/4 both ways, for plugins that
// make pictures on the cart (an arcade game's 224 or 256 wide to the
// TMS9918's 192 high). Portable.
//
// Of each 4 lines the 1st and 4th are kept and the 2nd and 3rd become one,
// then the same for each 4 columns. The two that become one: the same, or
// the one unlike both its neighbours if one is (a stroke one pixel wide
// survives, light on dark or dark on light), or else the first (an edge
// stays where it was). For the lines, where that can't tell (an edge
// between the two), a stroke along the line in the 3rd wins over none in
// the 2nd, as in the bars of E and F; pen 0 counts as the background there.
//
// Pens in, pens out: any 8-bit values (palette indices), compared only for
// being the same. Q*bert's picture (../qbert/qbcv.c) is made with these.
#pragma once
#include <stdint.h>

// The middle pair of a group of 4 along a line: a and b, between before and after
static inline __attribute__((always_inline)) uint8_t cvscale_pick(uint8_t a, uint8_t b, uint8_t before, uint8_t after) {
    if (a == b) return a;
    if (b != before && b != after && (a == before || a == after)) return b;
    return a;
}

// The middle pair of a group of 4 lines, at one column: u (2nd line) and v
// (3rd), between before (1st) and after (4th); ub, ua: u's neighbours along
// its line (the columns either side), vb, va: v's
static inline __attribute__((always_inline)) uint8_t cvscale_mid(uint8_t u, uint8_t v, uint8_t before, uint8_t after,
                                                                 uint8_t ub, uint8_t ua, uint8_t vb, uint8_t va) {
    uint8_t m = cvscale_pick(u, v, before, after);
    if (m == u && v && (u == before || u == after) && (vb == v || va == v) && !(u && (ub == u || ua == u))) m = v;
    return m;
}

// cvscale_mid in two steps, for a caller that loads the neighbours only
// when they count: m = cvscale_pick(u, v, before, after); if
// cvscale_mid_asks(m, u, v, before, after), m = cvscale_mid_then(u, v, ub,
// ua, vb, va). The same answer as cvscale_mid.
static inline __attribute__((always_inline)) int cvscale_mid_asks(uint8_t m, uint8_t u, uint8_t v, uint8_t before, uint8_t after) {
    return m == u && v && (u == before || u == after);
}
static inline __attribute__((always_inline)) uint8_t cvscale_mid_then(uint8_t u, uint8_t v, uint8_t ub, uint8_t ua, uint8_t vb, uint8_t va) {
    return (vb == v || va == v) && !(u && (ub == u || ua == u)) ? v : u;
}

// w x h pens at src (w, h multiples of 4; a line every sstride bytes) to
// 3w/4 x 3h/4 at dst (dstride). Columns cx0 to cx1 - 1 only (multiples of
// 4; all: 0, w), which come out at dst's 3 cx0 / 4 on: a piece of a picture
// comes out as the whole would have it there, the columns either side read
// as neighbours. Outside the w x h, neighbours are 0.
void cvscale34(const uint8_t *src, int sstride, int w, int h, int cx0, int cx1, uint8_t *dst, int dstride);
