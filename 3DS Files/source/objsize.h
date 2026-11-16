// objsize -- a retail object's allocation size on the R36S's aarch64
// build, whose pointers are wider than the console's.
//
// Not a port of any object. The image allocates some objects by a literal
// size, `new TrackCarCameraMover` as bMalloc(1136, ...), and the port keeps
// those literals, which the checkers hold to the image. On the 3DS and on
// the checkers' hosts this is the identity. On the R36S a pointer is 8
// bytes and an object built from those fields grows; it can at most
// double, since a field either keeps its size and alignment or, being a
// pointer, doubles both, so every offset and the padding after it at most
// double too. bmem.c gives every small block (BMEM_R36S_OBJECT_MAX bytes or
// less) that doubled size on the R36S, so the literals stay as they are.

#ifndef OBJSIZE_H
#define OBJSIZE_H

#if defined(NFSHP2_R36S)
#define RETAIL_OBJ_SIZE(n) ((int)((n) * (int)(sizeof(void *) / 4)))
#define BMEM_R36S_OBJECT_MAX 8192
#else
#define RETAIL_OBJ_SIZE(n) ((int)(n))
#endif

#endif
