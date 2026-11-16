// GPU buffers for the game's baked meshes (docs/R36S_OPTIMISATION_BACKLOG.md,
// item 1).
//
// The 3DS's PICA reads vertices and indices where the game wrote them, so
// the GL layer handed GL the same addresses as client-side arrays. The Mali
// driver then scans each draw's indices and copies the vertex range they
// reach, on every draw: about a thousand draws a frame in a race. A baked
// mesh (geobake.c) is written once, at its bake, and again only by
// georender_refresh; the game says so through r36s_mesh_static and
// r36s_mesh_changed, and the block's GL buffers are made, from the game's
// memory, at the first draw that uses them (on the GL thread) and again
// after a change. linearFree forgets a block; its GL buffers go at the
// next frame's start, on the GL thread.
//
// Draws from anything else -- the rings of quads, strips, sprites and
// skinned vertices, the envmap's fold, citro2d, a posed copy that draws
// ring vertices with a mesh's indices -- keep the client arrays.
//
// NFSHP2_VBO=0 turns it off (client arrays throughout, as before);
// NFSHP2_VBO=2 also checks every block's memory against what was uploaded,
// each frame, and logs any change the game made without a
// refresh (and uploads it).

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r36s_gl.h"
#include "glthread.h"
#include "r36s_platform.h"

typedef struct {
    const uint8_t* v;        // the key: the vertices' address
    const uint8_t* i;
    uint32_t vbytes, ibytes;
    GLuint   vbo, ibo;       // 0 until the first draw
    uint32_t sum;            // NFSHP2_VBO=2: the uploaded bytes' sum
    uint8_t  state;          // EMPTY, LIVE or GONE
    uint8_t  dirty;
} Block;

enum { EMPTY, LIVE, GONE };

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static Block*   s_table;
static uint32_t s_cap, s_used;          // s_used: LIVE and GONE slots
static int      s_mode = -1;            // NFSHP2_VBO
static GLuint*  s_dead;                 // GL names for the next frame to delete
static uint32_t s_deadN, s_deadCap;
static uint32_t s_verifyAt;
static unsigned s_mismatches;

// What GL has bound, so a draw binds only what changed (-1: unknown).
static GLint s_boundArray = -1, s_boundElement = -1;
// The block buffer 0 came from in this draw's setup, by its key (the
// table may move under a bake on another thread), or NULL.
static const void* s_drawKey;

static int mode(void)
{
    if (s_mode < 0) {
        const char* e = getenv("NFSHP2_VBO");
        s_mode = e == NULL || e[0] == '\0' ? 1 : atoi(e);
        printf("R36S: mesh buffers %s (NFSHP2_VBO=%d)\n",
               s_mode == 0 ? "off, client arrays" : s_mode == 2 ? "on, verified" : "on", s_mode);
    }
    return s_mode;
}

static uint32_t hash_ptr(const void* p)
{
    uint32_t h = (uint32_t)(uintptr_t)p;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

// The slot for `v`: its LIVE one, or NULL.
static Block* find(const void* v)
{
    if (s_table == NULL) return NULL;
    for (uint32_t k = hash_ptr(v) & (s_cap - 1);; k = (k + 1) & (s_cap - 1)) {
        Block* b = &s_table[k];
        if (b->state == EMPTY) return NULL;
        if (b->state == LIVE && b->v == v) return b;
    }
}

static void bury(GLuint name)
{
    if (name == 0) return;
    if (s_deadN == s_deadCap) {
        uint32_t cap = s_deadCap ? s_deadCap * 2 : 256;
        GLuint* d = (GLuint*)realloc(s_dead, cap * sizeof *d);
        if (d == NULL) return;   // a leak of one name, not a crash
        s_dead = d;
        s_deadCap = cap;
    }
    s_dead[s_deadN++] = name;
}

static void insert(const Block* nb);

static void grow(void)
{
    Block* old = s_table;
    uint32_t oldCap = s_cap;
    uint32_t cap = s_cap ? s_cap : 4096;
    while (cap < (s_used + 1) * 4) cap *= 2;   // load under a quarter, no tombstones
    s_table = (Block*)calloc(cap, sizeof *s_table);
    if (s_table == NULL) {
        s_table = old;
        return;
    }
    s_cap = cap;
    s_used = 0;
    for (uint32_t k = 0; k < oldCap; k++)
        if (old[k].state == LIVE) insert(&old[k]);
    free(old);
}

static void insert(const Block* nb)
{
    uint32_t k;
    if (s_table == NULL || (s_used + 1) * 2 > s_cap) grow();
    if (s_table == NULL) return;
    for (k = hash_ptr(nb->v) & (s_cap - 1); s_table[k].state == LIVE; k = (k + 1) & (s_cap - 1)) {}
    if (s_table[k].state == EMPTY) s_used++;
    s_table[k] = *nb;
    s_table[k].state = LIVE;
}

static void drop(Block* b)
{
    bury(b->vbo);
    bury(b->ibo);
    memset(b, 0, sizeof *b);
    b->state = GONE;
}

void r36s_mesh_static(const void* verts, size_t vbytes, const void* indices, size_t ibytes)
{
    Block nb;
    if (verts == NULL || vbytes == 0 || mode() == 0) return;
    memset(&nb, 0, sizeof nb);
    nb.v = (const uint8_t*)verts;
    nb.i = (const uint8_t*)indices;
    nb.vbytes = (uint32_t)vbytes;
    nb.ibytes = indices != NULL ? (uint32_t)ibytes : 0;
    nb.dirty = 1;
    pthread_mutex_lock(&s_lock);
    {
        Block* b = find(verts);
        if (b != NULL) drop(b);   // the same memory baked again
        insert(&nb);
    }
    pthread_mutex_unlock(&s_lock);
}

void r36s_mesh_changed(const void* verts)
{
    if (s_table == NULL) return;
    pthread_mutex_lock(&s_lock);
    {
        Block* b = find(verts);
        if (b != NULL) b->dirty = 1;
    }
    pthread_mutex_unlock(&s_lock);
}

void r36s_mesh_forget(const void* mem)
{
    if (s_table == NULL || mem == NULL) return;
    pthread_mutex_lock(&s_lock);
    {
        Block* b = find(mem);
        if (b != NULL) {
            drop(b);
        }
    }
    pthread_mutex_unlock(&s_lock);
}

// ----------------------------------------------------------- the GL side

static void bind_array(GLuint name)
{
    if (s_boundArray != (GLint)name) {
        glBindBuffer(GL_ARRAY_BUFFER, name);
        s_boundArray = (GLint)name;
    }
}

static void bind_element(GLuint name)
{
    if (s_boundElement != (GLint)name) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, name);
        s_boundElement = (GLint)name;
    }
}

static uint32_t sum_bytes(const uint8_t* p, uint32_t n)
{
    uint32_t h = 2166136261u, w;
    uint32_t k = 0;
    for (; k + 4 <= n; k += 4) {
        memcpy(&w, p + k, 4);
        h = (h ^ w) * 16777619u;
    }
    for (; k < n; k++) h = (h ^ p[k]) * 16777619u;
    return h;
}

static uint32_t block_sum(const Block* b)
{
    return sum_bytes(b->v, b->vbytes) ^ (b->i ? sum_bytes(b->i, b->ibytes) * 31u : 0);
}

// Makes or refreshes `b`'s GL buffers from the game's memory. glBufferData
// each time, so a refresh gets new storage and never waits for a draw the
// GPU hasn't run yet.
static bool upload(Block* b)
{
    if (b->vbo == 0) glGenBuffers(1, &b->vbo);
    if (b->i != NULL && b->ibo == 0) glGenBuffers(1, &b->ibo);
    if (b->vbo == 0 || (b->i != NULL && b->ibo == 0)) return false;
    bind_array(b->vbo);
    glBufferData(GL_ARRAY_BUFFER, b->vbytes, b->v, GL_STATIC_DRAW);
    if (b->i != NULL) {
        bind_element(b->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, b->ibytes, b->i, GL_STATIC_DRAW);
    }
    r36s_glcounts.bufUploads++;
    r36s_glcounts.bufUploadKB += (b->vbytes + b->ibytes + 1023) / 1024;
    if (s_mode == 2) b->sum = block_sum(b);
    b->dirty = 0;
    return true;
}

const void* r36sgl_vertex_source(const void* base, bool only)
{
    Block* b;
    s_drawKey = NULL;
    if (base == NULL || s_table == NULL || mode() == 0) {
        bind_array(0);
        return base;
    }
    pthread_mutex_lock(&s_lock);
    b = find(base);
    if (b != NULL && b->dirty && !upload(b)) b = NULL;
    if (b != NULL) {
        bind_array(b->vbo);
        if (only) s_drawKey = base;
        r36s_glcounts.bufDraws++;
    }
    pthread_mutex_unlock(&s_lock);
    if (b == NULL) {
        bind_array(0);
        return base;
    }
    return (const void*)0;   // offsets from the buffer's start
}

const void* r36sgl_index_source(const void* indices)
{
    const uint8_t* p = (const uint8_t*)indices;
    GLuint ibo = 0;
    uintptr_t off = 0;
    if (s_drawKey != NULL) {
        Block* b;
        pthread_mutex_lock(&s_lock);
        b = find(s_drawKey);
        if (b != NULL && b->ibo != 0 && p >= b->i && p < b->i + b->ibytes) {
            ibo = b->ibo;
            off = (uintptr_t)(p - b->i);
        }
        pthread_mutex_unlock(&s_lock);
    }
    bind_element(ibo);
    return ibo != 0 ? (const void*)off : indices;
}

bool r36sgl_mesh_dirty(const void* base)
{
    Block* b;
    bool dirty;
    if (base == NULL || s_table == NULL || mode() == 0) return false;
    pthread_mutex_lock(&s_lock);
    b = find(base);
    dirty = b != NULL && (b->dirty || b->vbo == 0);
    pthread_mutex_unlock(&s_lock);
    return dirty;
}

bool r36sgl_mesh_ref(const void* base, const void* indices, GLuint* vbo, GLuint* ibo,
                     uintptr_t* ioff)
{
    Block* b;
    bool ok = false;
    if (base == NULL || s_table == NULL || mode() == 0) return false;
    pthread_mutex_lock(&s_lock);
    b = find(base);
    if (b != NULL && b->dirty && !upload(b)) b = NULL;
    if (b != NULL) {
        const uint8_t* p = (const uint8_t*)indices;
        *vbo = b->vbo;
        *ibo = 0;
        *ioff = 0;
        if (p == NULL) {
            ok = true;
        } else if (b->ibo != 0 && p >= b->i && p < b->i + b->ibytes) {
            *ibo = b->ibo;
            *ioff = (uintptr_t)(p - b->i);
            ok = true;
        }
        if (ok) r36s_glcounts.bufDraws++;
    }
    pthread_mutex_unlock(&s_lock);
    return ok;
}

void r36sgl_bind_array(GLuint name) { bind_array(name); }
GLint r36sgl_bound_array(void) { return s_boundArray; }
void r36sgl_bind_element(GLuint name) { bind_element(name); }

void r36sgl_client_arrays(void)
{
    bind_array(0);
    bind_element(0);
    s_drawKey = NULL;
}

void r36sgl_buffers_unknown(void)
{
    s_boundArray = s_boundElement = -1;
    s_drawKey = NULL;
}

void r36sgl_buffers_frame(void)
{
    if (s_table == NULL) return;
    pthread_mutex_lock(&s_lock);
    if (s_deadN > 0) {
        for (uint32_t k = 0; k < s_deadN; k++) {
            if ((GLint)s_dead[k] == s_boundArray) s_boundArray = 0;
            if ((GLint)s_dead[k] == s_boundElement) s_boundElement = 0;
        }
        glDeleteBuffers((GLsizei)s_deadN, s_dead);
        // GL detached them from any attribute using them, and may give
        // their names out again.
        r36sgl_attrib_forget();
        s_deadN = 0;
    }
    if (s_mode == 2) {
        // Every block, every frame: any whose memory moved since its upload
        // without a refresh is a writer this file wasn't told of.
        uint32_t slice = s_cap;
        for (uint32_t n = 0; n < slice; n++) {
            Block* b = &s_table[s_verifyAt];
            s_verifyAt = (s_verifyAt + 1) & (s_cap - 1);
            if (b->state != LIVE || b->vbo == 0 || b->dirty) continue;
            if (block_sum(b) != b->sum) {
                s_mismatches++;
                printf("vbo: block %p (%u + %u bytes) changed without a refresh (%u so far)\n",
                       (const void*)b->v, b->vbytes, b->ibytes, s_mismatches);
                b->dirty = 1;
            }
        }
    }
    pthread_mutex_unlock(&s_lock);
}
