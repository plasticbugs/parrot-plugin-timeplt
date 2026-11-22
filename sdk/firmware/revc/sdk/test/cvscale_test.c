// cvscale_test.c -- cvscale.c's rules on the Mac:
//   cc -O2 -Wall -I.. -o /tmp/cvscale_test cvscale_test.c ../cvscale.c && /tmp/cvscale_test
// It prints what failed and exits 1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t src[64][64], dst[48][48], part[48][48];

static void scale(int w, int h) {
    memset(dst, 0xee, sizeof dst);
    cvscale34(&src[0][0], 64, w, h, 0, w, &dst[0][0], 48);
}

int main(void) {
    // the pick: the same; a stroke; an edge
    CHECK(cvscale_pick(3, 3, 0, 0) == 3, "same");
    CHECK(cvscale_pick(0, 5, 0, 0) == 5, "a stroke in the 3rd survives");
    CHECK(cvscale_pick(5, 0, 0, 0) == 5, "a stroke in the 2nd survives");
    CHECK(cvscale_pick(0, 5, 0, 5) == 0, "an edge stays where it was");
    CHECK(cvscale_pick(5, 0, 5, 0) == 5, "an edge stays where it was (the other way)");

    // a line one pixel wide, across or down, at each of the 4 places in a
    // group, and of each pen: always one line one pixel wide out
    for (int dir = 0; dir < 2; dir++)
        for (int at = 0; at < 16; at++) {
            memset(src, 0, sizeof src);
            for (int i = 0; i < 16; i++) {
                if (dir) src[i][at] = 7;
                else src[at][i] = 7;
            }
            scale(16, 16);
            int lines = 0;
            for (int j = 0; j < 12; j++) {
                int n = 0;
                for (int i = 0; i < 12; i++) n += (dir ? dst[i][j] : dst[j][i]) == 7;
                if (n) { lines++; CHECK(n == 12, "line %s at %d: %d of 12 pixels", dir ? "down" : "across", at, n); }
            }
            CHECK(lines == 1, "line %s at %d: %d lines out", dir ? "down" : "across", at, lines);
        }

    // a 2 x 2 dot at each place: still there, 1 to 2 pixels each way
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++) {
            memset(src, 0, sizeof src);
            src[y][x] = src[y][x + 1] = src[y + 1][x] = src[y + 1][x + 1] = 9;
            scale(8, 8);
            int n = 0;
            for (int j = 0; j < 6; j++)
                for (int i = 0; i < 6; i++) n += dst[j][i] == 9;
            CHECK(n >= 1 && n <= 4, "dot at %d,%d: %d pixels", x, y, n);
        }

    // an edge between lines 1 and 2 stays where it was
    memset(src, 0, sizeof src);
    for (int x = 0; x < 8; x++) src[0][x] = src[1][x] = 1, src[2][x] = src[3][x] = 2;
    scale(8, 4);
    CHECK(dst[1][0] == 1 && dst[2][0] == 2, "an edge keeps the 2nd line: %d %d", dst[1][0], dst[2][0]);
    memset(src, 0, sizeof src);
    for (int x = 0; x < 8; x++) src[2][x] = 4;      // a bar in line 2, nothing in line 1
    src[0][3] = 4; src[1][3] = 4; src[3][3] = 0;    // and a stroke down column 3 into it
    scale(8, 4);
    CHECK(dst[1][0] == 4 && dst[1][5] == 4, "a bar in the 3rd line wins over none in the 2nd: %d %d", dst[1][0], dst[1][5]);

    // a piece: the same as the whole at its place, with the columns either side read
    srand(7);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) src[y][x] = (uint8_t)(rand() % 3 == 0 ? rand() % 4 : 0);
    scale(64, 64);
    for (int cx = 0; cx < 64; cx += 4) {
        memset(part, 0xee, sizeof part);
        cvscale34(&src[0][0], 64, 64, 64, cx, cx + 8 > 64 ? 64 : cx + 8, &part[0][0], 48);
        for (int y = 0; y < 48; y++)
            for (int x = cx / 4 * 3; x < (cx + 8 > 64 ? 48 : cx / 4 * 3 + 6); x++)
                CHECK(part[y][x] == dst[y][x], "piece at %d differs at %d,%d", cx, x, y);
        for (int y = 0; y < 48; y++)
            for (int x = 0; x < cx / 4 * 3; x++) CHECK(part[y][x] == 0xee, "piece at %d wrote outside at %d,%d", cx, x, y);
    }

    // cvscale_mid in two steps gives cvscale_mid's answer (every case of 3 pens)
    for (int k = 0; k < 6561; k++) {
        uint8_t a[8];
        for (int i = 0, v = k; i < 8; i++, v /= 3) a[i] = (uint8_t)(v % 3);
        uint8_t m = cvscale_pick(a[0], a[1], a[2], a[3]);
        if (cvscale_mid_asks(m, a[0], a[1], a[2], a[3])) m = cvscale_mid_then(a[0], a[1], a[4], a[5], a[6], a[7]);
        CHECK(m == cvscale_mid(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]), "two steps differ at case %d", k);
    }

    printf(fails ? "cvscale: %d failed\n" : "cvscale: all passed\n", fails);
    return fails ? 1 : 0;
}
