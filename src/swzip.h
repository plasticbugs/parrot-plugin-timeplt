// swzip.h -- just enough of zip to take a ROM set out of one: the central
// directory's entries, and inflate (RFC 1951) for the deflated ones.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t crc, csize, usize;
    uint16_t method;                // 0 stored, 8 deflated
    const uint8_t *data;            // its bytes in the zip (NULL: outside it)
} swzip_entry_t;

// entry i of the zip in zip[0..n): false past the last (or if it isn't a zip)
bool swzip_entry(const uint8_t *zip, uint32_t n, int i, swzip_entry_t *e);
// an entry's bytes into out (e->usize of them): false if they don't come out right
bool swzip_read(const swzip_entry_t *e, uint8_t *out);
// raw deflate: src[0..n) into dst, which must come to exactly dstlen bytes
bool swzip_inflate(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t dstlen);
