#define _GNU_SOURCE
// The GL thread: see glthread.h.
//
// The game's thread records each GL call into a chunk -- an op and its
// arguments, with a copy of whatever memory the call reads (uniform
// values, pixels, buffer data, shader text, the vertices and indices of a
// draw from client-side arrays) -- and hands chunks to the GL thread as
// they fill, so the thread draws while the game is still making the
// frame. The frame's end hands over the swap; the game waits only when
// the thread is still a whole frame behind.
//
// A call whose result the caller needs (a shader's compile status, a
// uniform's location, a framebuffer's status, glGetError, glReadPixels)
// waits for the thread to play everything recorded, borrows the context,
// makes the call and gives the context back (rgl_direct_begin/end). Those
// happen when shaders, programs and render targets are made, and in the
// debug options. Texture and buffer names come from pools the thread
// keeps filled, so making a texture in a race doesn't wait.
//
// NFSHP2_GLTHREAD=0: no thread; every call is made at once, as before.
// It's also off by itself on a backend it hasn't been run on
// (r36s_video_thread_ok; NFSHP2_GLTHREAD=2 tries it there), and when the
// driver won't move the context to the thread.

#define RGL_NO_REDIRECT
#include <SDL.h>
#include <dlfcn.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "glthread.h"
#include "r36s_platform.h"

// --------------------------------------------------------------- chunks

typedef union {
    uint32_t    u;
    int32_t     i;
    float       f;
    const void* p;
} Arg;

#define CHUNK_BYTES  (256 * 1024)
#define SUBMIT_AT    (32 * 1024)       // hand a chunk over once its ops pass this
#define BIG_BLOB     (CHUNK_BYTES / 4) // larger copies are malloc'd
// What may wait for the GL thread, so a load's uploads can't take the
// device's memory: past these the game thread waits for it.
#define MAX_QUEUED   96                // chunks handed over, not yet played
#define MAX_HEAP     (48u << 20)       // large copies not yet played
#define MAX_FREE     24                // played chunks kept for reuse

typedef struct Chunk {
    struct Chunk*  next;
    size_t         ops;      // bytes of ops from the start
    size_t         blobs;    // copies from the end down to here
    unsigned char  mem[CHUNK_BYTES];
} Chunk;

enum {
    OP_END_OF_CHUNK,
    OP_RELEASE, OP_SWAP,
    OP_ActiveTexture, OP_AttachShader, OP_BindAttribLocation, OP_BindBuffer,
    OP_BindFramebuffer, OP_BindRenderbuffer, OP_BindTexture, OP_BlendColor,
    OP_BlendEquationSeparate, OP_BlendFuncSeparate, OP_BufferData, OP_Clear, OP_ClearColor,
    OP_ClearDepthf, OP_ClearStencil, OP_ColorMask, OP_CompileShader, OP_CullFace,
    OP_DeleteBuffers, OP_DeleteFramebuffers, OP_DeleteProgram, OP_DeleteRenderbuffers,
    OP_DeleteShader, OP_DeleteTextures, OP_DepthFunc, OP_DepthMask, OP_Disable,
    OP_DisableVertexAttribArray, OP_DrawArrays, OP_DrawElements, OP_DrawClient, OP_Enable,
    OP_EnableVertexAttribArray, OP_FramebufferRenderbuffer, OP_FramebufferTexture2D,
    OP_LinkProgram, OP_PixelStorei, OP_RenderbufferStorage, OP_ShaderSource, OP_TexImage2D,
    OP_TexParameteri, OP_Uniform1f, OP_Uniform1i, OP_Uniform2f, OP_Uniform4f, OP_Uniform4fv,
    OP_UseProgram, OP_VertexAttribPointer, OP_Viewport, OP_DrawElementsInstanced,
    OP_DrawArraysInstanced, OP_VertexAttribDivisor, OP_FreeBlob, OP_InvalidateFramebuffer,
    OP_TexSubImage2D,
};

static bool            s_on;           // recording for the thread
static bool            s_noDraw;       // NFSHP2_GL_NODRAW: a memory soak under emulation
static unsigned        s_drawFrom;     // NFSHP2_GL_DRAWFROM (rgl_start)
static int             s_direct;       // the context is lent to the game's thread
static pthread_t       s_thread;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cv = PTHREAD_COND_INITIALIZER;
static int             s_contextMoved;   // 1 yes, -1 no, 0 not yet known
static Chunk*          s_queue;        // handed over, oldest first
static Chunk*          s_queueTail;
static Chunk*          s_free;
static Chunk*          s_cur;          // the game thread's
static unsigned        s_queued;       // chunks in s_queue
static unsigned        s_freeN;        // chunks in s_free
static size_t          s_heapBytes;    // large copies not yet played
static bool            s_released, s_acquire;
static unsigned        s_framesSent, s_framesDone;
static double          s_busyMs;       // the GL thread's playing time this frame
static double          s_chunkStart;   // when its current chunk (or last swap) began
static double          s_cpuMark;      // its CPU time at the last swap's end
static double          s_gameStart;    // the game thread's frame began
static unsigned        s_waitUs;       // the game thread's last wait at a frame's end

typedef void (GL_APIENTRY* FnDrawElementsInst)(GLenum, GLsizei, GLenum, const void*, GLsizei);
typedef void (GL_APIENTRY* FnDrawArraysInst)(GLenum, GLint, GLsizei, GLsizei);
typedef void (GL_APIENTRY* FnAttribDivisor)(GLuint, GLuint);
static FnDrawElementsInst p_drawElementsInst;
static FnDrawArraysInst   p_drawArraysInst;
static FnAttribDivisor    p_attribDivisor;
static int                s_instResolved = -1;
typedef void (GL_APIENTRY* FnGetProgramBinary)(GLuint, GLsizei, GLsizei*, GLenum*, void*);
typedef void (GL_APIENTRY* FnProgramBinary)(GLuint, GLenum, const void*, GLsizei);
typedef void (GL_APIENTRY* FnProgramParameteri)(GLuint, GLenum, GLint);
typedef void (GL_APIENTRY* FnInvalidateFramebuffer)(GLenum, GLsizei, const GLenum*);
static FnGetProgramBinary      p_getProgramBinary;
static FnProgramBinary         p_programBinary;
static FnProgramParameteri     p_programParameteri;
static FnInvalidateFramebuffer p_invalidateFramebuffer;

// The pools of names the thread makes ahead.
#define POOL_FILL 64
static GLuint   s_texPool[POOL_FILL * 2], s_bufPool[POOL_FILL * 2];
static unsigned s_texN, s_bufN;

static Chunk* chunk_get(void)
{
    Chunk* c;
    pthread_mutex_lock(&s_mu);
    c = s_free;
    if (c != NULL) {
        s_free = c->next;
        s_freeN--;
    }
    pthread_mutex_unlock(&s_mu);
    if (c == NULL) c = (Chunk*)malloc(sizeof *c);
    if (c == NULL) {
        fprintf(stderr, "glthread: out of memory\n");
        abort();
    }
    c->next = NULL;
    c->ops = 0;
    c->blobs = CHUNK_BYTES;
    return c;
}

static void submit(void)
{
    Chunk* c = s_cur;
    if (c == NULL || c->ops == 0) return;
    ((Arg*)(c->mem + c->ops))->u = OP_END_OF_CHUNK;
    c->ops += sizeof(Arg);
    s_cur = chunk_get();
    pthread_mutex_lock(&s_mu);
    if (s_queueTail) s_queueTail->next = c; else s_queue = c;
    s_queueTail = c;
    s_queued++;
    pthread_cond_broadcast(&s_cv);
    while (s_queued > MAX_QUEUED) pthread_cond_wait(&s_cv, &s_mu);
    pthread_mutex_unlock(&s_mu);
}

// Room for an op of `nargs` arguments and a copy of `blob` bytes in one
// chunk (an op's copy must live as long as the op). The op's arguments;
// *copy is the copy's memory (malloc'd when large: *heap).
static Arg* rec(unsigned op, unsigned nargs, size_t blob, void** copy, bool* heap)
{
    size_t opBytes = (1 + nargs) * sizeof(Arg);
    size_t need = opBytes + sizeof(Arg);   // room for the chunk's end
    size_t b = 0;
    bool over = false;
    Arg* a;
    if (heap) *heap = false;
    if (blob > 0) {
        if (blob > BIG_BLOB) {
            *copy = malloc(blob);
            if (*copy == NULL) {
                fprintf(stderr, "glthread: out of memory for %u bytes\n", (unsigned)blob);
                abort();
            }
            *heap = true;
            pthread_mutex_lock(&s_mu);
            s_heapBytes += malloc_usable_size(*copy);
            over = s_heapBytes > MAX_HEAP;
            pthread_mutex_unlock(&s_mu);
        } else {
            b = (blob + 7) & ~(size_t)7;
        }
    }
    if (over) {
        // Hand over what's recorded and wait for the thread to catch up.
        submit();
        pthread_mutex_lock(&s_mu);
        while (s_heapBytes > MAX_HEAP / 2 && s_queue != NULL) pthread_cond_wait(&s_cv, &s_mu);
        pthread_mutex_unlock(&s_mu);
    }
    if (s_cur->ops + need + b > s_cur->blobs) submit();
    a = (Arg*)(s_cur->mem + s_cur->ops);
    a->u = op | (nargs << 16);
    s_cur->ops += opBytes;
    if (b > 0) {
        s_cur->blobs -= b;
        *copy = s_cur->mem + s_cur->blobs;
    }
    return a + 1;
}

static Arg* rec0(unsigned op, unsigned nargs) { return rec(op, nargs, 0, NULL, NULL); }

static void after_rec(void)
{
    if (s_cur->ops >= SUBMIT_AT) submit();
}

// -------------------------------------------------------- the GL thread

static void pools_fill(void)
{
    if (s_texN < POOL_FILL || s_bufN < POOL_FILL) {
        GLuint t[POOL_FILL * 2], b[POOL_FILL * 2];
        unsigned nt = POOL_FILL * 2 - s_texN, nb = POOL_FILL * 2 - s_bufN;
        // The pools are refilled to twice the mark, outside the lock.
        pthread_mutex_lock(&s_mu);
        nt = POOL_FILL * 2 - s_texN;
        nb = POOL_FILL * 2 - s_bufN;
        pthread_mutex_unlock(&s_mu);
        if (nt) glGenTextures((GLsizei)nt, t);
        if (nb) glGenBuffers((GLsizei)nb, b);
        pthread_mutex_lock(&s_mu);
        for (unsigned i = 0; i < nt && s_texN < POOL_FILL * 2; i++) s_texPool[s_texN++] = t[i];
        for (unsigned i = 0; i < nb && s_bufN < POOL_FILL * 2; i++) s_bufPool[s_bufN++] = b[i];
        pthread_mutex_unlock(&s_mu);
    }
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static double thread_cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ------------------------------------------------- the present thread
//
// With one context the GL thread waits in the swap while the GPU draws the
// frame (on the R36S the swap returns once the GPU is done with it), and
// the GPU then waits while the GL thread hands the driver the next frame:
// a frame costs the two added. Device run 7: 20 ms of driver work, 15 to
// 17 ms in the swap, the GPU 55% busy, 38.7 ms frames.
//
// So the window's swap gets a thread of its own. What the game draws to
// the window (the presenter, the frame counter) the GL thread draws into
// one of three textures instead; the present thread -- a second context
// sharing them (r36s_video_split) -- draws that texture to the window and
// swaps, while the GL thread goes on with the next frame. GLES 3 fences
// order the two on the GPU: the present thread's draw waits for the
// frame's, and a texture is drawn into again only after the present
// thread's draw of it. The GL thread is at most one frame ahead of the
// swaps (one more frame of latency).
//
// NFSHP2_PRESENT_THREAD=0: off, one context as before. Also off without
// the GL thread, GLES 3 or EGL_KHR_surfaceless_context.
// NFSHP2_PRESENT_AB=N (r36s_perf.c): every N seconds the GL thread waits
// for each swap, as before, to compare the two in one run.

typedef struct RSync_* RSync;
typedef RSync (GL_APIENTRY* FnFenceSync)(GLenum, GLbitfield);
typedef void (GL_APIENTRY* FnWaitSync)(RSync, GLbitfield, uint64_t);
typedef void (GL_APIENTRY* FnDeleteSync)(RSync);
#define R_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define R_TIMEOUT_IGNORED            0xFFFFFFFFFFFFFFFFull
static FnFenceSync  p_fenceSync;
static FnWaitSync   p_waitSync;
static FnDeleteSync p_deleteSync;

#define RING 3
typedef struct {
    GLuint tex, fbo;     // the shared texture; the GL thread's framebuffer of it
    int    w, h;
    RSync  ready;        // the frame drawn into it (the GL thread's fence)
    RSync  done;         // the present thread's draw of it (its fence)
    int    state;        // 0 free, 1 the GL thread's, 2 handed over, 3 being presented
} Slot;
static Slot            s_ring[RING];
static bool            s_split;        // the present thread is on
static unsigned        s_slot;         // the GL thread's slot this frame
static bool            s_slotHeld;     // and it has drawn to "the window"
static pthread_t       s_presenter;
static pthread_mutex_t s_pmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_pcv = PTHREAD_COND_INITIALIZER;
static int             s_presQ[RING];  // handed-over slots, oldest first
static unsigned        s_presHead, s_presTail;
static unsigned        s_handed, s_presented;
static double          s_presentMs;    // the present thread's last swap
static int             s_presentUp;    // 1 started, -1 failed, 0 not yet known

// The framebuffer that stands for the window (0) this frame: the GL
// thread's slot, once the present thread is done with it. On the GL thread,
// or the game's thread while the context is lent to it.
static GLuint window_fbo(void)
{
    Slot* sl = &s_ring[s_slot];
    if (!s_slotHeld) {
        RSync done;
        int w, h;
        pthread_mutex_lock(&s_pmu);
        while (sl->state != 0) pthread_cond_wait(&s_pcv, &s_pmu);
        sl->state = 1;
        done = sl->done;
        sl->done = NULL;
        pthread_mutex_unlock(&s_pmu);
        if (done != NULL) {
            p_waitSync(done, 0, R_TIMEOUT_IGNORED);
            p_deleteSync(done);
        }
        r36s_video_size(&w, &h);
        if (sl->fbo == 0 || sl->w != w || sl->h != h) {
            // Made, or remade for a new window size, keeping the texture
            // unit's binding as the layer left it.
            GLint unit = 0, bound = 0, drawFb = 0, readFb = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
            glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &drawFb);
            glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &readFb);
            if (sl->tex == 0) glGenTextures(1, &sl->tex);
            if (sl->fbo == 0) glGenFramebuffers(1, &sl->fbo);
            glBindTexture(GL_TEXTURE_2D, sl->tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
            glActiveTexture((GLenum)unit);
            glBindFramebuffer(GL_FRAMEBUFFER, sl->fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sl->tex, 0);
            glBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, (GLuint)drawFb);
            glBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, (GLuint)readFb);
            sl->w = w;
            sl->h = h;
        }
        s_slotHeld = true;
    }
    return sl->fbo;
}

static void fb_bind(GLenum target, GLuint fbo)
{
    if (fbo == 0 && s_split) fbo = window_fbo();
    glBindFramebuffer(target, fbo);
}

// The GL thread at the frame's end: the slot handed to the present thread.
// The wait is for the swap before this frame's (or, A/B, for this frame's).
static void present_hand(void)
{
    Slot* sl = &s_ring[s_slot];
    unsigned mine;
    if (!s_slotHeld) {
        // Nothing drawn to the window this frame: nothing to show.
        glFlush();
        return;
    }
    sl->ready = p_fenceSync(R_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    pthread_mutex_lock(&s_pmu);
    sl->state = 2;
    s_presQ[s_presTail++ % RING] = (int)s_slot;
    mine = ++s_handed;
    pthread_cond_broadcast(&s_pcv);
    while (s_presented + (r36s_present_serial ? 0u : 1u) < mine)
        pthread_cond_wait(&s_pcv, &s_pmu);
    pthread_mutex_unlock(&s_pmu);
    s_slot = (s_slot + 1) % RING;
    s_slotHeld = false;
}

static GLuint present_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

static void* present_main(void* arg)
{
    static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    static const char* vs =
        "attribute vec2 aPos;\n"
        "varying vec2 vUv;\n"
        "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* fs =
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "varying vec2 vUv;\n"
        "void main() { gl_FragColor = texture2D(uTex, vUv); }\n";
    GLuint prog = 0, v, f;
    GLint linked = 0;
    (void)arg;
    if (r36s_video_present_current()) {
        v = present_shader(GL_VERTEX_SHADER, vs);
        f = present_shader(GL_FRAGMENT_SHADER, fs);
        if (v && f) {
            prog = glCreateProgram();
            glAttachShader(prog, v);
            glAttachShader(prog, f);
            glBindAttribLocation(prog, 0, "aPos");
            glLinkProgram(prog);
            glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        }
    }
    pthread_mutex_lock(&s_pmu);
    s_presentUp = linked ? 1 : -1;
    pthread_cond_broadcast(&s_pcv);
    pthread_mutex_unlock(&s_pmu);
    if (!linked) return NULL;
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    for (;;) {
        Slot* sl;
        RSync ready, done;
        double t0;
        int w, h;
        pthread_mutex_lock(&s_pmu);
        while (s_presHead == s_presTail) pthread_cond_wait(&s_pcv, &s_pmu);
        sl = &s_ring[s_presQ[s_presHead++ % RING]];
        sl->state = 3;
        ready = sl->ready;
        sl->ready = NULL;
        pthread_mutex_unlock(&s_pmu);
        // The GPU waits for the frame; this thread doesn't.
        p_waitSync(ready, 0, R_TIMEOUT_IGNORED);
        p_deleteSync(ready);
        r36s_video_size(&w, &h);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, sl->tex);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        done = p_fenceSync(R_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        pthread_mutex_lock(&s_pmu);
        sl->done = done;
        sl->state = 0;
        pthread_cond_broadcast(&s_pcv);
        pthread_mutex_unlock(&s_pmu);
        t0 = now_ms();
        r36s_video_swap();
        t0 = now_ms() - t0;
        pthread_mutex_lock(&s_pmu);
        s_presentMs = t0;
        s_presented++;
        pthread_cond_broadcast(&s_pcv);
        pthread_mutex_unlock(&s_pmu);
    }
    return NULL;
}

// On the game's thread, the context current there with the window, before
// the GL thread starts. False: one context, as before.
static bool present_start(void)
{
    const char* e = getenv("NFSHP2_PRESENT_THREAD");
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (e != NULL && e[0] == '0') {
        printf("R36S: present thread off (NFSHP2_PRESENT_THREAD=0)\n");
        return false;
    }
    if (ver == NULL || strstr(ver, "OpenGL ES 3") == NULL) {
        printf("R36S: present thread off: needs GLES 3\n");
        return false;
    }
    p_fenceSync = (FnFenceSync)SDL_GL_GetProcAddress("glFenceSync");
    p_waitSync = (FnWaitSync)SDL_GL_GetProcAddress("glWaitSync");
    p_deleteSync = (FnDeleteSync)SDL_GL_GetProcAddress("glDeleteSync");
    if (!p_fenceSync) p_fenceSync = (FnFenceSync)dlsym(RTLD_DEFAULT, "glFenceSync");
    if (!p_waitSync) p_waitSync = (FnWaitSync)dlsym(RTLD_DEFAULT, "glWaitSync");
    if (!p_deleteSync) p_deleteSync = (FnDeleteSync)dlsym(RTLD_DEFAULT, "glDeleteSync");
    if (!p_fenceSync || !p_waitSync || !p_deleteSync) {
        printf("R36S: present thread off: no fences\n");
        return false;
    }
    if (!r36s_video_split()) return false;
    if (pthread_create(&s_presenter, NULL, present_main, NULL) != 0) {
        r36s_video_unsplit();
        printf("R36S: present thread not started\n");
        return false;
    }
    pthread_mutex_lock(&s_pmu);
    while (s_presentUp == 0) pthread_cond_wait(&s_pcv, &s_pmu);
    pthread_mutex_unlock(&s_pmu);
    if (s_presentUp < 0) {
        pthread_join(s_presenter, NULL);
        r36s_video_unsplit();
        printf("R36S: present thread off: its context didn't start\n");
        return false;
    }
#ifdef __GLIBC__
    pthread_setname_np(s_presenter, "nfshp2-present");
#endif
    s_split = true;
    r36s_present_thread = true;
    printf("R36S: present thread on\n");
    return true;
}

// A draw from client-side arrays, as rgl_DrawArrays/Elements copied it:
// the attributes, then their vertices, then the indices.
typedef struct {
    GLuint      index;
    GLint       size;
    GLenum      type;
    GLboolean   norm;
    GLsizei     stride;
    const void* ptr;     // into the copy, less the first vertex's offset
} ClientAttr;

typedef struct {
    GLenum      mode, itype;     // itype 0: an array draw
    GLint       first;
    GLsizei     count;
    const void* indices;         // into the copy
    GLuint      arrayBuf;        // the array buffer to bind back
    GLuint      elemBuf;
    unsigned    nAttr;
    ClientAttr  attr[16];
} ClientDraw;

static void play(Chunk* c)
{
    const unsigned char* p = c->mem;
    for (;;) {
        const Arg* h = (const Arg*)p;
        unsigned op = h->u & 0xFFFF, n = h->u >> 16;
        const Arg* a = h + 1;
        p += (1 + n) * sizeof(Arg);
        switch (op) {
        case OP_END_OF_CHUNK: return;
        case OP_RELEASE:
            r36s_video_make_current(false);
            pthread_mutex_lock(&s_mu);
            s_released = true;
            pthread_cond_broadcast(&s_cv);
            while (!s_acquire) pthread_cond_wait(&s_cv, &s_mu);
            s_acquire = false;
            pthread_mutex_unlock(&s_mu);
            r36s_video_make_current(true);
            break;
        case OP_SWAP: {
            double t0 = now_ms(), c0 = thread_cpu_ms(), c1;
            R36sGlCounts* counts = (R36sGlCounts*)a[0].p;
            counts->glBusyUs = (unsigned)((s_busyMs + t0 - s_chunkStart) * 1000.0);
            counts->playCpuUs = (unsigned)((c0 - s_cpuMark) * 1000.0);
            s_busyMs = 0;
            if (s_split) {
                present_hand();
                counts->presentUs = (unsigned)(s_presentMs * 1000.0);
            } else {
                r36s_video_swap();
            }
            c1 = thread_cpu_ms();
            counts->swapCpuUs = (unsigned)((c1 - c0) * 1000.0);
            s_cpuMark = c1;
            r36s_perf_frame_with(now_ms() - t0, counts);
            pools_fill();
            s_chunkStart = now_ms();
            pthread_mutex_lock(&s_mu);
            s_framesDone++;
            pthread_cond_broadcast(&s_cv);
            pthread_mutex_unlock(&s_mu);
            break;
        }
        case OP_FreeBlob:
            pthread_mutex_lock(&s_mu);
            s_heapBytes -= malloc_usable_size((void*)a[0].p);
            pthread_cond_broadcast(&s_cv);
            pthread_mutex_unlock(&s_mu);
            free((void*)a[0].p);
            break;
        case OP_TexSubImage2D:
            glTexSubImage2D(a[0].u, a[1].i, 0, 0, a[2].i, a[3].i, a[4].u, a[5].u, a[6].p);
            break;
        case OP_InvalidateFramebuffer: {
            GLenum list[2] = { a[1].u, a[2].u };
            p_invalidateFramebuffer(a[0].u, 2, list);
            break;
        }
        case OP_ActiveTexture: glActiveTexture(a[0].u); break;
        case OP_AttachShader: glAttachShader(a[0].u, a[1].u); break;
        case OP_BindAttribLocation: glBindAttribLocation(a[0].u, a[1].u, (const GLchar*)a[2].p); break;
        case OP_BindBuffer: glBindBuffer(a[0].u, a[1].u); break;
        case OP_BindFramebuffer: fb_bind(a[0].u, a[1].u); break;
        case OP_BindRenderbuffer: glBindRenderbuffer(a[0].u, a[1].u); break;
        case OP_BindTexture: glBindTexture(a[0].u, a[1].u); break;
        case OP_BlendColor: glBlendColor(a[0].f, a[1].f, a[2].f, a[3].f); break;
        case OP_BlendEquationSeparate: glBlendEquationSeparate(a[0].u, a[1].u); break;
        case OP_BlendFuncSeparate: glBlendFuncSeparate(a[0].u, a[1].u, a[2].u, a[3].u); break;
        case OP_BufferData: glBufferData(a[0].u, (GLsizeiptr)a[1].u, a[2].p, a[3].u); break;
        case OP_Clear: glClear(a[0].u); break;
        case OP_ClearColor: glClearColor(a[0].f, a[1].f, a[2].f, a[3].f); break;
        case OP_ClearDepthf: glClearDepthf(a[0].f); break;
        case OP_ClearStencil: glClearStencil(a[0].i); break;
        case OP_ColorMask: glColorMask((GLboolean)a[0].u, (GLboolean)a[1].u, (GLboolean)a[2].u, (GLboolean)a[3].u); break;
        case OP_CompileShader: glCompileShader(a[0].u); break;
        case OP_CullFace: glCullFace(a[0].u); break;
        case OP_DeleteBuffers: glDeleteBuffers(a[0].i, (const GLuint*)a[1].p); break;
        case OP_DeleteFramebuffers: glDeleteFramebuffers(a[0].i, (const GLuint*)a[1].p); break;
        case OP_DeleteProgram: glDeleteProgram(a[0].u); break;
        case OP_DeleteRenderbuffers: glDeleteRenderbuffers(a[0].i, (const GLuint*)a[1].p); break;
        case OP_DeleteShader: glDeleteShader(a[0].u); break;
        case OP_DeleteTextures: glDeleteTextures(a[0].i, (const GLuint*)a[1].p); break;
        case OP_DepthFunc: glDepthFunc(a[0].u); break;
        case OP_DepthMask: glDepthMask((GLboolean)a[0].u); break;
        case OP_Disable: glDisable(a[0].u); break;
        case OP_DisableVertexAttribArray: glDisableVertexAttribArray(a[0].u); break;
        case OP_DrawArrays: if (!s_noDraw) glDrawArrays(a[0].u, a[1].i, a[2].i); break;
        case OP_DrawElements: if (!s_noDraw) glDrawElements(a[0].u, a[1].i, a[2].u, a[3].p); break;
        case OP_DrawClient: {
            const ClientDraw* d = (const ClientDraw*)a[0].p;
            if (s_noDraw) break;
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            for (unsigned i = 0; i < d->nAttr; i++)
                glVertexAttribPointer(d->attr[i].index, d->attr[i].size, d->attr[i].type,
                                      d->attr[i].norm, d->attr[i].stride, d->attr[i].ptr);
            if (d->itype != 0) {
                if (d->elemBuf != 0) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glDrawElements(d->mode, d->count, d->itype, d->indices);
                if (d->elemBuf != 0) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->elemBuf);
            } else {
                glDrawArrays(d->mode, d->first, d->count);
            }
            glBindBuffer(GL_ARRAY_BUFFER, d->arrayBuf);
            break;
        }
        case OP_Enable: glEnable(a[0].u); break;
        case OP_EnableVertexAttribArray: glEnableVertexAttribArray(a[0].u); break;
        case OP_FramebufferRenderbuffer: glFramebufferRenderbuffer(a[0].u, a[1].u, a[2].u, a[3].u); break;
        case OP_FramebufferTexture2D: glFramebufferTexture2D(a[0].u, a[1].u, a[2].u, a[3].u, a[4].i); break;
        case OP_LinkProgram: glLinkProgram(a[0].u); break;
        case OP_PixelStorei: glPixelStorei(a[0].u, a[1].i); break;
        case OP_RenderbufferStorage: glRenderbufferStorage(a[0].u, a[1].u, a[2].i, a[3].i); break;
        case OP_ShaderSource: {
            const GLchar* src = (const GLchar*)a[1].p;
            glShaderSource(a[0].u, 1, &src, NULL);
            break;
        }
        case OP_TexImage2D:
            glTexImage2D(a[0].u, a[1].i, a[2].i, a[3].i, a[4].i, a[5].i, a[6].u, a[7].u, a[8].p);
            break;
        case OP_TexParameteri: glTexParameteri(a[0].u, a[1].u, a[2].i); break;
        case OP_Uniform1f: glUniform1f(a[0].i, a[1].f); break;
        case OP_Uniform1i: glUniform1i(a[0].i, a[1].i); break;
        case OP_Uniform2f: glUniform2f(a[0].i, a[1].f, a[2].f); break;
        case OP_Uniform4f: glUniform4f(a[0].i, a[1].f, a[2].f, a[3].f, a[4].f); break;
        case OP_Uniform4fv: glUniform4fv(a[0].i, a[1].i, (const GLfloat*)a[2].p); break;
        case OP_UseProgram: glUseProgram(a[0].u); break;
        case OP_VertexAttribPointer:
            glVertexAttribPointer(a[0].u, a[1].i, a[2].u, (GLboolean)a[3].u, a[4].i, a[5].p);
            break;
        case OP_Viewport: glViewport(a[0].i, a[1].i, a[2].i, a[3].i); break;
        case OP_DrawElementsInstanced: if (!s_noDraw) p_drawElementsInst(a[0].u, a[1].i, a[2].u, a[3].p, a[4].i); break;
        case OP_DrawArraysInstanced: if (!s_noDraw) p_drawArraysInst(a[0].u, a[1].i, a[2].i, a[3].i); break;
        case OP_VertexAttribDivisor: p_attribDivisor(a[0].u, a[1].u); break;
        default:
            fprintf(stderr, "glthread: bad op %u\n", op);
            abort();
        }
    }
}

static void* thread_main(void* arg)
{
    (void)arg;
    {
        // rgl_start waits to hear whether the context moved here.
        bool ok = r36s_video_make_current(true);
        pthread_mutex_lock(&s_mu);
        s_contextMoved = ok ? 1 : -1;
        pthread_cond_broadcast(&s_cv);
        pthread_mutex_unlock(&s_mu);
        if (!ok) return NULL;
    }
    s_cpuMark = thread_cpu_ms();
    for (;;) {
        Chunk* c;
        pthread_mutex_lock(&s_mu);
        while (s_queue == NULL) pthread_cond_wait(&s_cv, &s_mu);
        c = s_queue;
        s_queue = c->next;
        if (s_queue == NULL) s_queueTail = NULL;
        s_queued--;
        pthread_cond_broadcast(&s_cv);
        pthread_mutex_unlock(&s_mu);
        {
            s_chunkStart = now_ms();
            play(c);
            s_busyMs += now_ms() - s_chunkStart;
        }
        pthread_mutex_lock(&s_mu);
        if (s_freeN < MAX_FREE) {
            c->next = s_free;
            s_free = c;
            s_freeN++;
            c = NULL;
        }
        pthread_cond_broadcast(&s_cv);
        pthread_mutex_unlock(&s_mu);
        free(c);
    }
    return NULL;
}

// ----------------------------------------------------- the game's side

bool rgl_threaded(void) { return s_on; }

void rgl_frame_is(unsigned frame)
{
    if (s_drawFrom != 0 && frame >= s_drawFrom) {
        s_drawFrom = 0;
        s_noDraw = false;
    }
}

void rgl_start(void)
{
    const char* e = getenv("NFSHP2_GLTHREAD");
    if (s_on) return;
    if (e != NULL && e[0] == '0') {
        printf("R36S: GL thread off (NFSHP2_GLTHREAD=0)\n");
        return;
    }
    // NFSHP2_GLTHREAD=2: on whatever the backend.
    if (!r36s_video_thread_ok() && !(e != NULL && e[0] == '2')) {
        printf("R36S: GL thread off: not tried on the %s backend (NFSHP2_GLTHREAD=2 tries it)\n",
               r36s_video_driver());
        return;
    }
    rgl_instancing();
    pools_fill();
    s_cur = chunk_get();
    s_noDraw = getenv("NFSHP2_GL_NODRAW") != NULL;
    {
        // A test under emulation, where a frame takes half a minute: nothing
        // drawn until this frame, to reach a race's frames soon.
        const char* from = getenv("NFSHP2_GL_DRAWFROM");
        if (from != NULL) {
            s_drawFrom = (unsigned)atoi(from);
            s_noDraw = s_drawFrom > 0;
        }
    }
    present_start();
    r36s_video_make_current(false);
    if (pthread_create(&s_thread, NULL, thread_main, NULL) != 0) {
        r36s_video_make_current(true);
        printf("R36S: GL thread not started\n");
        return;
    }
    pthread_mutex_lock(&s_mu);
    while (s_contextMoved == 0) pthread_cond_wait(&s_cv, &s_mu);
    pthread_mutex_unlock(&s_mu);
    if (s_contextMoved < 0) {
        // This driver keeps its context on one thread: back to that one,
        // and the calls are made at once.
        pthread_join(s_thread, NULL);
        r36s_video_make_current(true);
        printf("R36S: GL thread off: the context doesn't move between threads here\n");
        return;
    }
#ifdef __GLIBC__
    pthread_setname_np(s_thread, "nfshp2-gl");
#endif
    s_on = true;
    printf("R36S: GL thread on\n");
}

void rgl_direct_begin(void)
{
    if (!s_on || s_direct++ > 0) return;
    rec0(OP_RELEASE, 0);
    submit();
    pthread_mutex_lock(&s_mu);
    while (!s_released) pthread_cond_wait(&s_cv, &s_mu);
    s_released = false;
    pthread_mutex_unlock(&s_mu);
    r36s_video_make_current(true);
}

void rgl_direct_end(void)
{
    if (!s_on || --s_direct > 0) return;
    r36s_video_make_current(false);
    pthread_mutex_lock(&s_mu);
    s_acquire = true;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mu);
}

#define LIVE (s_on && s_direct == 0)

void rgl_frame_end(const R36sGlCounts* counts)
{
    void* copy;
    bool heap;
    Arg* a;
    R36sGlCounts c = *counts;
    double entered = now_ms();
    c.gameUs = s_gameStart > 0 ? (unsigned)((entered - s_gameStart) * 1000.0) : 0;
    if (!s_on) {
        if (s_split) {
            present_hand();
            c.presentUs = (unsigned)(s_presentMs * 1000.0);
        } else {
            r36s_video_swap();
        }
        r36s_perf_frame_with(now_ms() - entered, &c);
        s_gameStart = now_ms();
        return;
    }
    a = rec(OP_SWAP, 1, sizeof c, &copy, &heap);
    c.waitUs = s_waitUs;
    memcpy(copy, &c, sizeof c);
    a[0].p = copy;
    submit();
    s_framesSent++;
    // One frame in flight: the thread may still be on the last one.
    {
        double t0 = now_ms();
        pthread_mutex_lock(&s_mu);
        while (s_framesDone + 1 < s_framesSent) pthread_cond_wait(&s_cv, &s_mu);
        pthread_mutex_unlock(&s_mu);
        s_gameStart = now_ms();
        s_waitUs = (unsigned)((s_gameStart - t0) * 1000.0);
    }
}

// A copy made for an op (rec's *copy): a large one is freed after it.
static void free_after(bool heap, void* copy)
{
    if (heap) rec0(OP_FreeBlob, 1)[0].p = copy;
}

// What the recorder knows of GL's state, to copy a client-array draw.
typedef struct {
    bool        enabled, client;
    GLint       size;
    GLenum      type;
    GLboolean   norm;
    GLsizei     stride;
    const void* ptr;
} AttrState;
static AttrState s_attr[16];
static GLuint s_arrayBuf, s_elemBuf;

static unsigned type_bytes(GLenum t)
{
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    default: return 4;
    }
}

// --------------------------------------------------- the GL functions

void rgl_ActiveTexture(GLenum a) { if (!LIVE) { glActiveTexture(a); return; } rec0(OP_ActiveTexture, 1)[0].u = a; after_rec(); }
void rgl_AttachShader(GLuint a, GLuint b) { if (!LIVE) { glAttachShader(a, b); return; } Arg* x = rec0(OP_AttachShader, 2); x[0].u = a; x[1].u = b; }

void rgl_BindAttribLocation(GLuint a, GLuint b, const GLchar* c)
{
    void* copy;
    bool heap;
    Arg* x;
    size_t n = strlen(c) + 1;
    if (!LIVE) { glBindAttribLocation(a, b, c); return; }
    x = rec(OP_BindAttribLocation, 3, n, &copy, &heap);
    memcpy(copy, c, n);
    x[0].u = a; x[1].u = b; x[2].p = copy;
    free_after(heap, copy);
}

void rgl_BindBuffer(GLenum a, GLuint b)
{
    if (a == GL_ARRAY_BUFFER) s_arrayBuf = b;
    else if (a == GL_ELEMENT_ARRAY_BUFFER) s_elemBuf = b;
    if (!LIVE) { glBindBuffer(a, b); return; }
    Arg* x = rec0(OP_BindBuffer, 2); x[0].u = a; x[1].u = b;
}

void rgl_BindFramebuffer(GLenum a, GLuint b) { if (!LIVE) { fb_bind(a, b); return; } Arg* x = rec0(OP_BindFramebuffer, 2); x[0].u = a; x[1].u = b; after_rec(); }
void rgl_BindRenderbuffer(GLenum a, GLuint b) { if (!LIVE) { glBindRenderbuffer(a, b); return; } Arg* x = rec0(OP_BindRenderbuffer, 2); x[0].u = a; x[1].u = b; }
void rgl_BindTexture(GLenum a, GLuint b) { if (!LIVE) { glBindTexture(a, b); return; } Arg* x = rec0(OP_BindTexture, 2); x[0].u = a; x[1].u = b; }
void rgl_BlendColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (!LIVE) { glBlendColor(a, b, c, d); return; } Arg* x = rec0(OP_BlendColor, 4); x[0].f = a; x[1].f = b; x[2].f = c; x[3].f = d; }
void rgl_BlendEquationSeparate(GLenum a, GLenum b) { if (!LIVE) { glBlendEquationSeparate(a, b); return; } Arg* x = rec0(OP_BlendEquationSeparate, 2); x[0].u = a; x[1].u = b; }
void rgl_BlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { if (!LIVE) { glBlendFuncSeparate(a, b, c, d); return; } Arg* x = rec0(OP_BlendFuncSeparate, 4); x[0].u = a; x[1].u = b; x[2].u = c; x[3].u = d; }

void rgl_BufferData(GLenum a, GLsizeiptr b, const void* c, GLenum d)
{
    void* copy = NULL;
    bool heap = false;
    Arg* x;
    if (!LIVE) { glBufferData(a, b, c, d); return; }
    x = c != NULL && b > 0 ? rec(OP_BufferData, 4, (size_t)b, &copy, &heap) : rec0(OP_BufferData, 4);
    if (copy) memcpy(copy, c, (size_t)b);
    x[0].u = a; x[1].u = (uint32_t)b; x[2].p = copy; x[3].u = d;
    free_after(heap, copy);
    after_rec();
}

void rgl_Clear(GLbitfield a) { if (!LIVE) { glClear(a); return; } rec0(OP_Clear, 1)[0].u = a; after_rec(); }
void rgl_ClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { if (!LIVE) { glClearColor(a, b, c, d); return; } Arg* x = rec0(OP_ClearColor, 4); x[0].f = a; x[1].f = b; x[2].f = c; x[3].f = d; }
void rgl_ClearDepthf(GLfloat a) { if (!LIVE) { glClearDepthf(a); return; } rec0(OP_ClearDepthf, 1)[0].f = a; }
void rgl_ClearStencil(GLint a) { if (!LIVE) { glClearStencil(a); return; } rec0(OP_ClearStencil, 1)[0].i = a; }
void rgl_ColorMask(GLboolean a, GLboolean b, GLboolean c, GLboolean d) { if (!LIVE) { glColorMask(a, b, c, d); return; } Arg* x = rec0(OP_ColorMask, 4); x[0].u = a; x[1].u = b; x[2].u = c; x[3].u = d; }
void rgl_CompileShader(GLuint a) { if (!LIVE) { glCompileShader(a); return; } rec0(OP_CompileShader, 1)[0].u = a; }
void rgl_CullFace(GLenum a) { if (!LIVE) { glCullFace(a); return; } rec0(OP_CullFace, 1)[0].u = a; }

static void rec_names(unsigned op, GLsizei n, const GLuint* names)
{
    void* copy;
    bool heap;
    Arg* x = rec(op, 2, (size_t)n * sizeof *names, &copy, &heap);
    memcpy(copy, names, (size_t)n * sizeof *names);
    x[0].i = n;
    x[1].p = copy;
    free_after(heap, copy);
}

void rgl_DeleteBuffers(GLsizei a, const GLuint* b)
{
    for (GLsizei i = 0; i < a; i++) {
        if (b[i] == s_arrayBuf) s_arrayBuf = 0;
        if (b[i] == s_elemBuf) s_elemBuf = 0;
    }
    if (!LIVE) { glDeleteBuffers(a, b); return; }
    if (a > 0) rec_names(OP_DeleteBuffers, a, b);
}
void rgl_DeleteFramebuffers(GLsizei a, const GLuint* b) { if (!LIVE) { glDeleteFramebuffers(a, b); return; } if (a > 0) rec_names(OP_DeleteFramebuffers, a, b); }
void rgl_DeleteProgram(GLuint a) { if (!LIVE) { glDeleteProgram(a); return; } rec0(OP_DeleteProgram, 1)[0].u = a; }
void rgl_DeleteRenderbuffers(GLsizei a, const GLuint* b) { if (!LIVE) { glDeleteRenderbuffers(a, b); return; } if (a > 0) rec_names(OP_DeleteRenderbuffers, a, b); }
void rgl_DeleteShader(GLuint a) { if (!LIVE) { glDeleteShader(a); return; } rec0(OP_DeleteShader, 1)[0].u = a; }
void rgl_DeleteTextures(GLsizei a, const GLuint* b) { if (!LIVE) { glDeleteTextures(a, b); return; } if (a > 0) rec_names(OP_DeleteTextures, a, b); }
void rgl_DepthFunc(GLenum a) { if (!LIVE) { glDepthFunc(a); return; } rec0(OP_DepthFunc, 1)[0].u = a; }
void rgl_DepthMask(GLboolean a) { if (!LIVE) { glDepthMask(a); return; } rec0(OP_DepthMask, 1)[0].u = a; }
void rgl_Disable(GLenum a) { if (!LIVE) { glDisable(a); return; } rec0(OP_Disable, 1)[0].u = a; }

void rgl_DisableVertexAttribArray(GLuint a)
{
    if (a < 16) s_attr[a].enabled = false;
    if (!LIVE) { glDisableVertexAttribArray(a); return; }
    rec0(OP_DisableVertexAttribArray, 1)[0].u = a;
}

void rgl_EnableVertexAttribArray(GLuint a)
{
    if (a < 16) s_attr[a].enabled = true;
    if (!LIVE) { glEnableVertexAttribArray(a); return; }
    rec0(OP_EnableVertexAttribArray, 1)[0].u = a;
}

// A draw: when an enabled attribute reads client memory, the vertices it
// reaches (and client-side indices) are copied with the draw.
static bool any_client(void)
{
    for (int i = 0; i < 16; i++)
        if (s_attr[i].enabled && s_attr[i].client) return true;
    return false;
}

static void rec_client_draw(GLenum mode, GLint first, GLsizei count, GLenum itype,
                            const void* indices)
{
    ClientDraw d;
    size_t total = sizeof d, ibytes = 0;
    GLint lo = first, hi = first + count - 1;
    size_t off[16];
    void* copy;
    bool heap;
    unsigned char* m;
    memset(&d, 0, sizeof d);
    if (itype != 0) {
        lo = 0x7FFFFFFF;
        hi = -1;
        if (itype == GL_UNSIGNED_SHORT) {
            const uint16_t* ix = (const uint16_t*)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (ix[i] < lo) lo = ix[i];
                if (ix[i] > hi) hi = ix[i];
            }
            ibytes = (size_t)count * 2;
        } else {
            const uint8_t* ix = (const uint8_t*)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (ix[i] < lo) lo = ix[i];
                if (ix[i] > hi) hi = ix[i];
            }
            ibytes = (size_t)count;
        }
        if (hi < lo) return;   // nothing drawn
    }
    for (GLuint i = 0; i < 16; i++) {
        const AttrState* s = &s_attr[i];
        if (!s->enabled || !s->client) continue;
        unsigned elem = (unsigned)s->size * type_bytes(s->type);
        size_t stride = s->stride ? (size_t)s->stride : elem;
        off[d.nAttr] = total;
        total += (((size_t)(hi - lo) * stride + elem) + 7) & ~(size_t)7;
        d.attr[d.nAttr].index = i;
        d.attr[d.nAttr].size = s->size;
        d.attr[d.nAttr].type = s->type;
        d.attr[d.nAttr].norm = s->norm;
        d.attr[d.nAttr].stride = (GLsizei)stride;
        d.nAttr++;
    }
    total += ibytes;
    {
        Arg* x = rec(OP_DrawClient, 1, total, &copy, &heap);
        x[0].p = copy;
    }
    m = (unsigned char*)copy;
    for (unsigned k = 0; k < d.nAttr; k++) {
        const AttrState* s = &s_attr[d.attr[k].index];
        size_t stride = (size_t)d.attr[k].stride;
        unsigned elem = (unsigned)s->size * type_bytes(s->type);
        memcpy(m + off[k], (const unsigned char*)s->ptr + (size_t)lo * stride,
               (size_t)(hi - lo) * stride + elem);
        d.attr[k].ptr = m + off[k] - (size_t)lo * stride;
    }
    if (ibytes) {
        memcpy(m + total - ibytes, indices, ibytes);
        d.indices = m + total - ibytes;
    }
    d.mode = mode;
    d.itype = itype;
    d.first = first;
    d.count = count;
    d.arrayBuf = s_arrayBuf;
    d.elemBuf = s_elemBuf;
    memcpy(m, &d, sizeof d);
    free_after(heap, copy);
    after_rec();
}

void rgl_DrawArrays(GLenum a, GLint b, GLsizei c)
{
    if (!LIVE) { glDrawArrays(a, b, c); return; }
    if (c <= 0) return;
    if (any_client()) { rec_client_draw(a, b, c, 0, NULL); return; }
    Arg* x = rec0(OP_DrawArrays, 3); x[0].u = a; x[1].i = b; x[2].i = c;
    after_rec();
}

void rgl_DrawElements(GLenum a, GLsizei b, GLenum c, const void* d)
{
    if (!LIVE) { glDrawElements(a, b, c, d); return; }
    if (b <= 0) return;
    if (s_elemBuf == 0) {
        // Client-side indices: copied, with any client-side vertices.
        if (any_client()) { rec_client_draw(a, 0, b, c, d); return; }
        {
            void* copy;
            bool heap;
            size_t n = (size_t)b * (c == GL_UNSIGNED_SHORT ? 2 : 1);
            Arg* x = rec(OP_DrawElements, 4, n, &copy, &heap);
            memcpy(copy, d, n);
            x[0].u = a; x[1].i = b; x[2].u = c; x[3].p = copy;
            free_after(heap, copy);
        }
    } else if (any_client()) {
        // Client vertices with an index buffer: the range isn't known
        // here, so the draw is made at once with the context lent.
        static bool told;
        if (!told) { told = true; printf("glthread: a client-array draw with an index buffer, made at once\n"); }
        rgl_direct_begin();
        for (GLuint i = 0; i < 16; i++)
            if (s_attr[i].enabled && s_attr[i].client) {
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glVertexAttribPointer(i, s_attr[i].size, s_attr[i].type, s_attr[i].norm,
                                      s_attr[i].stride, s_attr[i].ptr);
            }
        glBindBuffer(GL_ARRAY_BUFFER, s_arrayBuf);
        glDrawElements(a, b, c, d);
        rgl_direct_end();
        return;
    } else {
        Arg* x = rec0(OP_DrawElements, 4); x[0].u = a; x[1].i = b; x[2].u = c; x[3].p = d;
    }
    after_rec();
}

void rgl_Enable(GLenum a) { if (!LIVE) { glEnable(a); return; } rec0(OP_Enable, 1)[0].u = a; }
void rgl_FramebufferRenderbuffer(GLenum a, GLenum b, GLenum c, GLuint d) { if (!LIVE) { glFramebufferRenderbuffer(a, b, c, d); return; } Arg* x = rec0(OP_FramebufferRenderbuffer, 4); x[0].u = a; x[1].u = b; x[2].u = c; x[3].u = d; }
void rgl_FramebufferTexture2D(GLenum a, GLenum b, GLenum c, GLuint d, GLint e) { if (!LIVE) { glFramebufferTexture2D(a, b, c, d, e); return; } Arg* x = rec0(OP_FramebufferTexture2D, 5); x[0].u = a; x[1].u = b; x[2].u = c; x[3].u = d; x[4].i = e; }

static void gen_pooled(GLuint* pool, unsigned* n, GLsizei count, GLuint* out,
                       void (GL_APIENTRY* gen)(GLsizei, GLuint*))
{
    bool ok;
    if (!LIVE) { gen(count, out); return; }
    pthread_mutex_lock(&s_mu);
    ok = *n >= (unsigned)count;
    if (ok)
        for (GLsizei i = 0; i < count; i++) out[i] = pool[--*n];
    pthread_mutex_unlock(&s_mu);
    if (!ok) {
        rgl_direct_begin();
        gen(count, out);
        rgl_direct_end();
    }
}

void rgl_GenBuffers(GLsizei a, GLuint* b) { gen_pooled(s_bufPool, &s_bufN, a, b, glGenBuffers); }
void rgl_GenTextures(GLsizei a, GLuint* b) { gen_pooled(s_texPool, &s_texN, a, b, glGenTextures); }

// Calls whose results the caller needs: made with the context lent.
#define DIRECT(call) do { rgl_direct_begin(); call; rgl_direct_end(); } while (0)

void rgl_GenFramebuffers(GLsizei a, GLuint* b) { DIRECT(glGenFramebuffers(a, b)); }
void rgl_GenRenderbuffers(GLsizei a, GLuint* b) { DIRECT(glGenRenderbuffers(a, b)); }
GLenum rgl_CheckFramebufferStatus(GLenum a) { GLenum r; DIRECT(r = glCheckFramebufferStatus(a)); return r; }
GLuint rgl_CreateProgram(void) { GLuint r; DIRECT(r = glCreateProgram()); return r; }
GLuint rgl_CreateShader(GLenum a) { GLuint r; DIRECT(r = glCreateShader(a)); return r; }
GLenum rgl_GetError(void) { GLenum r; DIRECT(r = glGetError()); return r; }
void rgl_GetIntegerv(GLenum a, GLint* b) { DIRECT(glGetIntegerv(a, b)); }
void rgl_GetProgramInfoLog(GLuint a, GLsizei b, GLsizei* c, GLchar* d) { DIRECT(glGetProgramInfoLog(a, b, c, d)); }
void rgl_GetProgramiv(GLuint a, GLenum b, GLint* c) { DIRECT(glGetProgramiv(a, b, c)); }
void rgl_GetShaderInfoLog(GLuint a, GLsizei b, GLsizei* c, GLchar* d) { DIRECT(glGetShaderInfoLog(a, b, c, d)); }
void rgl_GetShaderiv(GLuint a, GLenum b, GLint* c) { DIRECT(glGetShaderiv(a, b, c)); }
const GLubyte* rgl_GetString(GLenum a) { const GLubyte* r; DIRECT(r = glGetString(a)); return r; }
GLint rgl_GetUniformLocation(GLuint a, const GLchar* b) { GLint r; DIRECT(r = glGetUniformLocation(a, b)); return r; }
GLboolean rgl_IsEnabled(GLenum a) { GLboolean r; DIRECT(r = glIsEnabled(a)); return r; }
void rgl_ReadPixels(GLint a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, void* g) { DIRECT(glReadPixels(a, b, c, d, e, f, g)); }

void rgl_LinkProgram(GLuint a) { if (!LIVE) { glLinkProgram(a); return; } rec0(OP_LinkProgram, 1)[0].u = a; }

static GLint s_unpackAlign = 4;

void rgl_PixelStorei(GLenum a, GLint b)
{
    if (a == GL_UNPACK_ALIGNMENT) s_unpackAlign = b;
    if (!LIVE) { glPixelStorei(a, b); return; }
    Arg* x = rec0(OP_PixelStorei, 2); x[0].u = a; x[1].i = b;
}

void rgl_RenderbufferStorage(GLenum a, GLenum b, GLsizei c, GLsizei d) { if (!LIVE) { glRenderbufferStorage(a, b, c, d); return; } Arg* x = rec0(OP_RenderbufferStorage, 4); x[0].u = a; x[1].u = b; x[2].i = c; x[3].i = d; }

void rgl_ShaderSource(GLuint a, GLsizei b, const GLchar* const* c, const GLint* d)
{
    size_t n = 1;
    void* copy;
    bool heap;
    char* t;
    Arg* x;
    if (!LIVE) { glShaderSource(a, b, c, d); return; }
    for (GLsizei i = 0; i < b; i++) n += d && d[i] >= 0 ? (size_t)d[i] : strlen(c[i]);
    x = rec(OP_ShaderSource, 2, n, &copy, &heap);
    t = (char*)copy;
    for (GLsizei i = 0; i < b; i++) {
        size_t k = d && d[i] >= 0 ? (size_t)d[i] : strlen(c[i]);
        memcpy(t, c[i], k);
        t += k;
    }
    *t = '\0';
    x[0].u = a;
    x[1].p = copy;
    free_after(heap, copy);
}

static unsigned pixel_bytes(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4
        || type == GL_UNSIGNED_SHORT_5_5_5_1)
        return 2;
    switch (format) {
    case GL_RGBA: return 4;
    case GL_RGB: return 3;
    case GL_LUMINANCE_ALPHA: return 2;
    default: return 1;
    }
}

void rgl_TexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g,
                    GLenum h, const void* i)
{
    void* copy = NULL;
    bool heap = false;
    size_t n = 0;
    Arg* x;
    if (!LIVE) { glTexImage2D(a, b, c, d, e, f, g, h, i); return; }
    if (i != NULL && d > 0 && e > 0) {
        size_t row = (size_t)d * pixel_bytes(g, h);
        size_t al = (size_t)(s_unpackAlign > 0 ? s_unpackAlign : 1);
        size_t pitch = (row + al - 1) / al * al;
        n = pitch * (size_t)(e - 1) + row;
    }
    x = n ? rec(OP_TexImage2D, 9, n, &copy, &heap) : rec0(OP_TexImage2D, 9);
    if (copy) memcpy(copy, i, n);
    x[0].u = a; x[1].i = b; x[2].i = c; x[3].i = d; x[4].i = e; x[5].i = f; x[6].u = g; x[7].u = h;
    x[8].p = copy;
    free_after(heap, copy);
    after_rec();
}

// The same pixels into a texture level that already has its storage (only
// a whole level is ever replaced).
void rgl_TexWhole2D(GLenum a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, const void* g)
{
    void* copy = NULL;
    bool heap = false;
    size_t row, al, pitch, n;
    Arg* x;
    if (!LIVE) { glTexSubImage2D(a, b, 0, 0, c, d, e, f, g); return; }
    row = (size_t)c * pixel_bytes(e, f);
    al = (size_t)(s_unpackAlign > 0 ? s_unpackAlign : 1);
    pitch = (row + al - 1) / al * al;
    n = pitch * (size_t)(d - 1) + row;
    x = rec(OP_TexSubImage2D, 7, n, &copy, &heap);
    memcpy(copy, g, n);
    x[0].u = a; x[1].i = b; x[2].i = c; x[3].i = d; x[4].u = e; x[5].u = f; x[6].p = copy;
    free_after(heap, copy);
    after_rec();
}

void rgl_TexParameteri(GLenum a, GLenum b, GLint c) { if (!LIVE) { glTexParameteri(a, b, c); return; } Arg* x = rec0(OP_TexParameteri, 3); x[0].u = a; x[1].u = b; x[2].i = c; }
void rgl_Uniform1f(GLint a, GLfloat b) { if (!LIVE) { glUniform1f(a, b); return; } Arg* x = rec0(OP_Uniform1f, 2); x[0].i = a; x[1].f = b; }
void rgl_Uniform1i(GLint a, GLint b) { if (!LIVE) { glUniform1i(a, b); return; } Arg* x = rec0(OP_Uniform1i, 2); x[0].i = a; x[1].i = b; }
void rgl_Uniform2f(GLint a, GLfloat b, GLfloat c) { if (!LIVE) { glUniform2f(a, b, c); return; } Arg* x = rec0(OP_Uniform2f, 3); x[0].i = a; x[1].f = b; x[2].f = c; }
void rgl_Uniform4f(GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e) { if (!LIVE) { glUniform4f(a, b, c, d, e); return; } Arg* x = rec0(OP_Uniform4f, 5); x[0].i = a; x[1].f = b; x[2].f = c; x[3].f = d; x[4].f = e; }

void rgl_Uniform4fv(GLint a, GLsizei b, const GLfloat* c)
{
    void* copy;
    bool heap;
    Arg* x;
    if (!LIVE) { glUniform4fv(a, b, c); return; }
    if (b <= 0) return;
    x = rec(OP_Uniform4fv, 3, (size_t)b * 16, &copy, &heap);
    memcpy(copy, c, (size_t)b * 16);
    x[0].i = a; x[1].i = b; x[2].p = copy;
    free_after(heap, copy);
}

void rgl_UseProgram(GLuint a) { if (!LIVE) { glUseProgram(a); return; } rec0(OP_UseProgram, 1)[0].u = a; }

void rgl_VertexAttribPointer(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void* f)
{
    if (a < 16) {
        s_attr[a].client = s_arrayBuf == 0;
        s_attr[a].size = b;
        s_attr[a].type = c;
        s_attr[a].norm = d;
        s_attr[a].stride = e;
        s_attr[a].ptr = f;
    }
    if (!LIVE) { glVertexAttribPointer(a, b, c, d, e, f); return; }
    if (s_arrayBuf == 0) return;   // set with the draw that reads it
    Arg* x = rec0(OP_VertexAttribPointer, 6);
    x[0].u = a; x[1].i = b; x[2].u = c; x[3].u = d; x[4].i = e; x[5].p = f;
}

void rgl_Viewport(GLint a, GLint b, GLsizei c, GLsizei d) { if (!LIVE) { glViewport(a, b, c, d); return; } Arg* x = rec0(OP_Viewport, 4); x[0].i = a; x[1].i = b; x[2].i = c; x[3].i = d; }

// ------------------------------------------------------------ GLES 3

bool rgl_instancing(void)
{
    if (s_instResolved < 0) {
        // SDL's lookup, else libGLESv2's own symbols (a headless run's EGL
        // context isn't SDL's).
        p_drawElementsInst = (FnDrawElementsInst)SDL_GL_GetProcAddress("glDrawElementsInstanced");
        p_drawArraysInst = (FnDrawArraysInst)SDL_GL_GetProcAddress("glDrawArraysInstanced");
        p_attribDivisor = (FnAttribDivisor)SDL_GL_GetProcAddress("glVertexAttribDivisor");
        if (!p_drawElementsInst) p_drawElementsInst = (FnDrawElementsInst)dlsym(RTLD_DEFAULT, "glDrawElementsInstanced");
        if (!p_drawArraysInst) p_drawArraysInst = (FnDrawArraysInst)dlsym(RTLD_DEFAULT, "glDrawArraysInstanced");
        if (!p_attribDivisor) p_attribDivisor = (FnAttribDivisor)dlsym(RTLD_DEFAULT, "glVertexAttribDivisor");
        s_instResolved = p_drawElementsInst && p_drawArraysInst && p_attribDivisor;
        p_getProgramBinary = (FnGetProgramBinary)SDL_GL_GetProcAddress("glGetProgramBinary");
        p_programBinary = (FnProgramBinary)SDL_GL_GetProcAddress("glProgramBinary");
        p_programParameteri = (FnProgramParameteri)SDL_GL_GetProcAddress("glProgramParameteri");
        p_invalidateFramebuffer = (FnInvalidateFramebuffer)SDL_GL_GetProcAddress("glInvalidateFramebuffer");
        if (!p_getProgramBinary) p_getProgramBinary = (FnGetProgramBinary)dlsym(RTLD_DEFAULT, "glGetProgramBinary");
        if (!p_programBinary) p_programBinary = (FnProgramBinary)dlsym(RTLD_DEFAULT, "glProgramBinary");
        if (!p_programParameteri) p_programParameteri = (FnProgramParameteri)dlsym(RTLD_DEFAULT, "glProgramParameteri");
        if (!p_invalidateFramebuffer) p_invalidateFramebuffer = (FnInvalidateFramebuffer)dlsym(RTLD_DEFAULT, "glInvalidateFramebuffer");
    }
    return s_instResolved == 1;
}

void rgl_DrawElementsInstanced(GLenum a, GLsizei b, GLenum c, const void* d, GLsizei e)
{
    if (!LIVE) { p_drawElementsInst(a, b, c, d, e); return; }
    Arg* x = rec0(OP_DrawElementsInstanced, 5);
    x[0].u = a; x[1].i = b; x[2].u = c; x[3].p = d; x[4].i = e;
    after_rec();
}

void rgl_DrawArraysInstanced(GLenum a, GLint b, GLsizei c, GLsizei d)
{
    if (!LIVE) { p_drawArraysInst(a, b, c, d); return; }
    Arg* x = rec0(OP_DrawArraysInstanced, 4);
    x[0].u = a; x[1].i = b; x[2].i = c; x[3].i = d;
    after_rec();
}

bool rgl_direct_active(void) { return !s_on || s_direct > 0; }

bool rgl_program_binaries(void)
{
    rgl_instancing();
    return p_getProgramBinary && p_programBinary && p_programParameteri;
}

bool rgl_invalidation(void)
{
    rgl_instancing();
    return p_invalidateFramebuffer != NULL;
}

void rgl_GetProgramBinary(GLuint a, GLsizei b, GLsizei* c, GLenum* d, void* e) { DIRECT(p_getProgramBinary(a, b, c, d, e)); }
void rgl_ProgramBinary(GLuint a, GLenum b, const void* c, GLsizei d) { DIRECT(p_programBinary(a, b, c, d)); }
void rgl_ProgramParameteri(GLuint a, GLenum b, GLint c) { DIRECT(p_programParameteri(a, b, c)); }

void rgl_InvalidateDepth(GLenum target, GLenum depth, GLenum stencil)
{
    GLenum list[2] = { depth, stencil };
    if (!LIVE) { p_invalidateFramebuffer(target, 2, list); return; }
    Arg* x = rec0(OP_InvalidateFramebuffer, 3);
    x[0].u = target; x[1].u = depth; x[2].u = stencil;
}

void rgl_VertexAttribDivisor(GLuint a, GLuint b)
{
    if (!LIVE) { p_attribDivisor(a, b); return; }
    Arg* x = rec0(OP_VertexAttribDivisor, 2);
    x[0].u = a; x[1].u = b;
}
