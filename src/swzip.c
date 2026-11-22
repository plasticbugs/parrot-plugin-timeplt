// swzip.c -- see swzip.h. The inflate is the textbook one: canonical Huffman
// codes decoded a bit at a time from their lengths' counts (as zlib's
// contrib/puff does it; written here from RFC 1951). Small, not fast: a ROM
// set is 74K.
#include "swzip.h"

#include <string.h>

typedef struct {
    const uint8_t *in;
    uint32_t inlen, incnt;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    uint32_t outlen, outcnt;
    bool bad;
} inf_t;

typedef struct {
    uint16_t count[16];
    uint16_t symbol[288];
} huff_t;

static int bits(inf_t *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen) { s->bad = true; return 0; }
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int decode(inf_t *s, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= bits(s, 1);
        if (s->bad) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->bad = true;
    return -1;
}

// the table for n symbols of the given code lengths; false if they over-subscribe
static bool construct(huff_t *h, const uint8_t *length, int n) {
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return false;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) h->symbol[offs[length[i]]++] = (uint16_t)i;
    return true;
}

static bool codes(inf_t *s, const huff_t *lencode, const huff_t *distcode) {
    static const uint16_t lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) return false;
        if (sym < 256) {
            if (s->outcnt == s->outlen) return false;
            s->out[s->outcnt++] = (uint8_t)sym;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            uint32_t len = lens[sym] + (uint32_t)bits(s, lext[sym]);
            sym = decode(s, distcode);
            if (sym < 0 || sym >= 30) return false;
            uint32_t dist = dists[sym] + (uint32_t)bits(s, dext[sym]);
            if (s->bad || dist > s->outcnt || s->outcnt + len > s->outlen) return false;
            for (; len; len--, s->outcnt++) s->out[s->outcnt] = s->out[s->outcnt - dist];
        }
    }
}

static bool stored(inf_t *s) {
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return false;
    uint32_t len = s->in[s->incnt] | (uint32_t)s->in[s->incnt + 1] << 8;
    if ((len ^ 0xFFFF) != (s->in[s->incnt + 2] | (uint32_t)s->in[s->incnt + 3] << 8)) return false;
    s->incnt += 4;
    if (s->incnt + len > s->inlen || s->outcnt + len > s->outlen) return false;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len;
    s->outcnt += len;
    return true;
}

static huff_t lencode, distcode;        // (not on the stack: 1.2K, and the lengths below)
static uint8_t length[320];

static bool fixed(inf_t *s) {
    int i = 0;
    for (; i < 144; i++) length[i] = 8;
    for (; i < 256; i++) length[i] = 9;
    for (; i < 280; i++) length[i] = 7;
    for (; i < 288; i++) length[i] = 8;
    construct(&lencode, length, 288);
    for (i = 0; i < 30; i++) length[i] = 5;
    construct(&distcode, length, 30);
    return codes(s, &lencode, &distcode);
}

static bool dynamic(inf_t *s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30) return false;
    int i;
    for (i = 0; i < ncode; i++) length[order[i]] = (uint8_t)bits(s, 3);
    for (; i < 19; i++) length[order[i]] = 0;
    if (!construct(&lencode, length, 19)) return false;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lencode);
        if (sym < 0) return false;
        if (sym < 16) {
            length[i++] = (uint8_t)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (!i) return false;
                len = length[i - 1];
                rep = 3 + bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + bits(s, 3);
            } else {
                rep = 11 + bits(s, 7);
            }
            if (s->bad || i + rep > nlen + ndist) return false;
            while (rep--) length[i++] = (uint8_t)len;
        }
    }
    if (!length[256]) return false;
    if (!construct(&lencode, length, nlen)) return false;
    if (!construct(&distcode, length + nlen, ndist)) return false;
    return codes(s, &lencode, &distcode);
}

bool swzip_inflate(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t dstlen) {
    inf_t s = {src, n, 0, 0, 0, dst, dstlen, 0, false};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.bad) return false;
        bool ok = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : false;
        if (!ok || s.bad) return false;
    } while (!last);
    return s.outcnt == dstlen;
}

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

bool swzip_entry(const uint8_t *zip, uint32_t n, int i, swzip_entry_t *e) {
    if (n < 22) return false;
    uint32_t end = n - 22;              // the end record: the last "PK\5\6" (a comment may follow it)
    for (;; end--) {
        if (le32(zip + end) == 0x06054B50) break;
        if (!end || n - end > 0x10000 + 22) return false;
    }
    uint32_t count = le16(zip + end + 10), at = le32(zip + end + 16);
    if (i < 0 || (uint32_t)i >= count) return false;
    for (int k = 0;; k++) {
        if (at + 46 > n || le32(zip + at) != 0x02014B50) return false;
        if (k == i) break;
        at += 46 + le16(zip + at + 28) + le16(zip + at + 30) + le16(zip + at + 32);
    }
    e->method = (uint16_t)le16(zip + at + 10);
    e->crc = le32(zip + at + 16);
    e->csize = le32(zip + at + 20);
    e->usize = le32(zip + at + 24);
    uint32_t local = le32(zip + at + 42);
    e->data = NULL;
    if (local + 30 <= n && le32(zip + local) == 0x04034B50) {
        uint32_t d = local + 30 + le16(zip + local + 26) + le16(zip + local + 28);
        if (d + e->csize <= n) e->data = zip + d;
    }
    return true;
}

bool swzip_read(const swzip_entry_t *e, uint8_t *out) {
    if (!e->data) return false;
    if (e->method == 0) {
        if (e->csize != e->usize) return false;
        memcpy(out, e->data, e->usize);
        return true;
    }
    return e->method == 8 && swzip_inflate(e->data, e->csize, out, e->usize);
}
