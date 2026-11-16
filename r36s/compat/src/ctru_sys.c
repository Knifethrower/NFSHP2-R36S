// libctru's system calls, OS clock, threads, locks, linear heap, applets
// and small services, for the R36S (3ds.h says what each stands for).

#define _GNU_SOURCE
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "lowheap.h"

#include <3ds.h>

#include "fpctl.h"
#include "r36s_platform.h"

// ------------------------------------------------------------ clocks

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

u64 svcGetSystemTick(void)
{
    // ns * 268111856 / 1e9 without overflowing for ~2000 years of uptime.
    uint64_t ns = mono_ns();
    return (ns / 1000000000ull) * SYSCLOCK_ARM11
         + (ns % 1000000000ull) * SYSCLOCK_ARM11 / 1000000000ull;
}

u64 osGetTime(void)
{
    // libctru counts milliseconds from 1900-01-01.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((u64)ts.tv_sec + 2208988800ull) * 1000ull + (u64)ts.tv_nsec / 1000000ull;
}

void svcSleepThread(s64 ns)
{
    struct timespec ts;
    if (ns <= 0) {
        sched_yield();
        return;
    }
    ts.tv_sec = (time_t)(ns / 1000000000ll);
    ts.tv_nsec = (long)(ns % 1000000000ll);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

void svcBreak(u32 breakReason)
{
    fprintf(stderr, "svcBreak(%u)\n", (unsigned)breakReason);
    fflush(NULL);
    abort();
}

// ------------------------------------------------- memory and limits

// The game checks there is room in citro3d's command buffer before a draw
// (trackview.c's CMDBUF_GUARD_WORDS) and skips the draw when not. The GL
// layer has no such buffer, so it always has room.
u32* gpuCmdBuf;
u32  gpuCmdBufSize = 0x40000000u;
u32  gpuCmdBufOffset;

// newlib's sbrk window and libctru's linear heap, which the 3DS code
// reports and threadguard bounds its report by. Here: the low heap
// (platform/lowheap.c), from its base to its top.
char* fake_heap_start;
char* fake_heap_end;
u32   __ctru_linear_heap;
u32   __ctru_linear_heap_size;

static pthread_mutex_t s_linearLock = PTHREAD_MUTEX_INITIALIZER;
static size_t s_linearUsed;
static size_t s_vramUsed;

void r36s_heap_bounds_refresh(void)
{
    struct lowheap_stats st;
    lowheap_stats(&st);
    fake_heap_start = (char*)st.base;
    fake_heap_end = (char*)st.top;
}

// The linear heap is the game's GPU-visible memory on the 3DS and has its
// own budget there; the game measures what is left before it loads. Here
// it is malloc'd memory with the same accounting, against a budget the
// launcher can set (NFSHP2_LINEAR_MB).
static size_t linear_budget(void)
{
    static size_t budget;
    if (budget == 0) {
        const char* e = getenv("NFSHP2_LINEAR_MB");
        long mb = e != NULL ? strtol(e, NULL, 10) : 0;
        budget = (size_t)(mb > 0 ? mb : R36S_LINEAR_DEFAULT_MB) << 20;
        __ctru_linear_heap_size = (u32)budget;
    }
    return budget;
}

void* linearMemAlign(size_t size, size_t alignment)
{
    void* p;
    if (alignment < 0x80) alignment = 0x80;
    p = memalign(alignment, size ? size : 1);
    if (p != NULL) {
        r36s_check_low(p, "linearMemAlign");
        pthread_mutex_lock(&s_linearLock);
        s_linearUsed += malloc_usable_size(p);
        pthread_mutex_unlock(&s_linearLock);
    }
    return p;
}

void* linearAlloc(size_t size)
{
    return linearMemAlign(size, 0x80);
}

void* linearRealloc(void* mem, size_t size)
{
    (void)mem;
    (void)size;
    return NULL;      // as libctru's: not supported
}

size_t linearGetSize(void* mem)
{
    return mem != NULL ? malloc_usable_size(mem) : 0;
}

void linearFree(void* mem)
{
    if (mem == NULL) return;
    // A baked mesh's block goes with its GPU buffers (c3d_buffers.c), and
    // a texture's pixels with its GL texture (c3d_gles.c).
    r36s_mesh_forget(mem);
    r36s_tex_memory_freed(mem);
    pthread_mutex_lock(&s_linearLock);
    s_linearUsed -= malloc_usable_size(mem);
    pthread_mutex_unlock(&s_linearLock);
    free(mem);
}

u32 linearSpaceFree(void)
{
    size_t budget = linear_budget(), used;
    pthread_mutex_lock(&s_linearLock);
    used = s_linearUsed;
    pthread_mutex_unlock(&s_linearLock);
    return used >= budget ? 0 : (u32)(budget - used);
}

void* vramMemAlign(size_t size, size_t alignment)
{
    void* p = linearMemAlign(size, alignment);
    if (p != NULL) s_vramUsed += size;
    return p;
}

void* vramAlloc(size_t size) { return vramMemAlign(size, 0x80); }
void* vramAllocAt(size_t size, int pos) { (void)pos; return vramAlloc(size); }

void vramFree(void* mem)
{
    if (mem == NULL) return;
    s_vramUsed -= s_vramUsed < linearGetSize(mem) ? s_vramUsed : linearGetSize(mem);
    linearFree(mem);
}

u32 vramSpaceFree(void)
{
    return s_vramUsed >= OS_VRAM_SIZE ? 0 : (u32)(OS_VRAM_SIZE - s_vramUsed);
}

Result svcControlMemory(u32* addr_out, u32 addr0, u32 addr1, u32 size,
                        MemOp op, MemPerm perm)
{
    (void)addr0; (void)addr1; (void)size; (void)op; (void)perm;
    if (addr_out) *addr_out = 0;
    return R36S_RESULT_UNSUPPORTED;
}

Result svcQueryMemory(MemInfo* info, PageInfo* out, u32 addr)
{
    if (info) memset(info, 0, sizeof *info);
    if (out) out->flags = 0;
    (void)addr;
    return R36S_RESULT_UNSUPPORTED;
}

// Type 0 param 1: the application region's bytes in use, which the port
// logs. Everything else reads 0.
Result svcGetSystemInfo(s64* out, u32 type, s32 param)
{
    if (out == NULL) return R36S_RESULT_UNSUPPORTED;
    *out = 0;
    if (type == 0 && param == 1) {
        struct lowheap_stats st;
        lowheap_stats(&st);
        *out = (s64)st.used;
    }
    return 0;
}

Result svcGetResourceLimit(Handle* resourceLimit, Handle process)
{
    (void)process;
    if (resourceLimit) *resourceLimit = 1;
    return 0;
}

static s64 commit_limit(void)
{
    long pages = sysconf(_SC_PHYS_PAGES), sz = sysconf(_SC_PAGESIZE);
    return pages > 0 && sz > 0 ? (s64)pages * sz : (s64)1 << 30;
}

Result svcGetResourceLimitLimitValues(s64* values, Handle resourceLimit,
                                      ResourceLimitType* names, s32 nameCount)
{
    (void)resourceLimit;
    for (s32 i = 0; i < nameCount; i++)
        values[i] = names[i] == RESLIMIT_COMMIT ? commit_limit() : 0;
    return 0;
}

Result svcGetResourceLimitCurrentValues(s64* values, Handle resourceLimit,
                                        ResourceLimitType* names, s32 nameCount)
{
    (void)resourceLimit;
    for (s32 i = 0; i < nameCount; i++) {
        values[i] = 0;
        if (names[i] == RESLIMIT_COMMIT) {
            struct lowheap_stats st;
            lowheap_stats(&st);
            values[i] = (s64)(st.top - st.base);
        }
    }
    return 0;
}

Result svcCloseHandle(Handle handle) { (void)handle; return 0; }

Result svcGetThreadPriority(s32* out, Handle handle)
{
    (void)handle;
    if (out) *out = 0x30;
    return 0;
}

Result svcFlushProcessDataCache(Handle process, u32 addr, u32 size)
{
    (void)process; (void)addr; (void)size;
    return 0;
}

u32 osConvertVirtToPhys(const void* vaddr)
{
    return (u32)(uintptr_t)vaddr;
}

void osSetSpeedupEnable(bool enable) { (void)enable; }
u32  osGetApplicationMemType(void) { return APPMEMTYPE_NEW3DS_178MB; }
bool envIsHomebrew(void) { return false; }

// ------------------------------------------------------------ threads

struct R36SThread {
    pthread_t  pt;
    ThreadFunc fn;
    void*      arg;
    void*      stack;
    bool       detached;
    bool       joined;
    volatile bool finished;
};

void r36s_thread_start(void);
void r36s_stack_guard(void* stack, bool on);

static __thread struct R36SThread* t_self;

static void* thread_entry(void* p)
{
    struct R36SThread* t = (struct R36SThread*)p;
    // libctru starts every thread in RunFast (flush-to-zero, default NaN),
    // as the main thread is (r36s_platform_init).
    fpctl_set((fpctl_get() & ~FPCTL_RZ) | FPCTL_RUNFAST);
    r36s_thread_start();
    t_self = t;
    t->fn(t->arg);
    t->finished = true;
    // A detached thread is still on its stack here, so the stack stays
    // allocated (a few hundred KB per detached thread that ends).
    if (t->detached) free(t);
    return NULL;
}

Thread threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size,
                    int prio, int core_id, bool detached)
{
    pthread_attr_t attr;
    struct R36SThread* t = (struct R36SThread*)calloc(1, sizeof *t);
    (void)prio;
    (void)core_id;
    if (t == NULL) return NULL;
    t->fn = entrypoint;
    t->arg = arg;
    t->detached = detached;
    pthread_attr_init(&attr);
    // The stack comes from the heap, below 4 GB, as the game may keep a
    // local's address in a 32-bit slot; pthread's own would be mmap'd high.
    // 64-bit frames are bigger than the 3DS's, so it is four times what
    // was asked, and at least 256 KB.
    stack_size = (stack_size * 4 + 0xFFFF) & ~(size_t)0xFFFF;
    if (stack_size < 256 * 1024) stack_size = 256 * 1024;
    stack_size += 0x10000;              // the guard below
    t->stack = memalign(0x10000, stack_size);
    if (t->stack == NULL) {
        free(t);
        return NULL;
    }
    r36s_stack_guard(t->stack, true);   // the lowest 64 KB: overflow faults
    pthread_attr_setstack(&attr, t->stack, stack_size);
    if (detached) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t->pt, &attr, thread_entry, t) != 0) {
        pthread_attr_destroy(&attr);
        r36s_stack_guard(t->stack, false);
        free(t->stack);
        free(t);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    return t;
}

Result threadJoin(Thread t, u64 timeout_ns)
{
    struct timespec ts;
    if (t == NULL) return 0;
    if (timeout_ns == U64_MAX) {
        pthread_join(t->pt, NULL);
        t->joined = true;
        return 0;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ns / 1000000000ull);
    ts.tv_nsec += (long)(timeout_ns % 1000000000ull);
    if (ts.tv_nsec >= 1000000000l) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000l;
    }
    if (pthread_timedjoin_np(t->pt, NULL, &ts) != 0) return (Result)0x09401BFE;
    t->joined = true;
    return 0;
}

void threadFree(Thread t)
{
    if (t == NULL || t->detached) return;
    // A thread that was never joined may still be running on its stack.
    if (t->joined) {
        r36s_stack_guard(t->stack, false);
        free(t->stack);
    }
    free(t);
}

Thread threadGetCurrent(void) { return t_self; }
Handle threadGetHandle(Thread t) { (void)t; return 0; }

void threadExit(int rc)
{
    (void)rc;
    if (t_self) t_self->finished = true;
    pthread_exit(NULL);
}

// --------------------------------------------------------- locking

static pthread_mutex_t s_initLock = PTHREAD_MUTEX_INITIALIZER;

void LightLock_Init(LightLock* lock)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&lock->m, &a);
    pthread_mutexattr_destroy(&a);
    __atomic_store_n(&lock->ready, 1, __ATOMIC_RELEASE);
}

// A LightLock zeroed in .bss and never initialised is usable on the 3DS.
static void lightlock_ready(LightLock* lock)
{
    if (__atomic_load_n(&lock->ready, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&s_initLock);
    if (!lock->ready) LightLock_Init(lock);
    pthread_mutex_unlock(&s_initLock);
}

void LightLock_Lock(LightLock* lock)
{
    lightlock_ready(lock);
    pthread_mutex_lock(&lock->m);
}

int LightLock_TryLock(LightLock* lock)
{
    lightlock_ready(lock);
    return pthread_mutex_trylock(&lock->m) == 0 ? 0 : 1;
}

void LightLock_Unlock(LightLock* lock)
{
    pthread_mutex_unlock(&lock->m);
}

void LightEvent_Init(LightEvent* e, ResetType reset_type)
{
    pthread_condattr_t ca;
    pthread_mutex_init(&e->m, NULL);
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&e->c, &ca);
    pthread_condattr_destroy(&ca);
    e->state = 0;
    e->type = reset_type;
}

void LightEvent_Clear(LightEvent* e)
{
    pthread_mutex_lock(&e->m);
    e->state = 0;
    pthread_mutex_unlock(&e->m);
}

void LightEvent_Pulse(LightEvent* e)
{
    pthread_mutex_lock(&e->m);
    pthread_cond_broadcast(&e->c);
    pthread_mutex_unlock(&e->m);
}

void LightEvent_Signal(LightEvent* e)
{
    pthread_mutex_lock(&e->m);
    if (e->type == RESET_PULSE) {
        pthread_cond_broadcast(&e->c);
    } else {
        e->state = 1;
        if (e->type == RESET_ONESHOT)
            pthread_cond_signal(&e->c);
        else
            pthread_cond_broadcast(&e->c);
    }
    pthread_mutex_unlock(&e->m);
}

// Called with the mutex held and the state set: a one-shot event is
// consumed by the waiter it wakes.
static void consume(LightEvent* e)
{
    if (e->type == RESET_ONESHOT) e->state = 0;
}

int LightEvent_TryWait(LightEvent* e)
{
    int got;
    pthread_mutex_lock(&e->m);
    got = e->state;
    if (got) consume(e);
    pthread_mutex_unlock(&e->m);
    return got;
}

void LightEvent_Wait(LightEvent* e)
{
    pthread_mutex_lock(&e->m);
    if (e->type == RESET_PULSE) {
        pthread_cond_wait(&e->c, &e->m);
    } else {
        while (!e->state)
            pthread_cond_wait(&e->c, &e->m);
        consume(e);
    }
    pthread_mutex_unlock(&e->m);
}

// As libctru's: 0 when signalled, 1 on the timeout.
int LightEvent_WaitTimeout(LightEvent* e, s64 timeout_ns)
{
    struct timespec ts;
    int rc = 0, timedOut = 0;
    uint64_t end = mono_ns() + (uint64_t)(timeout_ns > 0 ? timeout_ns : 0);
    ts.tv_sec = (time_t)(end / 1000000000ull);
    ts.tv_nsec = (long)(end % 1000000000ull);
    pthread_mutex_lock(&e->m);
    if (e->type == RESET_PULSE) {
        timedOut = pthread_cond_timedwait(&e->c, &e->m, &ts) == ETIMEDOUT;
    } else {
        while (!e->state && rc != ETIMEDOUT)
            rc = pthread_cond_timedwait(&e->c, &e->m, &ts);
        if (e->state)
            consume(e);
        else
            timedOut = 1;
    }
    pthread_mutex_unlock(&e->m);
    return timedOut;
}

// ------------------------------------------------- applets and services

Result APT_CheckNew3DS(bool* out)
{
    if (out) *out = true;
    return 0;
}

static u32 s_cpuLimit = 30;
Result APT_SetAppCpuTimeLimit(u32 percent) { s_cpuLimit = percent; return 0; }
Result APT_GetAppCpuTimeLimit(u32* percent) { if (percent) *percent = s_cpuLimit; return 0; }
void aptSetHomeAllowed(bool allowed) { (void)allowed; }
void aptSetSleepAllowed(bool allowed) { (void)allowed; }

bool aptMainLoop(void)
{
    return r36s_pump_events();
}

Result cfguInit(void) { return 0; }
void   cfguExit(void) {}
Result CFGU_GetSystemModel(u8* model)
{
    if (model) *model = CFG_MODEL_N3DSXL;
    return 0;
}
Result ptmSysmInit(void) { return 0; }
void   ptmSysmExit(void) {}

Result FSUSER_GetSdmcArchiveResource(FS_ArchiveResource* r)
{
    struct statvfs v;
    if (r == NULL) return R36S_RESULT_UNSUPPORTED;
    memset(r, 0, sizeof *r);
    if (statvfs(r36s_sdmc_root(), &v) != 0) return R36S_RESULT_UNSUPPORTED;
    r->sectorSize = 512;
    r->clusterSize = (u32)v.f_bsize;
    r->totalClusters = (u32)(v.f_blocks > 0xFFFFFFFFu ? 0xFFFFFFFFu : v.f_blocks);
    r->freeClusters = (u32)(v.f_bavail > 0xFFFFFFFFu ? 0xFFFFFFFFu : v.f_bavail);
    return 0;
}

PrintConsole* consoleInit(gfxScreen_t screen, PrintConsole* console)
{
    (void)screen;
    (void)console;
    return NULL;   // printf goes to stdout, which the launcher logs
}

Result GSPGPU_FlushDataCache(const void* adr, u32 size) { (void)adr; (void)size; return 0; }
Result GSPGPU_InvalidateDataCache(const void* adr, u32 size) { (void)adr; (void)size; return 0; }
Result GSPGPU_ReadHWRegs(u32 regAddr, u32* data, u8 size)
{
    (void)regAddr;
    if (data) memset(data, 0, size);
    return 0;
}
Result DSP_FlushDataCache(const void* address, u32 size) { (void)address; (void)size; return 0; }

void gxCmdQueueClear(gxCmdQueue_s* q) { if (q) q->numEntries = q->curEntry = q->lastEntry = 0; }
void gxCmdQueueRun(gxCmdQueue_s* q) { (void)q; }
void gxCmdQueueStop(gxCmdQueue_s* q) { (void)q; }
bool gxCmdQueueWait(gxCmdQueue_s* q, s64 timeout) { (void)q; (void)timeout; return true; }

Result GX_ProcessCommandList(u32* buf0a, u32 buf0s, u8 flags)
{
    (void)buf0a; (void)buf0s; (void)flags;
    return 0;
}

Result GX_MemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0,
                     u32* buf1a, u32 buf1v, u32* buf1e, u16 control1)
{
    (void)buf0a; (void)buf0v; (void)buf0e; (void)control0;
    (void)buf1a; (void)buf1v; (void)buf1e; (void)control1;
    return 0;
}

Result GX_DisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags)
{
    (void)inadr; (void)indim; (void)outadr; (void)outdim; (void)flags;
    return 0;
}

// The GPU's TextureCopy: `size` bytes, read as lines of (indim & 0xFFFF)
// * 16 bytes each followed by a gap of (indim >> 16) * 16, written the same
// way by outdim. A zero line width means one line.
Result GX_TextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim,
                      u32 size, u32 flags)
{
    const uint8_t* src = (const uint8_t*)inadr;
    uint8_t* dst = (uint8_t*)outadr;
    u32 inLine = (indim & 0xFFFF) * 16, inGap = (indim >> 16) * 16;
    u32 outLine = (outdim & 0xFFFF) * 16, outGap = (outdim >> 16) * 16;
    u32 inLeft, outLeft;
    (void)flags;
    if (inLine == 0) inLine = size;
    if (outLine == 0) outLine = size;
    inLeft = inLine;
    outLeft = outLine;
    while (size > 0) {
        u32 n = size;
        if (n > inLeft) n = inLeft;
        if (n > outLeft) n = outLeft;
        memcpy(dst, src, n);
        src += n; dst += n; size -= n;
        inLeft -= n; outLeft -= n;
        if (inLeft == 0) { src += inGap; inLeft = inLine; }
        if (outLeft == 0) { dst += outGap; outLeft = outLine; }
    }
    return 0;
}

// ----------------------------------------------------------- swkbd

void swkbdInit(SwkbdState* s, SwkbdType type, int numButtons, int maxTextLength)
{
    memset(s, 0, sizeof *s);
    s->type = type;
    s->numButtons = numButtons;
    s->maxTextLength = maxTextLength;
}
void swkbdSetFeatures(SwkbdState* s, u32 features) { s->features = features; }
void swkbdSetHintText(SwkbdState* s, const char* t) { snprintf(s->hint, sizeof s->hint, "%s", t ? t : ""); }
void swkbdSetInitialText(SwkbdState* s, const char* t) { snprintf(s->initial, sizeof s->initial, "%s", t ? t : ""); }
void swkbdSetValidation(SwkbdState* s, SwkbdValidInput v, u32 filterFlags, int maxDigits)
{
    s->validInput = v;
    s->filterFlags = filterFlags;
    s->maxDigits = maxDigits;
}
void swkbdSetFilterCallback(SwkbdState* s, SwkbdCallbackFn cb, void* user)
{
    s->filterCallback = cb;
    s->filterUser = user;
}

SwkbdButton swkbdInputText(SwkbdState* s, char* buf, size_t bufsize)
{
    // No keyboard applet: the initial text comes back, as if confirmed
    // unchanged, so a name the game proposes is kept.
    if (buf != NULL && bufsize > 0)
        snprintf(buf, bufsize, "%s", s->initial);
    s->result = SWKBD_D1_CLICK1;
    return s->initial[0] != '\0' ? SWKBD_BUTTON_CONFIRM : SWKBD_BUTTON_NONE;
}

SwkbdResult swkbdGetResult(SwkbdState* s) { return s->result; }
