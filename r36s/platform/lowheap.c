// The game's heap below 4 GB (lowheap.h).
//
// One region is reserved at start-up, as large as fits between the brk heap
// (with room left for glibc's own) and 4 GB. It is mapped NORESERVE, so
// only what is touched costs memory, and free space well above the
// highest block in use is handed back to the kernel.
//
// Chunks carry boundary tags as dlmalloc's do: a 16-byte header (the
// previous chunk's size, valid when that one is free, then this chunk's
// size with an in-use bit and a previous-in-use bit), and payloads
// aligned to 16. Free neighbours are always merged, and a free chunk
// next to the top merges into it. Free chunks sit in 128 bins: exact
// 16-byte classes below 1 KB, then four bins per power of two, the last
// holding everything from 56 MB. A bitmap finds the next bin with
// something in it. One mutex covers the heap; the game's threads allocate
// rarely next to the main thread.
//
// A pointer that isn't the heap's (glibc's strdup, say, freed by the game)
// goes to glibc's functions.

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lowheap.h"

#if !defined(__aarch64__)

// A 32-bit build's addresses all fit a slot: the game uses glibc's malloc,
// and this only reports it.
#include <malloc.h>

void lowheap_stats(struct lowheap_stats* s)
{
    struct mallinfo mi = mallinfo();
    memset(s, 0, sizeof *s);
    s->base = (uintptr_t)sbrk(0) - (uintptr_t)mi.arena;
    s->top = s->end = (uintptr_t)sbrk(0);
    s->used = (size_t)mi.uordblks + (size_t)mi.hblkhd;
    s->free = (size_t)mi.fordblks;
    s->largest = (size_t)mi.fordblks;
    s->pieces = (size_t)mi.ordblks;
    s->blocks = 0;
}

int lowheap_owns(const void* p)
{
    (void)p;
    return 0;
}

#else

typedef struct Chunk {
    size_t prev_size;     // the previous chunk's size, when it is free
    size_t head;          // size | INUSE | PINUSE
    struct Chunk* next;   // free chunks only
    struct Chunk* prev;
} Chunk;

#define HDR      ((size_t)16)
#define MINCHUNK ((size_t)32)
#define INUSE    ((size_t)1)
#define PINUSE   ((size_t)2)
#define FLAGS    ((size_t)3)
#define NBINS    128

#define CSIZE(c)   ((c)->head & ~FLAGS)
#define AT(p, off) ((Chunk*)((uint8_t*)(p) + (off)))

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  s_once = PTHREAD_ONCE_INIT;
static uint8_t* s_base;
static uint8_t* s_end;
static uint8_t* s_top;          // the first byte no chunk uses
static uint8_t* s_touched;      // the highest byte maybe holding memory
static bool     s_topPinuse = true;
static Chunk*   s_bins[NBINS];
static uint64_t s_map[NBINS / 64];
static size_t   s_used, s_blocks;

void* __real_malloc(size_t n);
void  __real_free(void* p);
void* __real_calloc(size_t n, size_t m);
void* __real_realloc(void* p, size_t n);
void* __real_memalign(size_t a, size_t n);
size_t __real_malloc_usable_size(void* p);

// ------------------------------------------------------------- region

static void reserve(void)
{
    // Leave 256 MB above the brk heap for glibc's malloc (SDL, the GL
    // driver); past that, glibc moves to mmap on its own.
    uintptr_t lo = ((uintptr_t)sbrk(0) + (256u << 20) + 0xFFFFFu) & ~(uintptr_t)0xFFFFF;
    const uintptr_t hi = 0xFFF00000u;
    static const uintptr_t bases[] = { 0, 0x40000000u, 0x80000000u };
    if (lo < 0x10000000u) lo = 0x10000000u;
    for (unsigned b = 0; b < sizeof bases / sizeof bases[0]; b++) {
        uintptr_t base = bases[b] ? bases[b] : lo;
        if (base < lo || base >= hi) continue;
        for (size_t size = (hi - base) & ~(size_t)0xFFFFF; size >= (64u << 20);
             size = (size / 4 * 3) & ~(size_t)0xFFFFF) {
            void* p = mmap((void*)base, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (p == MAP_FAILED) continue;
            if ((uintptr_t)p + size <= 0x100000000ull) {
                s_base = s_top = s_touched = (uint8_t*)p;
                s_end = s_base + size;
                return;
            }
            munmap(p, size);
        }
    }
    static const char msg[] = "R36S: no room below 4 GB for the game's heap\n";
    write(2, msg, sizeof msg - 1);
    abort();
}

static void init(void) { reserve(); }

int lowheap_owns(const void* p)
{
    return (const uint8_t*)p >= s_base && (const uint8_t*)p < s_end;
}

// --------------------------------------------------------------- bins

static unsigned bin_of(size_t size)
{
    if (size < 1024) return (unsigned)(size >> 4);
    unsigned k = 63u - (unsigned)__builtin_clzll((unsigned long long)size);
    unsigned b = 64u + (k - 10u) * 4u + (unsigned)((size >> (k - 2u)) & 3u);
    return b < NBINS ? b : NBINS - 1;
}

static void bin_insert(Chunk* c)
{
    unsigned b = bin_of(CSIZE(c));
    c->prev = NULL;
    c->next = s_bins[b];
    if (c->next) c->next->prev = c;
    s_bins[b] = c;
    s_map[b >> 6] |= 1ull << (b & 63);
}

static void bin_unlink(Chunk* c)
{
    if (c->prev) c->prev->next = c->next;
    else {
        unsigned b = bin_of(CSIZE(c));
        s_bins[b] = c->next;
        if (!c->next) s_map[b >> 6] &= ~(1ull << (b & 63));
    }
    if (c->next) c->next->prev = c->prev;
}

// The first free chunk of at least `n` bytes: first fit in n's own bin
// (a range below 1 KB is one size), then the head of the next bin up,
// whose every chunk is larger than n.
static Chunk* bin_find(size_t n)
{
    unsigned b = bin_of(n);
    for (Chunk* c = s_bins[b]; c; c = c->next)
        if (CSIZE(c) >= n) return c;
    unsigned start = b + 1;
    for (unsigned w = start >> 6; w < NBINS / 64; w++) {
        uint64_t m = s_map[w];
        if (w == start >> 6) m &= ~0ull << (start & 63);
        if (m) return s_bins[w * 64 + (unsigned)__builtin_ctzll(m)];
    }
    return NULL;
}

// ------------------------------------------------------------- chunks

static void set_next_pinuse(Chunk* c, bool on)
{
    Chunk* nx = AT(c, CSIZE(c));
    if ((uint8_t*)nx == s_top) s_topPinuse = on;
    else if (on) nx->head |= PINUSE;
    else nx->head &= ~PINUSE;
}

static void trim_top(void)
{
    // Give back what lies more than 32 MB above the top, keeping 8 MB.
    if ((size_t)(s_touched - s_top) > (32u << 20)) {
        uint8_t* from = (uint8_t*)(((uintptr_t)s_top + (8u << 20) + 0xFFFu) & ~(uintptr_t)0xFFF);
        if (from < s_touched) {
            madvise(from, (size_t)(s_touched - from), MADV_DONTNEED);
            s_touched = from;
        }
    }
}

// Frees in-use chunk c, merging it with free neighbours or the top.
static void free_chunk(Chunk* c)
{
    size_t size = CSIZE(c);
    s_used -= size;
    s_blocks--;
    if (!(c->head & PINUSE)) {
        size_t psize = c->prev_size;
        Chunk* pc = AT(c, -(ptrdiff_t)psize);
        bin_unlink(pc);
        c = pc;
        size += psize;
    }
    Chunk* nx = AT(c, size);
    if ((uint8_t*)nx == s_top) {
        s_top = (uint8_t*)c;
        s_topPinuse = (c->head & PINUSE) != 0;
        trim_top();
        return;
    }
    if (!(nx->head & INUSE)) {
        bin_unlink(nx);
        size += CSIZE(nx);
    }
    c->head = size | (c->head & PINUSE);
    nx = AT(c, size);
    nx->prev_size = size;
    nx->head &= ~PINUSE;
    bin_insert(c);
}

// Shrinks in-use chunk c to n bytes, freeing the rest when it is a chunk.
static void shrink_chunk(Chunk* c, size_t n)
{
    size_t size = CSIZE(c);
    if (size - n < MINCHUNK) return;
    Chunk* rest = AT(c, n);
    c->head = n | (c->head & FLAGS);
    rest->head = (size - n) | INUSE | PINUSE;
    // The rest is a block in use for a moment (the bytes are already
    // counted), which free_chunk then takes off.
    s_blocks++;
    free_chunk(rest);
}

static Chunk* alloc_chunk(size_t n)
{
    Chunk* c = bin_find(n);
    if (c) {
        bin_unlink(c);
        size_t size = CSIZE(c);
        if (size - n >= MINCHUNK) {
            Chunk* rest = AT(c, n);
            rest->head = (size - n) | PINUSE;
            c->head = n | (c->head & PINUSE) | INUSE;
            AT(rest, size - n)->prev_size = size - n;
            bin_insert(rest);
        } else {
            c->head |= INUSE;
            set_next_pinuse(c, true);
        }
    } else {
        if ((size_t)(s_end - s_top) < n) return NULL;
        c = (Chunk*)s_top;
        c->head = n | INUSE | (s_topPinuse ? PINUSE : 0);
        s_top += n;
        s_topPinuse = true;
        if (s_top > s_touched) s_touched = s_top;
    }
    s_used += CSIZE(c);
    s_blocks++;
    return c;
}

static bool request_size(size_t req, size_t* n)
{
    if (req > ((size_t)1 << 40)) return false;
    size_t s = (req + HDR + 15) & ~(size_t)15;
    *n = s < MINCHUNK ? MINCHUNK : s;
    return true;
}

// ---------------------------------------------------------- the API

void* __wrap_malloc(size_t req)
{
    size_t n;
    pthread_once(&s_once, init);
    if (!request_size(req, &n)) {
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&s_lock);
    Chunk* c = alloc_chunk(n);
    pthread_mutex_unlock(&s_lock);
    if (!c) {
        errno = ENOMEM;
        return NULL;
    }
    return (uint8_t*)c + HDR;
}

void __wrap_free(void* p)
{
    if (!p) return;
    if (!lowheap_owns(p)) {
        __real_free(p);
        return;
    }
    pthread_mutex_lock(&s_lock);
    free_chunk(AT(p, -(ptrdiff_t)HDR));
    pthread_mutex_unlock(&s_lock);
}

void* __wrap_calloc(size_t n, size_t m)
{
    size_t total;
    if (__builtin_mul_overflow(n, m, &total)) {
        errno = ENOMEM;
        return NULL;
    }
    void* p = __wrap_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* __wrap_memalign(size_t align, size_t req)
{
    size_t n;
    if (align <= 16) return __wrap_malloc(req);
    if (align & (align - 1)) align = (size_t)1 << (64 - __builtin_clzll(align));
    if (!request_size(req, &n) || req > ((size_t)1 << 40) - align) {
        errno = ENOMEM;
        return NULL;
    }
    uint8_t* p = (uint8_t*)__wrap_malloc(req + align + MINCHUNK);
    if (!p) return NULL;
    uint8_t* a = (uint8_t*)(((uintptr_t)p + align - 1) & ~(uintptr_t)(align - 1));
    if (a == p) {
        pthread_mutex_lock(&s_lock);
        shrink_chunk(AT(p, -(ptrdiff_t)HDR), n);
        pthread_mutex_unlock(&s_lock);
        return p;
    }
    if ((size_t)(a - p) < MINCHUNK) a += align;
    pthread_mutex_lock(&s_lock);
    Chunk* c = AT(p, -(ptrdiff_t)HDR);
    Chunk* ac = AT(a, -(ptrdiff_t)HDR);
    size_t lead = (size_t)(a - p);
    size_t size = CSIZE(c);
    // The lead becomes a chunk of its own, freed; the aligned chunk
    // keeps the rest and is trimmed to size.
    ac->head = (size - lead) | INUSE | PINUSE;
    c->head = lead | (c->head & PINUSE) | INUSE;
    s_blocks++;             // two blocks now; free_chunk takes the lead off
    free_chunk(c);
    shrink_chunk(ac, n);
    pthread_mutex_unlock(&s_lock);
    return a;
}

int __wrap_posix_memalign(void** out, size_t align, size_t req)
{
    if (align < sizeof(void*) || (align & (align - 1))) return EINVAL;
    void* p = __wrap_memalign(align, req);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

void* __wrap_aligned_alloc(size_t align, size_t req)
{
    return __wrap_memalign(align, req);
}

size_t __wrap_malloc_usable_size(void* p)
{
    if (!p) return 0;
    if (!lowheap_owns(p)) return __real_malloc_usable_size(p);
    return CSIZE(AT(p, -(ptrdiff_t)HDR)) - HDR;
}

void* __wrap_realloc(void* p, size_t req)
{
    size_t n;
    if (!p) return __wrap_malloc(req);
    if (!lowheap_owns(p)) return __real_realloc(p, req);
    if (req == 0) {
        __wrap_free(p);
        return NULL;
    }
    if (!request_size(req, &n)) {
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&s_lock);
    Chunk* c = AT(p, -(ptrdiff_t)HDR);
    size_t size = CSIZE(c);
    if (size >= n) {
        shrink_chunk(c, n);
        pthread_mutex_unlock(&s_lock);
        return p;
    }
    Chunk* nx = AT(c, size);
    if ((uint8_t*)nx == s_top) {
        if ((size_t)(s_end - (uint8_t*)c) >= n) {
            s_used += n - size;
            c->head = n | (c->head & FLAGS);
            s_top = (uint8_t*)c + n;
            s_topPinuse = true;
            if (s_top > s_touched) s_touched = s_top;
            pthread_mutex_unlock(&s_lock);
            return p;
        }
    } else if (!(nx->head & INUSE) && size + CSIZE(nx) >= n) {
        size_t grown = size + CSIZE(nx);
        bin_unlink(nx);
        s_used += grown - size;
        c->head = grown | (c->head & FLAGS);
        set_next_pinuse(c, true);
        shrink_chunk(c, n);
        pthread_mutex_unlock(&s_lock);
        return p;
    }
    pthread_mutex_unlock(&s_lock);
    void* q = __wrap_malloc(req);
    if (!q) return NULL;
    memcpy(q, p, size - HDR);
    __wrap_free(p);
    return q;
}

// ------------------------------------------------------------- report

void lowheap_stats(struct lowheap_stats* s)
{
    pthread_once(&s_once, init);
    pthread_mutex_lock(&s_lock);
    memset(s, 0, sizeof *s);
    s->base = (uintptr_t)s_base;
    s->end = (uintptr_t)s_end;
    s->top = (uintptr_t)s_top;
    s->used = s_used;
    s->blocks = s_blocks;
    for (unsigned b = 0; b < NBINS; b++)
        for (Chunk* c = s_bins[b]; c; c = c->next) {
            size_t z = CSIZE(c);
            s->free += z;
            s->pieces++;
            if (z > s->largest) s->largest = z;
        }
    pthread_mutex_unlock(&s_lock);
}

#endif  // __aarch64__
