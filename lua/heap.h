// heap.h -- the memory Lua's values live in: an allocator over one block
// of memory, with nothing from the C library. The runtime keeps two, one in
// the chip's RAM and one in the PSRAM (main.c).
//
// Plain C: the cart, the simulator and the tests share it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_BINS 64

typedef struct heap_free heap_free_t;
typedef struct {
    uint8_t *base;                      // the block, and its size
    size_t size;
    size_t used, peak;                  // bytes handed out now (with their headers), and the most there has been
    heap_free_t *bin[HEAP_BINS];        // free chunks by size
    uint64_t map;                       // bins that aren't empty
} heap_t;

// A heap over mem[0..size). The memory isn't touched beyond the allocator's
// own few words, so a big block starts at no cost.
void heap_init(heap_t *h, void *mem, size_t size);
void *heap_alloc(heap_t *h, size_t n);              // NULL: no room
void heap_free(heap_t *h, void *p);
// p made n bytes, in place when the room after it is free. NULL: no room,
// and p is as it was.
void *heap_realloc(heap_t *h, void *p, size_t n);
bool heap_owns(const heap_t *h, const void *p);
size_t heap_room(const heap_t *h);                  // bytes free in all (not one block's worth)
size_t heap_largest(const heap_t *h);               // the largest block heap_alloc() could give now
// Walks every chunk: false if the allocator's own words are damaged (tests).
bool heap_check(const heap_t *h);
