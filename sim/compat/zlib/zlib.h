// zlib.h -- the two zlib functions host.c's PNGs need, for a machine
// without zlib (Windows, usually). sim/CMakeLists.txt uses the real zlib
// when it finds one. compress2() here stores the data uncompressed, which
// is still a valid PNG: a 256x192 screenshot is then 49 KB, under the
// registry's 100 KB.
#pragma once
#include <stddef.h>
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef unsigned long uLongf;
#define Z_OK 0
#define Z_BUF_ERROR (-5)
int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);
uLong crc32(uLong crc, const Bytef *buf, uInt len);
