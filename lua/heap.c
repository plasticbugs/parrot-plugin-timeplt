// heap.c -- see heap.h.
//
// Chunks with a one-word header, as dlmalloc lays them out. The header is
// the chunk's size (a multiple of ALIGN) with two flags: this chunk is in
// use, and the one before it is. A free chunk also keeps its size in its
// last word, so the chunk after it can find its start and join it, and two
// pointers that link it into its bin. Small chunks have a bin for each
// size; larger ones share a bin for each power of two and a half.
#include "heap.h"

#include <string.h>

#define WORD    sizeof(size_t)
#define ALIGN   (2 * WORD)              // 8 on the cart, 16 on a 64-bit computer
#define MINCHUNK (4 * WORD)             // header, two links, the size at the end
#define USED    ((size_t)1)
#define PREV_USED ((size_t)2)
#define FLAGS   ((size_t)3)
#define SMALL_BINS 32                   // chunks of MINCHUNK .. MINCHUNK + 31 * ALIGN, one size each

struct heap_free {
    size_t head;
    heap_free_t *next, *prev;
};

static inline size_t csize(const void *c) { return *(const size_t *)c & ~FLAGS; }
static inline size_t *head(void *c) { return (size_t *)c; }
static inline void set_foot(void *c, size_t n) { *(size_t *)((uint8_t *)c + n - WORD) = n; }

static int bin_of(size_t n) {
    size_t k = (n - MINCHUNK) / ALIGN;
    if (k < SMALL_BINS) return (int)k;
    // two bins for each power of two past the small ones
    int b = SMALL_BINS;
    size_t top = (MINCHUNK + SMALL_BINS * ALIGN) * 2;
    while (b < HEAP_BINS - 2 && n >= top) {
        b += 2;
        top *= 2;
    }
    if (n >= top / 4 * 3 && b < HEAP_BINS - 1) b++;
    return b;
}

static void link_free(heap_t *h, void *c, size_t n) {
    heap_free_t *f = c;
    int b = bin_of(n);
    set_foot(c, n);
    f->prev = NULL;
    f->next = h->bin[b];
    if (f->next) f->next->prev = f;
    h->bin[b] = f;
    h->map |= (uint64_t)1 << b;
}

static void unlink_free(heap_t *h, heap_free_t *f) {
    int b = bin_of(csize(f));
    if (f->prev) f->prev->next = f->next;
    else {
        h->bin[b] = f->next;
        if (!f->next) h->map &= ~((uint64_t)1 << b);
    }
    if (f->next) f->next->prev = f->prev;
}

// the first chunk's header sits one word below an ALIGN boundary, so that
// what follows each header is aligned
static uint8_t *first_chunk(const heap_t *h) {
    uintptr_t a = ((uintptr_t)h->base + WORD + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    return (uint8_t *)(a - WORD);
}

void heap_init(heap_t *h, void *mem, size_t size) {
    memset(h, 0, sizeof *h);
    h->base = mem;
    h->size = size;
    if (!mem || size < 4 * MINCHUNK) {
        h->size = 0;
        return;
    }
    uint8_t *c = first_chunk(h);
    size_t n = ((size_t)((uint8_t *)mem + size - c) - WORD) & ~(size_t)(ALIGN - 1);   // a word kept for the end mark
    *head(c) = n | PREV_USED;
    link_free(h, c, n);
    *head(c + n) = USED;                // the end: a used chunk of no size
}

bool heap_owns(const heap_t *h, const void *p) {
    return h->size && (const uint8_t *)p >= h->base && (const uint8_t *)p < h->base + h->size;
}

static size_t chunk_for(size_t n) {
    size_t need = (n + WORD + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    return need < MINCHUNK ? MINCHUNK : need;
}

// cut chunk c (n bytes, in use) down to need, and free the rest
static void trim(heap_t *h, void *c, size_t n, size_t need) {
    if (n - need < MINCHUNK) return;
    *head(c) = need | USED | (*head(c) & PREV_USED);
    uint8_t *r = (uint8_t *)c + need;
    size_t rn = n - need;
    uint8_t *after = r + rn;
    if (!(*head(after) & USED)) {       // join the free chunk after it
        rn += csize(after);
        unlink_free(h, (heap_free_t *)after);
    }
    *head(r) = rn | PREV_USED;
    link_free(h, r, rn);
    *head(r + rn) &= ~PREV_USED;
    h->used -= n - need;
}

void *heap_alloc(heap_t *h, size_t n) {
    if (!h->size || n > h->size) return NULL;
    size_t need = chunk_for(n);
    heap_free_t *f = NULL;
    for (int b = bin_of(need); b < HEAP_BINS && !f; b++) {
        uint64_t m = h->map >> b;
        if (!m) return NULL;
        while (!(m & 1)) { m >>= 1; b++; }
        for (heap_free_t *q = h->bin[b]; q; q = q->next)
            if (csize(q) >= need) { f = q; break; }
    }
    if (!f) return NULL;
    size_t got = csize(f);
    unlink_free(h, f);
    *head(f) = got | USED | PREV_USED;  // (the chunk before a free one is always in use)
    *head((uint8_t *)f + got) |= PREV_USED;
    h->used += got;
    trim(h, f, got, need);
    if (h->used > h->peak) h->peak = h->used;
    return (uint8_t *)f + WORD;
}

void heap_free(heap_t *h, void *p) {
    if (!p) return;
    uint8_t *c = (uint8_t *)p - WORD;
    size_t n = csize(c);
    h->used -= n;
    uint8_t *after = c + n;
    if (!(*head(after) & USED)) {
        n += csize(after);
        unlink_free(h, (heap_free_t *)after);
    }
    if (!(*head(c) & PREV_USED)) {
        size_t pn = *(size_t *)(c - WORD);
        c -= pn;
        n += pn;
        unlink_free(h, (heap_free_t *)c);
    }
    *head(c) = n | PREV_USED;
    link_free(h, c, n);
    *head(c + n) &= ~PREV_USED;
}

void *heap_realloc(heap_t *h, void *p, size_t n) {
    if (!p) return heap_alloc(h, n);
    uint8_t *c = (uint8_t *)p - WORD;
    size_t have = csize(c), need = chunk_for(n);
    if (n > h->size) return NULL;
    if (need <= have) {
        trim(h, c, have, need);
        return p;
    }
    uint8_t *after = c + have;
    if (!(*head(after) & USED) && have + csize(after) >= need) {     // grow into the free chunk after it
        size_t an = csize(after);
        unlink_free(h, (heap_free_t *)after);
        *head(c) = (have + an) | USED | (*head(c) & PREV_USED);
        *head(c + have + an) |= PREV_USED;
        h->used += an;
        trim(h, c, have + an, need);
        if (h->used > h->peak) h->peak = h->used;
        return p;
    }
    void *q = heap_alloc(h, n);
    if (!q) return NULL;
    memcpy(q, p, have - WORD);
    heap_free(h, p);
    return q;
}

size_t heap_room(const heap_t *h) {
    if (!h->size) return 0;
    size_t all = (size_t)(h->base + h->size - first_chunk(h)) - WORD;
    return all - h->used;
}

size_t heap_largest(const heap_t *h) {
    size_t best = 0;
    for (int b = HEAP_BINS - 1; b >= 0 && !best; b--)
        for (heap_free_t *q = h->bin[b]; q; q = q->next)
            if (csize(q) > best) best = csize(q);
    return best > WORD ? best - WORD : 0;
}

bool heap_check(const heap_t *h) {
    if (!h->size) return true;
    uint8_t *c = first_chunk(h), *end = h->base + h->size;
    size_t used = 0, free_chunks = 0, binned = 0;
    bool prev_used = true;
    for (;;) {
        if (c + WORD > end) return false;
        size_t n = csize(c);
        if (!!(*head(c) & PREV_USED) != prev_used) return false;
        if (n == 0) break;                              // the end mark
        if (n < MINCHUNK || n % ALIGN || n > (size_t)(end - c)) return false;
        prev_used = *head(c) & USED;
        if (prev_used) used += n;
        else {
            if (*(size_t *)(c + n - WORD) != n) return false;
            free_chunks++;
        }
        c += n;
    }
    if (!(*head(c) & USED) || used != h->used) return false;
    for (int b = 0; b < HEAP_BINS; b++) {
        if (!h->bin[b] != !(h->map >> b & 1)) return false;
        for (heap_free_t *q = h->bin[b]; q; q = q->next) {
            if ((*head(q) & USED) || bin_of(csize(q)) != b) return false;
            if (q->next && q->next->prev != q) return false;
            binned++;
        }
    }
    return binned == free_chunks;
}
