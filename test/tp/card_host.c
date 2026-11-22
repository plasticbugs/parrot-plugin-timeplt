// card_host.c -- tpcard on a disk image (test/tp/card.sh makes them): the
// ROMs found as the plugin finds them, in the room the plugin gives them
// (src/main.c: the card's state at the start of tpcv's room, the reads
// after it, the graphics at its end), and, with TP_SET a folder of the
// set's files, checked byte for byte against them; then the board run ten
// seconds from them. (Donkey Kong's.)
//   card_host IMAGE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tp.h"
#include "tpay.h"
#include "tpcard.h"
#include "tpcv.h"
#include "tprun.h"

static int fd;
static bool rd(uint32_t lba, uint8_t *buf) { return pread(fd, buf, 512, (off_t)lba * 512) == 512; }
static int ticks;
static void tick(void) { ticks++; }

int main(int argc, char **argv) {
    static tpcv_t cv;
    tpcv_init(&cv);
    size_t room;
    uint8_t *scratch = tpcv_scratch(&room);     // as the plugin
    if (argc < 2 || (fd = open(argv[1], O_RDONLY)) < 0) { perror("image"); return 2; }
    fs_t *fs = (fs_t *)(void *)scratch;
    size_t fs_room = (sizeof(fs_t) + 7) & ~(size_t)7;
    tp_rom_gfx = scratch + room - TP_GFX_BYTES;
    int r = fs_mount(fs, rd);
    if (r != FS_OK) { printf("mount: %d\n", r); return 1; }
    uint32_t found = tpcard_load_roms(fs, scratch + fs_room, (uint32_t)(room - fs_room - TP_GFX_BYTES), tick);
    int missing = 0;
    for (int i = 0; i < TP_ROM_FILES; i++) missing += !(found & 1u << i);
    printf("%s: found %03X (%d missing), %d sectors read, %u bytes for the reads\n", fs_type_name(fs), found, missing,
           ticks, (unsigned)(room - fs_room - TP_GFX_BYTES));
    if (found != TPROMS_ALL) return 1;
    const char *set = getenv("TP_SET");
    for (int i = 0; set && i < TP_ROM_FILES; i++) {
        const tp_rom_file_t *f = &tp_rom_files[i];
        char path[1024];
        static uint8_t want[0x2000];
        snprintf(path, sizeof path, "%s/%s", set, f->name);
        FILE *fp = fopen(path, "rb");
        const uint8_t *at = f->at & TP_GFX_AT ? tp_rom_gfx + (f->at & ~TP_GFX_AT) : (const uint8_t *)&tp_rom_store + f->at;
        if (!fp || fread(want, 1, f->size, fp) != f->size || memcmp(at, want, f->size)) {
            printf("%s isn't right in its place\n", f->name);
            return 1;
        }
        fclose(fp);
    }
    if (set) printf("every ROM found is right, byte for byte\n");
    static tp_roms_t roms;
    tproms_roms(&roms);
    tpay_reset();
    tp_init(&roms);                             // (the graphics decoded: the room is the picture's from here)
    tpcv_prepare();
    static tprun_t run;
    static uint8_t list[TPCV_LIST_MAX];
    tprun_start(&run, &cv);
    for (int l = 0; l < 600; l++) tprun_list(&run, list);
    static uint8_t px[TPCV_H * TPCV_W];
    tpcv_screen(&cv, px);
    uint32_t lit = 0;
    for (int i = 0; i < TPCV_H * TPCV_W; i++) lit += px[i] != px[0];
    printf("ran 10 s: %u frames, %u pictures, %u of the console's pixels unlike the first, AY queue max %u\n",
           tp_frames, run.pictures, lit, tpay_queue_max);
    return lit < 1000;
}
