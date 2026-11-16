// The game's heap on the R36S: a region reserved below 4 GB, which the
// game's and the compat layer's malloc family use (the link wraps them,
// r36s/Makefile). The game keeps pointers in 32-bit slots, so nothing it
// allocates may live above 4 GB. glibc's own malloc would put big blocks
// and other threads' arenas there, and under QEMU its brk heap cannot grow
// at all. SDL, the GL driver and glibc keep glibc's malloc.

#ifndef R36S_LOWHEAP_H
#define R36S_LOWHEAP_H

#include <stddef.h>
#include <stdint.h>

struct lowheap_stats {
    uintptr_t base;     // the region
    uintptr_t end;
    uintptr_t top;      // chunks below, untouched space above
    size_t    used;     // bytes in use, headers included
    size_t    free;     // bytes in free chunks below top
    size_t    largest;  // the largest free chunk, top excluded
    size_t    pieces;   // free chunks
    size_t    blocks;   // chunks in use
};

void lowheap_stats(struct lowheap_stats* s);

// True when `p` came from the low heap.
int lowheap_owns(const void* p);

#endif
