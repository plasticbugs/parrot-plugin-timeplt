// minizlib.c -- see zlib.h beside it.
#include "zlib.h"

uLong crc32(uLong crc, const Bytef *buf, uInt len) {
    static unsigned long table[256];
    if (!table[1])
        for (unsigned long n = 0; n < 256; n++) {
            unsigned long c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320ul ^ c >> 1 : c >> 1;
            table[n] = c;
        }
    crc = ~crc & 0xFFFFFFFFul;
    for (uInt i = 0; i < len; i++) crc = table[(crc ^ buf[i]) & 255] ^ crc >> 8;
    return ~crc & 0xFFFFFFFFul;
}

// a zlib stream of stored (uncompressed) deflate blocks
int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level) {
    (void)level;
    const uLong blocks = sourceLen / 65535 + 1, need = 2 + blocks * 5 + sourceLen + 4;
    if (*destLen < need) return Z_BUF_ERROR;
    Bytef *p = dest;
    *p++ = 0x78;
    *p++ = 0x01;
    unsigned long a = 1, b = 0;
    uLong at = 0;
    do {
        const uLong n = sourceLen - at > 65535 ? 65535 : sourceLen - at;
        *p++ = at + n == sourceLen;         // BFINAL, BTYPE 00
        *p++ = (Bytef)n;
        *p++ = (Bytef)(n >> 8);
        *p++ = (Bytef)~n;
        *p++ = (Bytef)(~n >> 8);
        for (uLong i = 0; i < n; i++) {
            *p++ = source[at + i];
            a = (a + source[at + i]) % 65521;
            b = (b + a) % 65521;
        }
        at += n;
    } while (at < sourceLen);
    const unsigned long adler = b << 16 | a;
    *p++ = (Bytef)(adler >> 24);
    *p++ = (Bytef)(adler >> 16);
    *p++ = (Bytef)(adler >> 8);
    *p++ = (Bytef)adler;
    *destLen = (uLongf)(p - dest);
    return Z_OK;
}
