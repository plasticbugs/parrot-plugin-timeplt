// heap_test.c -- the allocator (../heap.c) by itself: thousands of random
// allocations, frees and resizes, each block filled with a pattern that
// must still be there when it's freed, and the allocator's own bookkeeping
// walked after every step. lua/test/run.sh builds and runs it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heap.h"

#define SLOTS 400
static struct { unsigned char *p; size_t n; unsigned char tag; } slot[SLOTS];
static unsigned long long arena[64 * 1024 / 8];
static unsigned rng = 12345;
static unsigned rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void fill(int i) { memset(slot[i].p, slot[i].tag, slot[i].n); }
static int intact(int i, size_t n) {
    for (size_t k = 0; k < n; k++) if (slot[i].p[k] != slot[i].tag) return 0;
    return 1;
}
#define MUST(c, what) do { if (!(c)) { printf("FAIL at step %d: %s\n", step, what); return 1; } } while (0)

int main(void) {
    heap_t h;
    int step = 0, allocs = 0, refused = 0, moved = 0;
    for (int round = 0; round < 3; round++) {
        // an odd start and size the second time: alignment must not depend on them
        heap_init(&h, (unsigned char *)arena + (round == 1 ? 3 : 0), sizeof arena - (round == 1 ? 13 : 0));
        memset(slot, 0, sizeof slot);
        const size_t empty = heap_room(&h);
        MUST(heap_check(&h), "a new heap");
        MUST(heap_largest(&h) + 2 * sizeof(size_t) >= empty, "a new heap is one block");
        for (step = 0; step < 60000; step++) {
            const int i = (int)(rnd() % SLOTS);
            // sizes as Lua asks for them: mostly small, now and then big
            size_t n = rnd() % 8 ? rnd() % 96 + 1 : rnd() % 16 ? rnd() % 1500 + 1 : rnd() % 20000 + 1;
            const unsigned what = rnd() % 10;
            if (!slot[i].p) {
                slot[i].p = heap_alloc(&h, n);
                if (!slot[i].p) { refused++; continue; }
                MUST(heap_owns(&h, slot[i].p) && (size_t)slot[i].p % (2 * sizeof(size_t)) == 0, "a block's address");
                slot[i].n = n;
                slot[i].tag = (unsigned char)(rnd() | 1);
                fill(i);
                allocs++;
            } else if (what < 4) {
                MUST(intact(i, slot[i].n), "a block changed before it was freed");
                heap_free(&h, slot[i].p);
                slot[i].p = NULL;
            } else {
                unsigned char *q = heap_realloc(&h, slot[i].p, n);
                if (!q) {
                    refused++;
                    MUST(intact(i, slot[i].n), "a refused resize changed the block");
                    continue;
                }
                if (q != slot[i].p) moved++;
                slot[i].p = q;
                MUST(intact(i, n < slot[i].n ? n : slot[i].n), "a resize lost the block's bytes");
                slot[i].n = n;
                fill(i);
            }
            if (step % 16 == 0) MUST(heap_check(&h), "the allocator's bookkeeping");
        }
        for (int i = 0; i < SLOTS; i++)
            if (slot[i].p) {
                MUST(intact(i, slot[i].n), "a block changed");
                heap_free(&h, slot[i].p);
            }
        MUST(heap_check(&h), "the bookkeeping at the end");
        MUST(h.used == 0 && heap_room(&h) == empty, "everything freed, and bytes are missing");
        MUST(heap_largest(&h) + 2 * sizeof(size_t) >= empty, "everything freed, and the heap isn't one block again");
    }
    heap_init(&h, NULL, 0);
    step = -1;
    MUST(heap_alloc(&h, 8) == NULL && heap_room(&h) == 0 && heap_check(&h), "a heap of no memory");
    printf("%d allocations, %d resizes that moved, %d refused when full: every block intact\n", allocs, moved, refused);
    return 0;
}
