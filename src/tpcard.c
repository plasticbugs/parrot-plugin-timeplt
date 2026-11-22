// tpcard.c -- see tpcard.h. (Donkey Kong's dkcard.c.)
#include "tpcard.h"

#include <string.h>

#include "swzip.h"

static uint8_t sec[512];                // (not on the stack: the plugin's is small)

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

// n bytes of the open file from pos
static bool read_at(fs_t *fs, uint32_t pos, uint8_t *dst, uint32_t n, void (*tick)(void)) {
    while (n) {
        uint32_t lba = fs_lba(fs, pos / 512), o = pos % 512, k = 512 - o < n ? 512 - o : n;
        if (!lba || !fs->read(lba, sec)) return false;
        memcpy(dst, sec + o, k);
        dst += k;
        pos += k;
        n -= k;
        if (tick) tick();
    }
    return true;
}

// A zip: its end record and directory (the last of the file), then every
// entry the directory says is a ROM still wanted, unpacked into buf and
// put in its place if its CRC-32 is right.
static void take_zip(fs_t *fs, uint32_t size, uint8_t *buf, uint32_t max, void (*tick)(void), uint32_t *found) {
    uint32_t tail = size < 1024 ? size : 1024, end;
    if (tail < 22 || tail > max || !read_at(fs, size - tail, buf, tail, tick)) return;
    for (end = tail - 22;; end--) {
        if (le32(buf + end) == 0x06054B50) break;
        if (!end) return;
    }
    uint32_t count = le16(buf + end + 10), cd_size = le32(buf + end + 12), cd_at = le32(buf + end + 16);
    static struct { uint32_t local, csize; uint16_t method; int8_t rom; } want[TP_ROM_FILES];
    int n = 0;
    if (cd_size > max || cd_at + cd_size > size || !read_at(fs, cd_at, buf, cd_size, tick)) return;
    for (uint32_t at = 0, k = 0; k < count && at + 46 <= cd_size && n < TP_ROM_FILES; k++) {
        const uint8_t *e = buf + at;
        if (le32(e) != 0x02014B50) break;
        int rom = tproms_wanted(le32(e + 24), le32(e + 16), *found);
        bool again = false;
        for (int j = 0; j < n; j++) again |= want[j].rom == rom;
        if (rom >= 0 && !again && ((le32(e + 20) + 3) & ~3u) + tp_rom_files[rom].size <= max) {
            want[n].local = le32(e + 42);
            want[n].csize = le32(e + 20);
            want[n].method = (uint16_t)le16(e + 10);
            want[n++].rom = (int8_t)rom;
        }
        at += 46 + le16(e + 28) + le16(e + 30) + le16(e + 32);
    }
    for (int j = 0; j < n; j++) {
        static uint8_t head[30];
        const tp_rom_file_t *r = &tp_rom_files[want[j].rom];
        if (want[j].local + 30 > size || !read_at(fs, want[j].local, head, 30, tick) || le32(head) != 0x04034B50) continue;
        uint32_t data = want[j].local + 30 + le16(head + 26) + le16(head + 28);
        if (data + want[j].csize > size || !read_at(fs, data, buf, want[j].csize, tick)) continue;
        // unpacked after the packed bytes in buf (a stored one is there already)
        uint8_t *rom = buf + ((want[j].csize + 3) & ~3u);
        bool ok = want[j].method == 8 ? swzip_inflate(buf, want[j].csize, rom, r->size)
                : want[j].method == 0 && want[j].csize == r->size ? (rom = buf, true) : false;
        if (ok && tp_crc32(0, rom, r->size) == r->crc) {
            tproms_put(want[j].rom, rom);
            *found |= 1u << want[j].rom;
        }
    }
}

// a file: a ROM by itself (read, known by its CRC-32, put in its place), or a zip of them
static void take(fs_t *fs, const fs_file_t *f, uint8_t *buf, uint32_t max, void (*tick)(void), uint32_t *found) {
    uint32_t n = (uint32_t)f->size;
    if (f->is_dir || f->size > 0x1000000 || n < 22 || fs_open(fs, f) != FS_OK) return;
    if (tproms_size_ok(n) && n <= max) {
        if (!read_at(fs, 0, buf, n, tick)) return;
        int rom = tproms_wanted(n, tp_crc32(0, buf, n), *found);
        if (rom >= 0) {
            tproms_put(rom, buf);
            *found |= 1u << rom;
            return;
        }
        if (le32(buf) != 0x04034B50) return;
    } else {
        uint8_t magic[4];
        if (!read_at(fs, 0, magic, 4, tick) || le32(magic) != 0x04034B50) return;
    }
    take_zip(fs, n, buf, max, tick, found);
}

// (each file as the listing comes to it: a listing keeps its place through
// a file's reads, fsro.h; fs_dir_next gives 1 for each entry)
uint32_t tpcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void)) {
    static fs_dir_t dir;                // (too big for the stack)
    uint32_t found = 0;
    fs_file_t e;
    if (fs_dir_open_path(fs, &dir, TPCARD_HOME) == FS_OK)
        while (found != TPROMS_ALL && fs_dir_next(fs, &dir, &e) == 1) take(fs, &e, buf, max, tick, &found);
    if (found != TPROMS_ALL && fs_find(fs, TPCARD_ZIP, &e) == FS_OK) take(fs, &e, buf, max, tick, &found);
    return found;
}
