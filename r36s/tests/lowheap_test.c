// Stress test for platform/lowheap.c: random malloc, calloc, realloc,
// memalign and free from several threads, every block filled with a
// pattern of its own and checked before it is freed, and the heap's
// chunks walked for consistency. Built and run by r36s/tests/run.sh.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define malloc_usable_size  real_usable_unused
#include "../platform/lowheap.c"
#undef malloc_usable_size

void* __real_malloc(size_t n) { return malloc(n); }
void  __real_free(void* p) { free(p); }
void* __real_calloc(size_t n, size_t m) { return calloc(n, m); }
void* __real_realloc(void* p, size_t n) { return realloc(p, n); }
void* __real_memalign(size_t a, size_t n) { return aligned_alloc(a, n); }
size_t __real_malloc_usable_size(void* p) { (void)p; return 0; }

static void fail(const char* what, const void* at)
{
    fprintf(stderr, "FAIL: %s at %p\n", what, at);
    abort();
}

// Walks every chunk from the base to the top: sizes, flags, footers, no
// two free chunks in a row, every free chunk in its bin, totals.
static void walk(void)
{
    pthread_mutex_lock(&s_lock);
    uint8_t* p = s_base;
    bool prevInuse = true;
    size_t used = 0, blocks = 0, freeBytes = 0, freeCount = 0;
    while (p < s_top) {
        Chunk* c = (Chunk*)p;
        size_t z = CSIZE(c);
        if (z < MINCHUNK || (z & 15) || p + z > s_top) fail("chunk size", c);
        if (((c->head & PINUSE) != 0) != prevInuse) fail("pinuse flag", c);
        bool inuse = (c->head & INUSE) != 0;
        if (!inuse) {
            if (!prevInuse) fail("two free chunks in a row", c);
            if (p + z == s_top) fail("free chunk next to top", c);
            if (((Chunk*)(p + z))->prev_size != z) fail("footer", c);
            bool found = false;
            for (Chunk* f = s_bins[bin_of(z)]; f; f = f->next)
                if (f == c) { found = true; break; }
            if (!found) fail("free chunk not in its bin", c);
            freeBytes += z;
            freeCount++;
        } else {
            used += z;
            blocks++;
        }
        prevInuse = inuse;
        p += z;
    }
    if (p != s_top) fail("walk overran top", p);
    if (prevInuse != s_topPinuse) fail("top pinuse", p);
    if (used != s_used) fail("used total", (void*)used);
    if (blocks != s_blocks) fail("block count", (void*)blocks);
    size_t binned = 0;
    for (unsigned b = 0; b < NBINS; b++) {
        bool any = s_bins[b] != NULL;
        if (any != ((s_map[b >> 6] >> (b & 63)) & 1)) fail("bitmap", (void*)(uintptr_t)b);
        for (Chunk* f = s_bins[b]; f; f = f->next) {
            if (bin_of(CSIZE(f)) != b) fail("chunk in the wrong bin", f);
            if (f->next && f->next->prev != f) fail("bin links", f);
            binned++;
        }
    }
    if (binned != freeCount) fail("bins hold chunks the walk didn't see", (void*)binned);
    (void)freeBytes;
    pthread_mutex_unlock(&s_lock);
}

typedef struct { uint8_t* p; size_t n; uint8_t tag; } Block;

static uint32_t rng(uint32_t* s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

static size_t pick_size(uint32_t* s)
{
    uint32_t r = rng(s) % 100;
    if (r < 60) return rng(s) % 256;
    if (r < 90) return rng(s) % 8192;
    if (r < 99) return rng(s) % (1u << 20);
    return rng(s) % (24u << 20);
}

static void fill(Block* b) { memset(b->p, b->tag, b->n); }
static void check(const Block* b)
{
    for (size_t i = 0; i < b->n; i++)
        if (b->p[i] != b->tag) fail("block contents", b->p + i);
}

struct Job { uint32_t seed; int steps; int walkEvery; };

static void* run(void* arg)
{
    struct Job* j = (struct Job*)arg;
    enum { N = 512 };
    Block* blocks = (Block*)calloc(N, sizeof(Block));
    uint32_t s = j->seed;
    for (int step = 0; step < j->steps; step++) {
        Block* b = &blocks[rng(&s) % N];
        uint32_t op = rng(&s) % 10;
        if (b->p) {
            check(b);
            if (op < 3) {
                size_t n = pick_size(&s);
                uint8_t* q = (uint8_t*)__wrap_realloc(b->p, n);
                if (n == 0) { if (q) fail("realloc(0)", q); b->p = NULL; continue; }
                if (!q) fail("realloc", NULL);
                size_t keep = b->n < n ? b->n : n;
                for (size_t i = 0; i < keep; i++) if (q[i] != b->tag) fail("realloc kept", q + i);
                b->p = q; b->n = n; b->tag = (uint8_t)rng(&s); fill(b);
            } else {
                __wrap_free(b->p);
                b->p = NULL;
            }
        } else {
            size_t n = pick_size(&s);
            if (op < 2) {
                size_t a = (size_t)32 << (rng(&s) % 12);
                b->p = (uint8_t*)__wrap_memalign(a, n);
                if (!b->p || ((uintptr_t)b->p & (a - 1))) fail("memalign", b->p);
            } else if (op < 4) {
                b->p = (uint8_t*)__wrap_calloc(1, n);
                if (!b->p) fail("calloc", NULL);
                for (size_t i = 0; i < n; i++) if (b->p[i]) fail("calloc zero", b->p + i);
            } else {
                b->p = (uint8_t*)__wrap_malloc(n);
                if (!b->p) fail("malloc", NULL);
            }
            if (((uintptr_t)b->p & 15)) fail("alignment", b->p);
            if ((uintptr_t)b->p + n > 0xFFFFFFFFu) fail("above 4 GB", b->p);
            if (__wrap_malloc_usable_size(b->p) < n) fail("usable size", b->p);
            b->n = n; b->tag = (uint8_t)rng(&s); fill(b);
        }
        if (j->walkEvery && step % j->walkEvery == 0) walk();
    }
    for (int i = 0; i < N; i++) if (blocks[i].p) { check(&blocks[i]); __wrap_free(blocks[i].p); }
    free(blocks);
    return NULL;
}

int main(void)
{
    struct Job one = { 0x12345u, 200000, 1 };
    run(&one);
    walk();
    if (s_used != 0 || s_blocks != 0 || s_top != s_base) fail("heap not empty after freeing all", s_top);
    printf("single thread: ok (region %p-%p)\n", (void*)s_base, (void*)s_end);

    pthread_t t[4];
    struct Job jobs[4];
    for (int i = 0; i < 4; i++) {
        jobs[i] = (struct Job){ 0x9E3779B9u * (uint32_t)(i + 1), 150000, 0 };
        pthread_create(&t[i], NULL, run, &jobs[i]);
    }
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    walk();
    if (s_used != 0 || s_blocks != 0 || s_top != s_base) fail("heap not empty after threads", s_top);
    printf("four threads: ok\n");
    void* g = malloc(100);   // glibc's, freed through the wrapper
    __wrap_free(__wrap_realloc(g, 200));
    printf("foreign pointers: ok\n");
    return 0;
}
