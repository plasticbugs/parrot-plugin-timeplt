// card.c -- see card.h.
#include "card.h"

#include <string.h>

static fs_t *fs;                    // (in memory main.c gives: it's 14 KB)
static fs_dir_t dir;
static uint8_t sector[512];
static bool mounted;
static void (*poll_fn)(void);

void card_set_poll(void (*poll)(void)) { poll_fn = poll; }

void card_memory(fs_t *memory) { fs = memory; }

static const char *why(int r) {
    switch (r) {
    case FS_OK:       return NULL;
    case FS_EIO:      return "the card didn't answer";
    case FS_ENOFS:    return "the card has no FAT or exFAT file system";
    case FS_ENOFILE:  return "no such file";
    case FS_ENOTDIR:  return "no such folder";
    case FS_EFULL:    return "the card is full";
    case FS_ENAME:    return "that name can't be used";
    case FS_ENOWRITE: return "the card can't be written";
    default:          return "the card's file system has a problem";
    }
}

const char *card_mount(void) {
    if (mounted) return NULL;
    if (!fs) return "no memory for the card";
    if (!cvsd_card_in()) return "there's no card in the slot";
    if (!cvsd_init()) return "the card didn't start";
    const int r = fs_mount(fs, cvsd_read);
    if (r != FS_OK) return why(r);
    fs_set_writer(fs, cvsd_write);
    mounted = true;
    return NULL;
}

fs_t *card_fs(bool failed) {
    if (failed) mounted = false;
    return card_mount() ? NULL : fs;
}

const char *card_mount_launched(const cvl_t *b, fs_file_t *f) {
    if (!fs) return "no memory for the card";
    const int r = cvsd_open_launched(fs, b, true, f);
    if (r != FS_OK) return why(r);
    mounted = true;
    return NULL;
}

const char *card_find(const char *path, fs_file_t *e) {
    const char *err = card_mount();
    if (err) return err;
    return why(fs_find(fs, path, e));
}

const char *card_read(const fs_file_t *e, uint32_t off, uint8_t *dst, uint32_t n) {
    const char *err = card_mount();
    if (err) return err;
    if (!n) return NULL;
    const int r = fs_open(fs, e);
    if (r != FS_OK) return why(r);
    uint32_t count = 0;
    while (n) {
        const uint32_t sec = off / 512, at = off % 512;
        const uint32_t lba = fs_lba(fs, sec);
        if (!lba) return "the file is shorter than the card says";
        uint32_t take = 512 - at;
        if (take > n) take = n;
        if (take == 512) {
            if (!cvsd_read(lba, dst)) return "the card didn't answer";
        } else {
            if (!cvsd_read(lba, sector)) return "the card didn't answer";
            memcpy(dst, sector + at, take);
        }
        dst += take;
        off += take;
        n -= take;
        if (poll_fn && (++count & 7) == 0) poll_fn();
    }
    return NULL;
}

const char *card_write(const fs_file_t *e, uint32_t off, const uint8_t *src, uint32_t n) {
    const char *err = card_mount();
    if (err) return err;
    if (!n) return NULL;
    int r = fs_open(fs, e);
    if (r != FS_OK) return why(r);
    while (n) {
        const uint32_t sec = off / 512, at = off % 512;
        const uint32_t lba = fs_lba(fs, sec);
        if (!lba) return "the file is shorter than the card says";
        uint32_t take = 512 - at;
        if (take > n) take = n;
        if (take == 512) memcpy(sector, src, 512);
        else {                          // part of a sector: the rest of it stays
            if (!cvsd_read(lba, sector)) return "the card didn't answer";
            memcpy(sector + at, src, take);
        }
        r = fs_write_sec(fs, sec, sector);
        if (r != FS_OK) return why(r);
        src += take;
        off += take;
        n -= take;
    }
    return NULL;
}

// a path as fsro's writers take it: the folder, and the name as UTF-16
static char folder[256];
static uint16_t name16[128];
static const char *split(const char *path, int *n16) {
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    size_t fl = slash ? (size_t)(slash - path) : 0;
    if (fl >= sizeof folder) return "the path is too long";
    memcpy(folder, path, fl);
    folder[fl] = 0;
    if (!fl) strcpy(folder, "/");
    int n = 0;
    while (name[n] && n < 127) {
        name16[n] = (uint8_t)name[n];
        n++;
    }
    if (!n || name[n]) return "that name can't be used";
    *n16 = n;
    return NULL;
}

const char *card_create(const char *path, uint32_t size, fs_file_t *e, bool *made) {
    const char *err = card_mount();
    if (err) return err;
    if (!size || size > CARD_CREATE_MAX) return "a new file is 1 byte to 1 MB";
    int n;
    err = split(path, &n);
    if (err) return err;
    int r = fs_create(fs, folder, name16, n, size, e, made);
    if (r != FS_OK) return why(r);
    if (!*made) return NULL;
    // a new file holds whatever the card had there: zeros instead
    r = fs_open(fs, e);
    if (r != FS_OK) return why(r);
    memset(sector, 0, sizeof sector);
    for (uint32_t s = 0; s < (size + 511) / 512; s++) {
        r = fs_write_sec(fs, s, sector);
        if (r != FS_OK) return why(r);
    }
    return NULL;
}

const char *card_list(const char *path, card_list_fn fn, void *ctx) {
    const char *err = card_mount();
    if (err) return err;
    int r = fs_dir_open_path(fs, &dir, path);
    if (r != FS_OK) return why(r == FS_ENOFILE ? FS_ENOTDIR : r);
    fs_file_t e;
    while ((r = fs_dir_next(fs, &dir, &e)) == 1) {
        if (e.attr & (FS_A_HIDDEN | FS_A_SYSTEM)) continue;
        if (dir.lname[0] == '.') continue;
        if (!fn(dir.lname, &e, ctx)) break;
    }
    return r < 0 ? why(r) : NULL;
}
