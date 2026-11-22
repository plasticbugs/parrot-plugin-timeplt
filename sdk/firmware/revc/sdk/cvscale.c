// cvscale.c -- see cvscale.h.
#include <string.h>

#include "cvscale.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define CVSCALE_HOT __attribute__((section(".time_critical.cvscale")))
#else
#define CVSCALE_HOT
#endif

CVSCALE_HOT void cvscale34(const uint8_t *src, int sstride, int w, int h, int cx0, int cx1, uint8_t *dst, int dstride) {
    for (int y = 0; y + 4 <= h; y += 4) {
        const uint8_t *l0 = src + y * sstride, *l1 = l0 + sstride, *l2 = l1 + sstride, *l3 = l2 + sstride;
        uint8_t *o0 = dst + (y / 4 * 3) * dstride + cx0 / 4 * 3, *o1 = o0 + dstride, *o2 = o1 + dstride;
        for (int x = cx0; x < cx1; x += 4, o0 += 3, o1 += 3, o2 += 3) {
            uint32_t w0, w1, w2, w3;    // 4 x 4 of one pen: 3 x 3 of it, whatever the neighbours
            memcpy(&w0, l0 + x, 4);
            memcpy(&w1, l1 + x, 4);
            memcpy(&w2, l2 + x, 4);
            memcpy(&w3, l3 + x, 4);
            if (w0 == w1 && w0 == w2 && w0 == w3 && w0 == l0[x] * 0x01010101u) {
                uint8_t k = l0[x];
                o0[0] = o0[1] = o0[2] = o1[0] = o1[1] = o1[2] = o2[0] = o2[1] = o2[2] = k;
                continue;
            }
            uint8_t m[4];               // the middle line's 4 pixels
            for (int i = 0; i < 4; i++) {
                int xx = x + i;
                uint8_t ub = xx ? l1[xx - 1] : 0, ua = xx + 1 < w ? l1[xx + 1] : 0;
                uint8_t vb = xx ? l2[xx - 1] : 0, va = xx + 1 < w ? l2[xx + 1] : 0;
                m[i] = cvscale_mid(l1[xx], l2[xx], l0[xx], l3[xx], ub, ua, vb, va);
            }
            o0[0] = l0[x];
            o0[1] = cvscale_pick(l0[x + 1], l0[x + 2], l0[x], l0[x + 3]);
            o0[2] = l0[x + 3];
            o1[0] = m[0];
            o1[1] = cvscale_pick(m[1], m[2], m[0], m[3]);
            o1[2] = m[3];
            o2[0] = l3[x];
            o2[1] = cvscale_pick(l3[x + 1], l3[x + 2], l3[x], l3[x + 3]);
            o2[2] = l3[x + 3];
        }
    }
}
