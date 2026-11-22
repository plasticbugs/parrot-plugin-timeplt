// card.c -- the SD card, under the models' cvsd_read() and cvsd_write().
//
// host.c reads its card with pread() and writes it with pwrite() on its
// card_fd; model.c points those at card_pread() and card_pwrite(), so the
// plugin's reads and writes go through the SDK's own file system (fsro.c)
// to one of these:
//
//   a folder   (--card FOLDER) a FAT32 volume made from the folder when the
//              simulator starts, behind an MBR as on a real card. Its boot
//              sector, FATs and folders are made in memory; a file's
//              sectors are read from the file itself, when they are read.
//              A write into a file (fs_write_sec: a game's save) goes
//              straight to that file. Anything else (fs_create, fs_delete:
//              a new save file, a file deleted) is kept in memory, and when
//              the session ends the volume is read back with fsro and the
//              folder made to match it: new files made, changed ones
//              written, deleted ones deleted. Names starting with "." are
//              left out (macOS's .DS_Store and ._ files).
//   an image   (--card FILE) a disk image, read and written in place, like
//              the ones test/run.sh makes with hdiutil
//
// Everything here goes through SDL's file functions, so it is the same on
// macOS, Linux and Windows.
#include "sim.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsro.h"

#ifdef _WIN32
#include <stdlib.h>
#define realpath(p, r) _fullpath((r), (p), 4096)
#else
#include <limits.h>
#endif

#define SEC         512
#define PART_LBA    2048u               // where the partition starts, as on a card from the factory
#define RESERVED    32u                 // FAT32's reserved sectors: boot, FSInfo, their backups at 6 and 7
#define MAX_DEPTH   32

typedef struct {
    char *name;                         // as in the folder (UTF-8)
    char *rel;                          // its path on the card, "/"-separated, from the top
    char *host;                         // its path on this computer
    bool dir;
    uint64_t size;
    uint16_t date, time;                // FAT's
    int parent, child, next;            // the tree (-1: none)
    uint8_t sfn[11];                    // its 8.3 name
    bool lfn;                           // with a long name before it
    uint32_t first_clus, nclus;
    uint32_t entries;                   // a folder's 32-byte entries
    bool seen;                          // (card_sync)
} node_t;

typedef struct { uint32_t lba, nsec; int node; } span_t;   // a file's sectors

static struct {
    int mode;                           // 0 none, 1 a folder, 2 an image
    char root[4096];
    node_t *n;
    int nn, cap;
    uint32_t spc, nclus, used, fatsz, data_rel, meta_secs, total_secs;
    uint8_t *meta;                      // the volume's first meta_secs sectors
    uint8_t mbr[SEC];
    span_t *spans;
    int nspans;
    bool structural;                    // the FATs or a folder were written
    bool overlaid;                      // a sector is held in memory (card_sync has work)
    SDL_IOStream *img;
    uint64_t img_bytes;
} C;

// ---- sectors held in memory ------------------------------------------------------------
typedef struct ov { uint32_t lba; struct ov *next; uint8_t d[SEC]; } ov_t;
static ov_t *ov_bucket[4096];

static ov_t *ov_find(uint32_t lba) {
    for (ov_t *o = ov_bucket[lba & 4095]; o; o = o->next)
        if (o->lba == lba) return o;
    return NULL;
}

static void ov_put(uint32_t lba, const uint8_t *d) {
    ov_t *o = ov_find(lba);
    if (!o) {
        o = malloc(sizeof *o);
        if (!o) { fprintf(stderr, "card: out of memory\n"); exit(2); }
        o->lba = lba;
        o->next = ov_bucket[lba & 4095];
        ov_bucket[lba & 4095] = o;
    }
    memcpy(o->d, d, SEC);
    C.overlaid = true;
}

// ---- little helpers --------------------------------------------------------------------
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (!d) { fprintf(stderr, "card: out of memory\n"); exit(2); }
    memcpy(d, s, n);
    return d;
}

static char *join(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    char *s = malloc(na + nb + 2);
    if (!s) { fprintf(stderr, "card: out of memory\n"); exit(2); }
    memcpy(s, a, na);
    size_t k = na;
    if (na && a[na - 1] != '/' && a[na - 1] != '\\') s[k++] = '/';
    memcpy(s + k, b, nb + 1);
    return s;
}

// UTF-8 -> UTF-16 (a long name); an invalid byte is '?'. Returns the units, at most max.
static int utf16(const char *s, uint16_t *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (c >= 0x80 && !more) c = '?';
        else if (more) {
            c &= 0x3Fu >> more;
            for (int i = 0; i < more; i++) {
                if ((*p & 0xC0) != 0x80) { c = '?'; break; }
                c = c << 6 | (*p++ & 0x3Fu);
            }
        }
        if (c >= 0x10000) {
            if (n + 2 > max) break;
            c -= 0x10000;
            out[n++] = (uint16_t)(0xD800 | c >> 10);
            out[n++] = (uint16_t)(0xDC00 | (c & 0x3FF));
        } else out[n++] = (uint16_t)c;
    }
    return n;
}

// a path as fsro reads names: a character outside ASCII is '?'
static void ascii_path(const char *s, char *out, size_t max) {
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n + 1 < max) {
        if (*p < 0x80) out[n++] = (char)*p++;
        else {
            out[n++] = '?';
            p++;
            while ((*p & 0xC0) == 0x80) p++;
        }
    }
    out[n] = 0;
}

static void fat_date(SDL_Time t, uint16_t *date, uint16_t *time) {
    SDL_DateTime d;
    if (!SDL_TimeToDateTime(t, &d, true) || d.year < 1980) { *date = (0 << 9) | (1 << 5) | 1; *time = 0; return; }
    *date = (uint16_t)((d.year - 1980) << 9 | d.month << 5 | d.day);
    *time = (uint16_t)(d.hour << 11 | d.minute << 5 | d.second / 2);
}

// ---- the folder, read --------------------------------------------------------------------
static int add_node(const char *name, const char *rel, const char *host, bool dir, uint64_t size, SDL_Time mtime, int parent) {
    if (C.nn == C.cap) {
        C.cap = C.cap ? C.cap * 2 : 256;
        C.n = realloc(C.n, sizeof *C.n * (size_t)C.cap);
        if (!C.n) { fprintf(stderr, "card: out of memory\n"); exit(2); }
    }
    node_t *e = &C.n[C.nn];
    memset(e, 0, sizeof *e);
    e->name = dup_str(name);
    e->rel = dup_str(rel);
    e->host = dup_str(host);
    e->dir = dir;
    e->size = size;
    e->parent = parent;
    e->child = e->next = -1;
    fat_date(mtime, &e->date, &e->time);
    return C.nn++;
}

typedef struct { char **v; int n, cap; } names_t;

static SDL_EnumerationResult SDLCALL collect(void *ud, const char *dirname, const char *fname) {
    (void)dirname;
    names_t *l = ud;
    if (fname[0] == '.') return SDL_ENUM_CONTINUE;      // hidden: .DS_Store, ._ files, .git
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = realloc(l->v, sizeof *l->v * (size_t)l->cap);
        if (!l->v) return SDL_ENUM_FAILURE;
    }
    l->v[l->n++] = dup_str(fname);
    return SDL_ENUM_CONTINUE;
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void scan(int d, int depth) {
    names_t l = {0};
    if (!SDL_EnumerateDirectory(C.n[d].host, collect, &l)) fprintf(stderr, "card: can't list %s: %s\n", C.n[d].host, SDL_GetError());
    if (l.n) qsort(l.v, (size_t)l.n, sizeof *l.v, by_name);
    int last = -1;
    for (int i = 0; i < l.n; i++) {
        char *host = join(C.n[d].host, l.v[i]);
        char *rel = C.n[d].rel[0] ? join(C.n[d].rel, l.v[i]) : dup_str(l.v[i]);
        SDL_PathInfo info;
        if (SDL_GetPathInfo(host, &info) && (info.type == SDL_PATHTYPE_FILE || info.type == SDL_PATHTYPE_DIRECTORY)) {
            if (info.type == SDL_PATHTYPE_FILE && info.size >= 0xFFFFFFFFull)
                fprintf(stderr, "card: %s is left out: FAT32 holds files under 4 GB\n", rel);
            else if (info.type == SDL_PATHTYPE_DIRECTORY && depth >= MAX_DEPTH)
                fprintf(stderr, "card: %s is left out: folders %d deep at most\n", rel, MAX_DEPTH);
            else {
                int k = add_node(l.v[i], rel, host, info.type == SDL_PATHTYPE_DIRECTORY, info.type == SDL_PATHTYPE_FILE ? info.size : 0,
                                 info.modify_time, d);
                if (last < 0) C.n[d].child = k;
                else C.n[last].next = k;
                last = k;
            }
        }
        free(host);
        free(rel);
        free(l.v[i]);
    }
    free(l.v);
    for (int k = C.n[d].child; k >= 0; k = C.n[k].next)
        if (C.n[k].dir) scan(k, depth + 1);
}

// ---- 8.3 names ---------------------------------------------------------------------------
static bool sfn_char(unsigned c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c >= 128 ? false : strchr("!#$%&'()-@^_`{}~", (int)c) && c);
}

static bool sfn_taken(int d, int upto, const uint8_t *sfn) {
    for (int k = C.n[d].child; k >= 0 && k != upto; k = C.n[k].next)
        if (!memcmp(C.n[k].sfn, sfn, 11)) return true;
    return false;
}

static void make_sfn(int d, int k) {
    node_t *e = &C.n[k];
    const char *name = e->name, *dot = strrchr(name, '.');
    if (dot == name) dot = NULL;
    const size_t nb = dot ? (size_t)(dot - name) : strlen(name), ne = dot ? strlen(dot + 1) : 0;
    // as it is, or with only its case changed?
    bool fits = nb >= 1 && nb <= 8 && ne <= 3, upper = true;
    for (size_t i = 0; i < nb + (dot ? 1 + ne : 0) && fits; i++) {
        unsigned c = (unsigned char)name[i];
        if (dot && name + i == dot) continue;
        if (c >= 'a' && c <= 'z') { upper = false; c -= 32; }
        if (!sfn_char(c)) fits = false;
    }
    uint8_t s[11];
    memset(s, ' ', 11);
    if (fits) {
        for (size_t i = 0; i < nb; i++) s[i] = (uint8_t)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
        for (size_t i = 0; i < ne; i++) s[8 + i] = (uint8_t)(dot[1 + i] >= 'a' && dot[1 + i] <= 'z' ? dot[1 + i] - 32 : dot[1 + i]);
        if (!sfn_taken(d, k, s)) {
            memcpy(e->sfn, s, 11);
            e->lfn = !upper;                // the long name keeps its case
            return;
        }
    }
    // a basis from the name, and ~N
    uint8_t basis[8], ext[3];
    int bn = 0, en = 0;
    for (size_t i = 0; i < nb && bn < 6; i++) {
        unsigned c = (unsigned char)name[i];
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        basis[bn++] = (uint8_t)(sfn_char(c) ? c : '_');
        if (c >= 0xC0) while (i + 1 < nb && ((unsigned char)name[i + 1] & 0xC0) == 0x80) i++;
    }
    if (!bn) basis[bn++] = '_';
    for (size_t i = 0; i < ne && en < 3; i++) {
        unsigned c = (unsigned char)dot[1 + i];
        if (c == ' ') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        ext[en++] = (uint8_t)(sfn_char(c) ? c : '_');
        if (c >= 0xC0) while (i + 1 < ne && ((unsigned char)dot[2 + i] & 0xC0) == 0x80) i++;
    }
    for (int n = 1; n < 1000000; n++) {
        char tail[8];
        const int tn = snprintf(tail, sizeof tail, "~%d", n);
        const int keep = bn + tn > 8 ? 8 - tn : bn;
        memset(s, ' ', 11);
        memcpy(s, basis, (size_t)keep);
        memcpy(s + keep, tail, (size_t)tn);
        memcpy(s + 8, ext, (size_t)en);
        if (!sfn_taken(d, k, s)) break;
    }
    memcpy(e->sfn, s, 11);
    e->lfn = true;
}

static int lfn_entries(const node_t *e) {
    uint16_t u[260];
    return e->lfn ? (utf16(e->name, u, 255) + 12) / 13 : 0;
}

static uint8_t sfn_sum(const uint8_t *s) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + s[i]);
    return sum;
}

// ---- the volume ----------------------------------------------------------------------------
static uint8_t *meta_sec(uint32_t rel) { return C.meta + (size_t)rel * SEC; }
static uint32_t clus_rel(uint32_t c) { return C.data_rel + (c - 2) * C.spc; }

static void fat_set(uint32_t c, uint32_t v) {
    for (int f = 0; f < 2; f++) put32(meta_sec(RESERVED + (uint32_t)f * C.fatsz) + (size_t)c * 4, v);
}

static void dir_entry(uint8_t *p, const uint8_t *sfn, uint8_t attr, uint32_t clus, uint32_t size, uint16_t date, uint16_t time) {
    memcpy(p, sfn, 11);
    p[11] = attr;
    put16(p + 14, time);
    put16(p + 16, date);
    put16(p + 18, date);
    put16(p + 20, clus >> 16);
    put16(p + 22, time);
    put16(p + 24, date);
    put16(p + 26, clus & 0xFFFF);
    put32(p + 28, size);
}

static void write_dir(int d) {
    node_t *D = &C.n[d];
    uint8_t *p = meta_sec(clus_rel(D->first_clus));
    if (d) {                                // "." and ".."
        static const uint8_t dot[11] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
        static const uint8_t dotdot[11] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
        dir_entry(p, dot, 0x10, D->first_clus, 0, D->date, D->time);
        dir_entry(p + 32, dotdot, 0x10, D->parent > 0 ? C.n[D->parent].first_clus : 0, 0, D->date, D->time);
        p += 64;
    }
    for (int k = D->child; k >= 0; k = C.n[k].next) {
        node_t *e = &C.n[k];
        if (e->lfn) {
            uint16_t u[260];
            const int n = utf16(e->name, u, 255), pieces = (n + 12) / 13;
            const uint8_t sum = sfn_sum(e->sfn);
            static const uint8_t off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (int piece = pieces; piece >= 1; piece--) {
                memset(p, 0, 32);
                p[0] = (uint8_t)(piece | (piece == pieces ? 0x40 : 0));
                p[11] = 0x0F;
                p[13] = sum;
                for (int c = 0; c < 13; c++) {
                    const int at = (piece - 1) * 13 + c;
                    put16(p + off[c], at < n ? u[at] : at == n ? 0 : 0xFFFF);
                }
                p += 32;
            }
        }
        dir_entry(p, e->sfn, e->dir ? 0x10 : 0x20, e->first_clus, e->dir ? 0 : (uint32_t)e->size, e->date, e->time);
        p += 32;
    }
}

static int by_lba(const void *a, const void *b) {
    const span_t *x = a, *y = b;
    return x->lba < y->lba ? -1 : x->lba > y->lba;
}

static bool build(void) {
    // names and sizes
    uint64_t data_bytes = 0;
    for (int i = 0; i < C.nn; i++) {
        node_t *e = &C.n[i];
        if (e->dir) {
            e->entries = i ? 2 : 0;
            for (int k = e->child; k >= 0; k = C.n[k].next) {
                make_sfn(i, k);
                e->entries += 1 + (uint32_t)lfn_entries(&C.n[k]);
            }
        }
        data_bytes += e->dir ? e->entries * 32u : e->size;
    }
    C.spc = data_bytes > (32ull << 30) ? 64 : 8;            // 32K clusters past 32 GB, else 4K
    const uint32_t cb = C.spc * SEC;
    uint64_t used = 0;
    for (int i = 0; i < C.nn; i++) {
        node_t *e = &C.n[i];
        const uint64_t bytes = e->dir ? e->entries * 32u : e->size;
        e->nclus = (uint32_t)((bytes + cb - 1) / cb);
        if (e->dir && !e->nclus) e->nclus = 1;
        used += e->nclus;
    }
    uint64_t total = used + (used / 8 > 16384 ? used / 8 : 16384);   // room for new files
    if (total < 65536 + 16) total = 65536 + 16;                       // FAT32 needs 65,525 clusters or more
    if ((total * C.spc) >> 32) { fprintf(stderr, "card: the folder is too big for one volume\n"); return false; }
    C.nclus = (uint32_t)total;
    C.used = (uint32_t)used;
    C.fatsz = (C.nclus + 2 + 127) / 128;
    C.data_rel = RESERVED + 2 * C.fatsz;
    C.total_secs = C.data_rel + C.nclus * C.spc;
    // clusters: the folders first (they live in memory), then the files
    uint32_t c = 2, dir_clus = 0;
    for (int i = 0; i < C.nn; i++)
        if (C.n[i].dir) { C.n[i].first_clus = c; c += C.n[i].nclus; dir_clus += C.n[i].nclus; }
    C.meta_secs = C.data_rel + dir_clus * C.spc;
    C.meta = calloc(C.meta_secs, SEC);
    C.spans = calloc((size_t)C.nn + 1, sizeof *C.spans);
    if (!C.meta || !C.spans) { fprintf(stderr, "card: out of memory\n"); return false; }
    for (int i = 0; i < C.nn; i++) {
        node_t *e = &C.n[i];
        if (e->dir || !e->nclus) continue;
        e->first_clus = c;
        c += e->nclus;
        C.spans[C.nspans++] = (span_t){PART_LBA + clus_rel(e->first_clus), e->nclus * C.spc, i};
    }
    qsort(C.spans, (size_t)C.nspans, sizeof *C.spans, by_lba);
    // the FATs
    fat_set(0, 0x0FFFFFF8);
    fat_set(1, 0x0FFFFFFF);
    for (int i = 0; i < C.nn; i++) {
        const node_t *e = &C.n[i];
        for (uint32_t k = 0; k < e->nclus; k++) fat_set(e->first_clus + k, k + 1 < e->nclus ? e->first_clus + k + 1 : 0x0FFFFFFF);
    }
    // the boot sector, FSInfo, and their backups
    uint8_t *b = meta_sec(0);
    memcpy(b, "\xEB\x58\x90" "MSWIN4.1", 11);
    put16(b + 11, SEC);
    b[13] = (uint8_t)C.spc;
    put16(b + 14, RESERVED);
    b[16] = 2;
    b[21] = 0xF8;
    put16(b + 24, 63);
    put16(b + 26, 255);
    put32(b + 28, PART_LBA);
    put32(b + 32, C.total_secs);
    put32(b + 36, C.fatsz);
    put32(b + 44, 2);                       // the top folder's cluster
    put16(b + 48, 1);                       // FSInfo
    put16(b + 50, 6);                       // the backup
    b[64] = 0x80;
    b[66] = 0x29;
    put32(b + 67, 0x50415252);
    memcpy(b + 71, "PARROTSIM  FAT32   ", 19);
    b[510] = 0x55;
    b[511] = 0xAA;
    uint8_t *fi = meta_sec(1);
    put32(fi, 0x41615252);
    put32(fi + 484, 0x61417272);
    put32(fi + 488, C.nclus - C.used);
    put32(fi + 492, c);
    put32(fi + 508, 0xAA550000);
    memcpy(meta_sec(6), b, SEC);
    memcpy(meta_sec(7), fi, SEC);
    for (int i = 0; i < C.nn; i++)
        if (C.n[i].dir) write_dir(i);
    // the MBR: one FAT32 (LBA) partition
    uint8_t *pe = C.mbr + 446;
    pe[1] = 0xFE; pe[2] = 0xFF; pe[3] = 0xFF;
    pe[4] = 0x0C;
    pe[5] = 0xFE; pe[6] = 0xFF; pe[7] = 0xFF;
    put32(pe + 8, PART_LBA);
    put32(pe + 12, C.total_secs);
    C.mbr[510] = 0x55;
    C.mbr[511] = 0xAA;
    return true;
}

// ---- the files' own sectors -------------------------------------------------------------
static struct { int node; bool rw; SDL_IOStream *io; } fh[8];
static int fh_next;

static SDL_IOStream *file_io(int node, bool rw) {
    for (int i = 0; i < 8; i++)
        if (fh[i].io && fh[i].node == node) {
            if (rw && !fh[i].rw) { SDL_CloseIO(fh[i].io); fh[i].io = NULL; break; }
            return fh[i].io;
        }
    int i = fh_next++ % 8;
    if (fh[i].io) SDL_CloseIO(fh[i].io);
    fh[i].node = node;
    fh[i].rw = rw;
    fh[i].io = SDL_IOFromFile(C.n[node].host, rw ? "r+b" : "rb");
    return fh[i].io;
}

static const span_t *span_at(uint32_t lba) {
    int lo = 0, hi = C.nspans - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const span_t *s = &C.spans[mid];
        if (lba < s->lba) hi = mid - 1;
        else if (lba >= s->lba + s->nsec) lo = mid + 1;
        else return s;
    }
    return NULL;
}

static bool sector_read(uint32_t lba, uint8_t *buf) {
    if (C.mode == 2) {
        if ((uint64_t)lba * SEC + SEC > C.img_bytes) return false;
        return SDL_SeekIO(C.img, (Sint64)lba * SEC, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(C.img, buf, SEC) == SEC;
    }
    const ov_t *o = ov_find(lba);
    if (o) { memcpy(buf, o->d, SEC); return true; }
    memset(buf, 0, SEC);
    if (lba < PART_LBA) {
        if (!lba) memcpy(buf, C.mbr, SEC);
        return true;
    }
    const uint32_t rel = lba - PART_LBA;
    if (rel >= C.total_secs) return false;
    if (rel < C.meta_secs) { memcpy(buf, meta_sec(rel), SEC); return true; }
    const span_t *s = span_at(lba);
    if (!s) return true;                    // free space
    const node_t *e = &C.n[s->node];
    const uint64_t at = (uint64_t)(lba - s->lba) * SEC;
    if (at >= e->size) return true;
    const size_t n = e->size - at < SEC ? (size_t)(e->size - at) : SEC;
    SDL_IOStream *io = file_io(s->node, false);
    if (!io || SDL_SeekIO(io, (Sint64)at, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, buf, n) != n) {
        fprintf(stderr, "card: can't read %s: %s\n", e->host, SDL_GetError());
        return false;
    }
    return true;
}

static bool sector_write(uint32_t lba, const uint8_t *buf) {
    if (C.mode == 2) {
        if ((uint64_t)lba * SEC + SEC > C.img_bytes) return false;
        return SDL_SeekIO(C.img, (Sint64)lba * SEC, SDL_IO_SEEK_SET) >= 0 && SDL_WriteIO(C.img, buf, SEC) == SEC && SDL_FlushIO(C.img);
    }
    if (lba >= PART_LBA && lba - PART_LBA >= C.total_secs) return false;
    if (lba >= PART_LBA && lba - PART_LBA < C.meta_secs) {
        memcpy(meta_sec(lba - PART_LBA), buf, SEC);
        C.structural = true;
        return true;
    }
    const span_t *s = lba >= PART_LBA && !C.structural && !ov_find(lba) ? span_at(lba) : NULL;
    if (s) {                                // a file's own sector: into the file, now
        const node_t *e = &C.n[s->node];
        const uint64_t at = (uint64_t)(lba - s->lba) * SEC;
        if (at >= e->size) return true;     // past its end: nothing to keep
        const size_t n = e->size - at < SEC ? (size_t)(e->size - at) : SEC;
        SDL_IOStream *io = file_io(s->node, true);
        if (io && SDL_SeekIO(io, (Sint64)at, SDL_IO_SEEK_SET) >= 0 && SDL_WriteIO(io, buf, n) == n && SDL_FlushIO(io)) return true;
        fprintf(stderr, "card: can't write %s (%s): kept until the end\n", e->host, SDL_GetError());
    }
    ov_put(lba, buf);
    return true;
}

// ---- what host.c calls ------------------------------------------------------------------
int64_t card_pread(int fd, void *buf, size_t n, int64_t off) {
    (void)fd;
    if (n != SEC || off % SEC || !C.mode) return -1;
    return sector_read((uint32_t)(off / SEC), buf) ? SEC : -1;
}

int64_t card_pwrite(int fd, const void *buf, size_t n, int64_t off) {
    (void)fd;
    if (n != SEC || off % SEC || !C.mode) return -1;
    return sector_write((uint32_t)(off / SEC), buf) ? SEC : -1;
}

int card_fopen(const char *path, int flags) {
    (void)flags;
    return card_open(path) ? 3 : -1;
}

bool card_present(void) { return C.mode != 0; }

uint32_t card_mb(void) {
    if (C.mode == 2) return (uint32_t)(C.img_bytes >> 20);
    return (uint32_t)(((uint64_t)PART_LBA + C.total_secs) >> 11);
}

bool card_open(const char *path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || (info.type != SDL_PATHTYPE_DIRECTORY && info.type != SDL_PATHTYPE_FILE)) {
        fprintf(stderr, "--card: %s isn't a folder or a disk image\n", path);
        return false;
    }
    if (info.type == SDL_PATHTYPE_FILE) {
        C.img = SDL_IOFromFile(path, "r+b");
        if (!C.img) { fprintf(stderr, "--card: can't open %s: %s\n", path, SDL_GetError()); return false; }
        C.img_bytes = info.size;
        C.mode = 2;
        return true;
    }
    snprintf(C.root, sizeof C.root, "%s", path);
    add_node("", "", C.root, true, 0, info.modify_time, -1);
    scan(0, 0);
    if (!build()) return false;
    C.mode = 1;
    int files = 0;
    for (int i = 0; i < C.nn; i++) files += !C.n[i].dir;
    printf("card: %s as FAT32, %d files in %d folders, %u MB\n", C.root, files, C.nn - files, card_mb());
    return true;
}

// ---- the end: the folder made to match --------------------------------------------------
static fs_t sync_fs;
static uint32_t *by_clus;                   // file nodes with clusters, by first cluster
static int n_by_clus;

static int cmp_clus(const void *a, const void *b) {
    const uint32_t x = C.n[*(const int *)a].first_clus, y = C.n[*(const int *)b].first_clus;
    return x < y ? -1 : x > y;
}

static int original(uint32_t clus, const char *rel) {
    char a[1024];
    if (!clus) {                            // an empty file: by its path
        for (int i = 0; i < C.nn; i++) {
            if (C.n[i].dir || C.n[i].nclus) continue;
            ascii_path(C.n[i].rel, a, sizeof a);
            if (!strcmp(a, rel)) return i;
        }
        return -1;
    }
    int lo = 0, hi = n_by_clus - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, i = (int)by_clus[mid];
        if (C.n[i].first_clus < clus) lo = mid + 1;
        else if (C.n[i].first_clus > clus) hi = mid - 1;
        else {
            ascii_path(C.n[i].rel, a, sizeof a);
            return strcmp(a, rel) ? -1 : i;
        }
    }
    return -1;
}

static bool touched(const node_t *e) {      // a sector of it held in memory
    const uint32_t first = PART_LBA + clus_rel(e->first_clus);
    for (uint32_t k = 0; k < e->nclus * C.spc; k++)
        if (ov_find(first + k)) return true;
    return false;
}

// a file from the volume into the folder: an original over itself, a new one by its name
static void save_file(int i, const char *rel, fs_file_t *f) {
    char host[4096];
    if (i >= 0) snprintf(host, sizeof host, "%s", C.n[i].host);
    else {
        snprintf(host, sizeof host, "%s/%s", C.root, rel);
        for (char *p = host + strlen(C.root) + 1; *p; p++)
            if (strchr("?*:\"<>|\\", *p)) *p = '_';
        char *slash = strrchr(host, '/');   // (a folder made on the card: made here too)
        if (slash > host + strlen(C.root)) {
            *slash = 0;
            SDL_CreateDirectory(host);
            *slash = '/';
        }
    }
    SDL_IOStream *io = SDL_IOFromFile(host, "wb");
    if (!io) { fprintf(stderr, "card: can't write %s: %s\n", host, SDL_GetError()); return; }
    uint8_t buf[SEC];
    uint64_t left = f->size;
    bool ok = f->size == 0 || fs_open(&sync_fs, f) == FS_OK;
    for (uint32_t s = 0; ok && left; s++) {
        const uint32_t lba = fs_lba(&sync_fs, s);
        const size_t n = left < SEC ? (size_t)left : SEC;
        ok = lba && sector_read(lba, buf) && SDL_WriteIO(io, buf, n) == n;
        left -= n;
    }
    SDL_CloseIO(io);
    if (ok) printf("card: %s /%s\n", i < 0 ? "made" : "wrote", rel);
    else fprintf(stderr, "card: /%s couldn't be read back from the volume\n", rel);
}

static bool walk(const fs_file_t *dir, const char *prefix, int depth) {
    fs_dir_t *d = malloc(sizeof *d);
    if (!d || depth > MAX_DEPTH + 1 || fs_dir_open(&sync_fs, d, dir) != FS_OK) { free(d); return false; }
    fs_file_t e;
    bool ok = true;
    int r;
    while ((r = fs_dir_next(&sync_fs, d, &e)) > 0) {
        char rel[1024];
        snprintf(rel, sizeof rel, "%s%s%s", prefix, *prefix ? "/" : "", d->lname);
        if (e.is_dir) {
            if (!walk(&e, rel, depth + 1)) ok = false;
            continue;
        }
        const int i = original(e.first_clus, rel);
        if (i >= 0) {
            C.n[i].seen = true;
            if (C.n[i].size == e.size && !touched(&C.n[i])) continue;   // as it was (or written already)
        }
        save_file(i, rel, &e);
    }
    free(d);
    return ok && r == 0;
}

void card_sync(void) {
    if (C.mode != 1 || (!C.structural && !C.overlaid)) return;
    for (int i = 0; i < 8; i++)
        if (fh[i].io) { SDL_CloseIO(fh[i].io); fh[i].io = NULL; }
    by_clus = malloc(sizeof *by_clus * (size_t)(C.nn + 1));
    n_by_clus = 0;
    for (int i = 0; i < C.nn; i++)
        if (!C.n[i].dir && C.n[i].nclus) by_clus[n_by_clus++] = (uint32_t)i;
    qsort(by_clus, (size_t)n_by_clus, sizeof *by_clus, cmp_clus);
    if (fs_mount(&sync_fs, sector_read) != FS_OK || !walk(NULL, "", 0)) {
        fprintf(stderr, "card: the volume can't be read back, so the folder is left as it is\n");
        return;
    }
    for (int i = 0; i < C.nn; i++)
        if (!C.n[i].dir && !C.n[i].seen) {
            if (SDL_RemovePath(C.n[i].host)) printf("card: deleted /%s\n", C.n[i].rel);
            else fprintf(stderr, "card: can't delete %s: %s\n", C.n[i].host, SDL_GetError());
        }
    C.structural = C.overlaid = false;
}

// ---- --open ------------------------------------------------------------------------------
char *card_resolve_open(const char *open, const char **card) {
    static char card_dir[4096];
    char full[4096], top[4096], *out = malloc(4096);
    SDL_PathInfo info;
    if (!out) exit(2);
    if (SDL_GetPathInfo(open, &info) && info.type == SDL_PATHTYPE_FILE && realpath(open, full)) {
        if (!*card) {                       // the file's folder is the card
            char *slash = strrchr(full, '/');
#ifdef _WIN32
            char *bs = strrchr(full, '\\');
            if (!slash || (bs && bs > slash)) slash = bs;
#endif
            if (!slash) { snprintf(out, 4096, "/%s", full); return out; }
            *slash = 0;
            snprintf(card_dir, sizeof card_dir, "%s", full[0] ? full : "/");
            *card = card_dir;
            ascii_path(slash + 1, out + 1, 4095);
            out[0] = '/';
            return out;
        }
        if (realpath(*card, top)) {
            const size_t n = strlen(top);
            if (!strncmp(full, top, n) && (full[n] == '/' || full[n] == '\\')) {
                ascii_path(full + n, out, 4096);
                for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
                return out;
            }
        }
        fprintf(stderr, "--open: %s is on this computer but not in the card's folder %s\n", open, *card);
        exit(2);
    }
    out[0] = '/';
    ascii_path(open[0] == '/' ? open + 1 : open, out + 1, 4095);
    return out;
}
