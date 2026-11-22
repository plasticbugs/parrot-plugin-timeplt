// card.h -- the SD card for the Lua runtime: whole files by their paths,
// over the SDK's cvsd and fsro. Every call returns NULL when it worked, or
// what went wrong as a short sentence.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cvsd.h"

#define CARD_CREATE_MAX (1024u * 1024u)     // the largest file card_create() makes

// Where the file system keeps its state (an fs_t), before anything else.
void card_memory(fs_t *memory);
// Called every few sectors of a long read, so RESET keeps working.
void card_set_poll(void (*poll)(void));

// The card started and its file system found, the first time it's needed.
const char *card_mount(void);
// The mounted card, for code that uses fsro itself (the editor): NULL if
// there's none. failed: the last use hit an error, so it's started again.
fs_t *card_fs(bool failed);
// The same for the file the menu launched the plugin with: *f is that file.
const char *card_mount_launched(const cvl_t *b, fs_file_t *f);

const char *card_find(const char *path, fs_file_t *e);
// n bytes of the file from offset off (the caller checks them against e->size).
const char *card_read(const fs_file_t *e, uint32_t off, uint8_t *dst, uint32_t n);
// The same, written in place. A file never changes size.
const char *card_write(const fs_file_t *e, uint32_t off, const uint8_t *src, uint32_t n);
// The file at path, made with size bytes of zeros if it isn't there
// (*made). Its folder must be there already.
const char *card_create(const char *path, uint32_t size, fs_file_t *e, bool *made);
// Each entry of a folder, but the ones a computer hides: name is its full
// name. fn returns false to stop.
typedef bool (*card_list_fn)(const char *name, const fs_file_t *e, void *ctx);
const char *card_list(const char *path, card_list_fn fn, void *ctx);
