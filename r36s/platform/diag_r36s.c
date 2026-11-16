// The 3DS build's diagnostics, as the R36S build has them: the hang
// watcher's trail (kept, so a crash report can still name the last mark),
// the heap's measures from glibc instead of newlib's internals, and the
// capture and core sampler, which read the PICA's buffers and the ARM11's
// cores and have nothing to read here.

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <3ds.h>
#include <citro3d.h>

#include "capture.h"
#include "lowheap.h"
#include "coresample.h"
#include "hangwatch.h"
#include "heapcfg.h"
#include "heapmap.h"
#include "r36s_platform.h"

// ------------------------------------------------------------ hangwatch

const char* volatile HangwatchTrail[HANGWATCH_TRAIL];
volatile uint32_t    HangwatchMarks;
volatile uint32_t    HangwatchSp;

static void (*s_hangLog)(const char* fmt, ...);

void hangwatch_start(void (*log)(const char* fmt, ...)) { s_hangLog = log; }
void hangwatch_stop(void) {}
void hangwatch_note(void (*fn)(char* buf, size_t n)) { (void)fn; }

bool hangwatch_main_loop(void)
{
    bool up;
    hangwatch_mark("aptMainLoop");
    up = aptMainLoop();
    hangwatch_mark("frame begun");
    return up;
}

void hangwatch_applet(bool in) { (void)in; }
void hangwatch_gpu_race(bool racing) { (void)racing; }
void hangwatch_gpu_begin(void) {}
void hangwatch_gpu_top(void* colour, void* depth) { (void)colour; (void)depth; }
void hangwatch_gpu_stage(const char* stage) { (void)stage; }
void hangwatch_gpu_frame_end(void) {}
void hangwatch_gpu_sends_begin(bool on) { (void)on; }
void hangwatch_gpu_send(void) {}
void hangwatch_gpu_send_now(void) {}
void hangwatch_gpu_sends_end(void) {}

void hangwatch_log(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (s_hangLog != NULL)
        s_hangLog("%s", buf);
    else
        printf("%s\n", buf);
}

// ---------------------------------------------------------------- heap

// What heapcfg.c measured of the 3DS's memory grant. Here the "heap" is
// glibc's arena with the linear budget beside it; heapGot is what the
// game may still use, from the device's memory (r36s_mem_report).
NfsHeapInfo g_heapInfo;

__attribute__((constructor(102))) static void heapinfo_init(void)
{
    long pages = sysconf(_SC_PHYS_PAGES), sz = sysconf(_SC_PAGESIZE);
    u32 total = (pages > 0 && sz > 0) ? (u32)((uint64_t)pages * sz > 0xFFFFFFFFu
                                              ? 0xFFFFFFFFu : (uint64_t)pages * sz)
                                      : 0;
    memset(&g_heapInfo, 0, sizeof g_heapInfo);
    g_heapInfo.commitMax = total;
    g_heapInfo.avail = total;
    g_heapInfo.heapAsked = g_heapInfo.heapGot = total / 2;
    g_heapInfo.linearAsked = g_heapInfo.linearGot = (u32)R36S_LINEAR_DEFAULT_MB << 20;
    g_heapInfo.heapTries = g_heapInfo.linearTries = 1;
}

void heapmap_take(HeapMap* m)
{
    struct lowheap_stats st;
    lowheap_stats(&st);
    r36s_heap_bounds_refresh();
    memset(m, 0, sizeof *m);
    m->used = (uint32_t)st.used;
    m->free = (uint32_t)st.free;
    m->top = (uint32_t)(st.end - st.top);
    m->largest = (uint32_t)st.largest;
    m->pieces = (uint32_t)st.pieces;
    m->ok = 1;
}

void heapmap_lines(void (*line)(const char* text), uint32_t min)
{
    char buf[160];
    HeapMap m;
    (void)min;
    heapmap_take(&m);
    snprintf(buf, sizeof buf, "heap: %u KB used, %u KB free in %u pieces",
             m.used / 1024, m.free / 1024, m.pieces);
    line(buf);
}

void heapmap_note(void* p, int size, const char* name) { (void)p; (void)size; (void)name; }
void heapmap_forget(void* p) { (void)p; }
void heapmap_named(void (*line)(const char* text), uint32_t lo, uint32_t hi)
{
    (void)line; (void)lo; (void)hi;
}

// ------------------------------------------------------ capture, sampler

volatile uint8_t CoreSampleStage;
volatile uint8_t CoreSamplePart;

bool coresample_start(void (*log)(const char* fmt, ...)) { (void)log; return false; }
void coresample_stop(void) {}
bool coresample_line(char* buf, size_t n) { if (n) buf[0] = '\0'; return false; }

void capture_race(bool racing) { (void)racing; }
bool capture_read(C3D_RenderTarget* top) { (void)top; return false; }
bool capture_ids_on(void) { return false; }
bool capture_lod0_on(void) { return false; }
void capture_offscreen(bool on) { (void)on; }
int  capture_next_id(void) { return 0; }
void capture_note(const char* fmt, ...) { (void)fmt; }
void capture_pause_frame(C3D_RenderTarget* top, bool paused) { (void)top; (void)paused; }
void capture_pause_note(const PauseFrameNote* n) { (void)n; }
