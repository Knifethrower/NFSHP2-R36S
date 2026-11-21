// The R36S build's speed measures: the frame counter (Select + L3 or R3),
// a line a second in log.txt with the frame's times and what the GL layer
// did, and NFSHP2_PROFILE's CPU sampler.
//
// The frame is the time from one swap's end to the next's. Its "swap" part
// is the swap itself: eglSwapBuffers waits there for the display's vertical
// blank, and for the GPU when the GPU is behind. The rest is the CPU's: the
// game, and the GL calls that hand it to the driver.

#define _GNU_SOURCE
#include <dirent.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "r36s_platform.h"

// The effects' sprite counts (3DS Files/source/acidsys.h), logged with the
// memory line.
extern int      gAcidThin;
extern uint32_t gAcidSprites;
extern uint32_t gAcidDrawn;
extern float    gAcidAreaAllK;
extern float    gAcidAreaDrawnK;
extern float    gAcidBucketK[5];
extern uint32_t gAcidBucketN[5];
extern int      gAcidBigThin;

R36sGlCounts r36s_glcounts;
bool r36s_present_thread;
volatile int r36s_present_serial;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ------------------------------------------------------------ the meter

static int s_meter = -1;   // -1: not read from NFSHP2_SHOW_FPS yet
static char s_line1[32] = "-- FPS", s_line2[32] = "";

bool r36s_fps_meter_on(void)
{
    if (s_meter < 0) {
        const char* e = getenv("NFSHP2_SHOW_FPS");
        s_meter = e != NULL && e[0] == '1';
    }
    return s_meter == 1;
}

void r36s_fps_meter_toggle(void)
{
    s_meter = !r36s_fps_meter_on();
    printf("perf: frame counter %s\n", s_meter ? "on" : "off");
}

void r36s_fps_meter_text(char* line1, size_t n1, char* line2, size_t n2)
{
    snprintf(line1, n1, "%s", s_line1);
    snprintf(line2, n2, "%s", s_line2);
}

// ------------------------------------------------------------ the presets

static int s_quality = -1;

// FAST (1) unless NFSHP2_QUALITY or the menu's choice says otherwise: the
// user's choice of 2026-11-19, after a dense race on Full at 19-25 fps.
// NFSHP2_QUALITY=0 is the game as retail draws it.
#define QUALITY_DEFAULT 1

// Display Options' Graphics row keeps its choice here, one digit, beside
// the game's own saves: NFSHP2_QUALITY, when set, still wins at start.
#define QUALITY_SAVE_DIR  "sdmc:/NFSHP2/SAVES"
#define QUALITY_SAVE_FILE QUALITY_SAVE_DIR "/graphics.txt"

const char* r36s_path(const char* in, char* out, size_t n);   // compat's 3ds.h

int r36s_quality(void)
{
    if (s_quality < 0) {
        const char* e = getenv("NFSHP2_QUALITY");
        s_quality = QUALITY_DEFAULT;
        if (e != NULL && e[0] != '\0') {
            s_quality = atoi(e);
            printf("perf: quality %d (NFSHP2_QUALITY)\n", s_quality);
        } else {
            char path[512];
            FILE* f = fopen(r36s_path(QUALITY_SAVE_FILE, path, sizeof path), "r");
            int q;
            if (f != NULL) {
                if (fscanf(f, "%d", &q) == 1) {
                    s_quality = q;
                    printf("perf: quality %d (saved)\n", q);
                }
                fclose(f);
            }
        }
        if (s_quality < 0 || s_quality > 2) s_quality = QUALITY_DEFAULT;
    }
    return s_quality;
}

// The smoke row (Options' Display): 0 Full draws every big smoke and dust
// puff as the PS2 does; 1 Less and 2 Least draw one in two or three of
// them, and thin a dense cloud (NFSHP2_SMOKE_BUDGET). Kept beside the
// graphics choice; NFSHP2_PARTICLES and NFSHP2_SMOKE_BUDGET, when set,
// still win.
#define SMOKE_DEFAULT 1
#define SMOKE_SAVE_FILE QUALITY_SAVE_DIR "/smoke.txt"

static int s_smoke = -1;

int r36s_smoke(void)
{
    if (s_smoke < 0) {
        char path[512];
        FILE* f = fopen(r36s_path(SMOKE_SAVE_FILE, path, sizeof path), "r");
        int v;

        s_smoke = SMOKE_DEFAULT;
        if (f != NULL) {
            if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 2) s_smoke = v;
            fclose(f);
        }
    }
    return s_smoke;
}

void r36s_smoke_set(int v)
{
    char path[512];
    FILE* f;

    if (v < 0 || v > 2) return;
    s_smoke = v;
    printf("perf: smoke %d\n", v);
    mkdir(r36s_path(QUALITY_SAVE_DIR, path, sizeof path), 0755);
    f = fopen(r36s_path(SMOKE_SAVE_FILE, path, sizeof path), "w");
    if (f != NULL) {
        fprintf(f, "%d\n", v);
        fclose(f);
    } else {
        printf("perf: the smoke choice couldn't be saved to %s\n", path);
    }
}

void r36s_quality_set(int q)
{
    char path[512];
    FILE* f;

    if (q < 0 || q > 2) return;
    s_quality = q;
    printf("perf: quality %d\n", q);
    mkdir(r36s_path(QUALITY_SAVE_DIR, path, sizeof path), 0755);
    f = fopen(r36s_path(QUALITY_SAVE_FILE, path, sizeof path), "w");
    if (f != NULL) {
        fprintf(f, "%d\n", q);
        fclose(f);
    } else {
        printf("perf: the graphics choice couldn't be saved to %s\n", path);
    }
}

// The share of the screen's resolution drawn: NFSHP2_RENDER_SCALE, any value
// from 0.25 to 1 (0,75 is read as 0.75, and 25 to 100 as a percent, as this
// setting first was), else the preset's.
float r36s_render_scale(void)
{
    static float forced = -2.0f;
    if (forced == -2.0f) {
        const char* e = getenv("NFSHP2_RENDER_SCALE");
        forced = -1.0f;
        if (e != NULL && e[0] != '\0') {
            char text[32], *end;
            double v;
            snprintf(text, sizeof text, "%s", e);
            for (char* c = text; *c != '\0'; c++)
                if (*c == ',') *c = '.';
            v = strtod(text, &end);
            if (end != text && v >= 25.0 && v <= 100.0) v /= 100.0;
            if (end != text && v >= 0.25 && v <= 1.0) {
                forced = (float)v;
                printf("R36S: render scale %.3f (NFSHP2_RENDER_SCALE)\n", v);
            } else {
                printf("R36S: NFSHP2_RENDER_SCALE=%s ignored: it goes from 0.25 to 1\n", e);
            }
        }
    }
    return forced > 0.0f ? forced : r36s_quality() >= 2 ? 0.75f : 1.0f;
}

// A short note on screen for two seconds (the D-pad swap's).
#define TOAST_MS 2000.0
static char s_toast[32];
static double s_toastUntil;

void r36s_toast(const char* text)
{
    snprintf(s_toast, sizeof s_toast, "%s", text);
    s_toastUntil = now_ms() + TOAST_MS;
}

const char* r36s_toast_text(void)
{
    return s_toast[0] != '\0' && now_ms() < s_toastUntil ? s_toast : NULL;
}

// ------------------------------------------------------- the frame's end

// The meter's half-second window, and the log's two-second one.
static struct {
    double start;
    unsigned frames;
    double worst;
} s_meterWin;

static struct {
    double start;
    unsigned frames;
    double worst, swap, swapWorst;
    unsigned over17, over34;
    R36sGlCounts gl;
} s_logWin;

// The process's and the device's memory, for the perf line: a process that
// dies with status 137 was killed, most often for memory.
static void mem_text(char* out, size_t n)
{
    char line[160], gpu[48];
    long temp = 0;
    long rss = -1, swapped = -1, avail = -1, swapFree = -1, swapTotal = -1;
    unsigned long majflt = 0;
    FILE* f = fopen("/proc/self/status", "r");
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            if (sscanf(line, "VmRSS: %ld", &rss) == 1) continue;
            if (sscanf(line, "VmSwap: %ld", &swapped) == 1) continue;
        }
        fclose(f);
    }
    f = fopen("/proc/meminfo", "r");
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            if (sscanf(line, "MemAvailable: %ld", &avail) == 1) continue;
            if (sscanf(line, "SwapTotal: %ld", &swapTotal) == 1) continue;
            if (sscanf(line, "SwapFree: %ld", &swapFree) == 1) continue;
        }
        fclose(f);
    }
    f = fopen("/proc/self/stat", "r");
    if (f != NULL) {
        // Field 12, after the ")" that ends the name.
        if (fgets(line, sizeof line, f) != NULL) {
            const char* p = strrchr(line, ')');
            if (p != NULL)
                sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %lu", &majflt);
        }
        fclose(f);
    }
    // The GPU's own load ("busy@clock") and the SoC's temperature.
    gpu[0] = '\0';
    {
        // ff400000.gpu on the RK3326, another name on another SoC.
        static char path[128];
        static bool looked;
        if (!looked) {
            DIR* dir = opendir("/sys/class/devfreq");
            looked = true;
            if (dir != NULL) {
                const struct dirent* e;
                while ((e = readdir(dir)) != NULL)
                    if (strstr(e->d_name, "gpu") != NULL) {
                        snprintf(path, sizeof path, "/sys/class/devfreq/%.80s/load", e->d_name);
                        break;
                    }
                closedir(dir);
            }
        }
        f = path[0] != '\0' ? fopen(path, "r") : NULL;
    }
    if (f != NULL) {
        if (fgets(gpu, sizeof gpu, f) != NULL) gpu[strcspn(gpu, "\r\n")] = '\0';
        fclose(f);
    }
    f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f != NULL) {
        if (fscanf(f, "%ld", &temp) != 1) temp = 0;
        fclose(f);
    }
    // The effects' sprites in the last frame drawn (acidsys.h), with the
    // thinning in force: what a frame's smoke and dust costs.
    snprintf(out, n, "memory: RSS %ld MB (swapped %ld), available %ld MB, swap %ld of %ld MB free,"
             " %lu major faults; malloc %u MB, mmap %u MB; %u GL textures made; GPU %s, %ld C,"
             " quality %d; particles 1 in %d, big 1 in %d: %u sprites (%.0f kpx), %u drawn (%.0f kpx)",
             rss / 1024, swapped / 1024, avail / 1024, swapFree / 1024, swapTotal / 1024, majflt,
             (unsigned)(mallinfo().uordblks >> 20), (unsigned)(mallinfo().hblkhd >> 20),
             r36s_gl_textures_made, gpu[0] ? gpu : "?", temp / 1000, r36s_quality(), gAcidThin,
             gAcidBigThin, gAcidSprites, (double)gAcidAreaAllK, gAcidDrawn, (double)gAcidAreaDrawnK);
}

void r36s_perf_frame(double swapMs)
{
    R36sGlCounts c = r36s_glcounts;
    memset(&r36s_glcounts, 0, sizeof r36s_glcounts);
    r36s_perf_frame_with(swapMs, &c);
}

void r36s_perf_frame_with(double swapMs, const R36sGlCounts* counts)
{
    static double last;
    double now = now_ms(), frame;
    R36sGlCounts c = *counts;

    if (last == 0) {
        last = s_meterWin.start = s_logWin.start = now;
        return;
    }
    frame = now - last;
    last = now;

    // A frame far past the frame rate says what happened in it.
    if (frame > 66.0)
        printf("hitch: %.0f ms: game thread %.1f, GL busy %.1f, swap %.1f; %u programs (%u compiled,"
               " %.1f ms), %u uploads (%u KB), %u meshes made (%u KB), %u draws\n",
               frame, c.gameUs / 1000.0, c.glBusyUs / 1000.0, swapMs, c.progBuilds, c.progCompiled,
               c.progUs / 1000.0, c.uploads, c.uploadKB, c.bufUploads, c.bufUploadKB, c.draws);

    s_meterWin.frames++;
    if (frame > s_meterWin.worst) s_meterWin.worst = frame;
    if (now - s_meterWin.start >= 500.0) {
        double span = now - s_meterWin.start;
        snprintf(s_line1, sizeof s_line1, "%.1f FPS", s_meterWin.frames * 1000.0 / span);
        snprintf(s_line2, sizeof s_line2, "%.1f ms  max %.0f", span / s_meterWin.frames,
                 s_meterWin.worst);
        memset(&s_meterWin, 0, sizeof s_meterWin);
        s_meterWin.start = now;
    }

    s_logWin.frames++;
    if (frame > s_logWin.worst) s_logWin.worst = frame;
    if (frame > 17.5) s_logWin.over17++;
    if (frame > 34.0) s_logWin.over34++;
    s_logWin.swap += swapMs;
    if (swapMs > s_logWin.swapWorst) s_logWin.swapWorst = swapMs;
    s_logWin.gl.draws += c.draws;
    s_logWin.gl.programs += c.programs;
    s_logWin.gl.textures += c.textures;
    s_logWin.gl.uniforms += c.uniforms;
    s_logWin.gl.uploads += c.uploads;
    s_logWin.gl.uploadKB += c.uploadKB;
    s_logWin.gl.targets += c.targets;
    s_logWin.gl.quads2d += c.quads2d;
    s_logWin.gl.flush2d += c.flush2d;
    s_logWin.gl.bufDraws += c.bufDraws;
    s_logWin.gl.bufUploads += c.bufUploads;
    s_logWin.gl.bufUploadKB += c.bufUploadKB;
    s_logWin.gl.held += c.held;
    s_logWin.gl.groups += c.groups;
    s_logWin.gl.glBusyUs += c.glBusyUs;
    s_logWin.gl.waitUs += c.waitUs;
    s_logWin.gl.gameUs += c.gameUs;
    s_logWin.gl.playCpuUs += c.playCpuUs;
    s_logWin.gl.swapCpuUs += c.swapCpuUs;
    s_logWin.gl.progBuilds += c.progBuilds;
    s_logWin.gl.progCompiled += c.progCompiled;
    s_logWin.gl.progUs += c.progUs;
    s_logWin.gl.presentUs += c.presentUs;
    if (now - s_logWin.start >= 2000.0) {
        double span = now - s_logWin.start, n = s_logWin.frames;
        double mean = span / n, swap = s_logWin.swap / n;
        printf("perf: %.1f s, %.1f fps, frame %.1f ms (max %.1f, %u over 17.5, %u over 34),"
               " cpu %.1f, swap %.1f (max %.1f); a frame: %.0f draws, %.0f programs, %.0f textures,"
               " %.0f uniforms, %.0f targets, %.1f uploads (%.0f KB), %.0f 2D quads in %.0f;"
               " %.0f draws from mesh buffers, %u made (%u KB); %.0f grouped into %.0f;"
               " GL thread busy %.1f ms (CPU %.1f), swap CPU %.1f; game thread %.1f ms, waited %.1f;"
               " programs %u (%u compiled, %.0f ms)\n",
               span / 1000.0, n * 1000.0 / span, mean, s_logWin.worst, s_logWin.over17,
               s_logWin.over34, mean - swap, swap, s_logWin.swapWorst, s_logWin.gl.draws / n,
               s_logWin.gl.programs / n, s_logWin.gl.textures / n, s_logWin.gl.uniforms / n,
               s_logWin.gl.targets / n, s_logWin.gl.uploads / n, s_logWin.gl.uploadKB / n,
               s_logWin.gl.quads2d / n, s_logWin.gl.flush2d / n, s_logWin.gl.bufDraws / n,
               s_logWin.gl.bufUploads, s_logWin.gl.bufUploadKB, s_logWin.gl.held / n,
               s_logWin.gl.groups / n, s_logWin.gl.glBusyUs / n / 1000.0,
               s_logWin.gl.playCpuUs / n / 1000.0, s_logWin.gl.swapCpuUs / n / 1000.0,
               s_logWin.gl.gameUs / n / 1000.0, s_logWin.gl.waitUs / n / 1000.0,
               s_logWin.gl.progBuilds, s_logWin.gl.progCompiled,
               s_logWin.gl.progUs / 1000.0);
        if (r36s_present_thread) {
            // NFSHP2_PRESENT_AB=N: the next N seconds in the other order.
            static int every = -1, windows;
            if (every < 0) {
                const char* e = getenv("NFSHP2_PRESENT_AB");
                every = e ? atoi(e) / 2 : 0;
                if (every < 0) every = 0;
            }
            printf("present: %s, the present thread's swap %.1f ms\n",
                   r36s_present_serial ? "the GL thread waits for each swap (A/B)"
                                       : "the GL thread goes on",
                   s_logWin.gl.presentUs / n / 1000.0);
            if (every > 0 && ++windows >= every) {
                windows = 0;
                r36s_present_serial = !r36s_present_serial;
            }
        }
        {
            char mem[400];
            mem_text(mem, sizeof mem);
            printf("%s\n", mem);
            printf("particles by half-size (under 10, 10-40, 40-100, 100-250, 250 up, px): "
                   "%u/%.0f %u/%.0f %u/%.0f %u/%.0f %u/%.0f sprites/kpx\n",
                   gAcidBucketN[0], (double)gAcidBucketK[0], gAcidBucketN[1],
                   (double)gAcidBucketK[1], gAcidBucketN[2], (double)gAcidBucketK[2],
                   gAcidBucketN[3], (double)gAcidBucketK[3], gAcidBucketN[4],
                   (double)gAcidBucketK[4]);
            r36s_pad_text(mem, sizeof mem);
            printf("%s\n", mem);
        }
        fflush(stdout);
        memset(&s_logWin, 0, sizeof s_logWin);
        s_logWin.start = now;
    }
}

// ------------------------------------------------------------ the sampler

// A preallocated ring of (pc, tid) pairs, filled by the SIGPROF handler:
// 512 K samples, the last 8 minutes of one busy core (about 3 of the game's
// busy threads). A thread writes them out every NFSHP2_PROFILE_EVERY seconds
// (20 unset, 0 only at exit), as well as at exit: PortMaster's Start + Select
// kills the game with SIGKILL, which no exit handler sees, and a session's
// profile was lost with it.
#define PROF_MAX (512u * 1024u)
static struct { uint32_t pc, tid; }* s_samples;
static volatile unsigned s_nSamples;

static void prof_handler(int sig, siginfo_t* si, void* uc)
{
    unsigned i;
    (void)sig; (void)si;
    i = __atomic_fetch_add(&s_nSamples, 1, __ATOMIC_RELAXED) % PROF_MAX;
#if defined(__arm__)
    s_samples[i].pc = (uint32_t)((ucontext_t*)uc)->uc_mcontext.arm_pc;
#elif defined(__aarch64__)
    s_samples[i].pc = (uint32_t)((ucontext_t*)uc)->uc_mcontext.pc;
#else
    s_samples[i].pc = 0;
    (void)uc;
#endif
    s_samples[i].tid = (uint32_t)syscall(SYS_gettid);
}

static void copy_file(const char* from, FILE* to)
{
    char buf[4096];
    size_t n;
    FILE* f = fopen(from, "r");
    if (f == NULL) return;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) fwrite(buf, 1, n, to);
    fclose(f);
}

// Each file goes to a .tmp beside it and is renamed over the old one, so a
// kill in the middle leaves the last whole copy.
static FILE* prof_open(const char* name, char* tmp, size_t n)
{
    snprintf(tmp, n, "%s.tmp", name);
    return fopen(tmp, "w");
}

static void prof_close(FILE* f, const char* tmp, const char* name)
{
    if (f == NULL) return;
    if (fclose(f) == 0) rename(tmp, name);
}

static pthread_mutex_t s_profMu = PTHREAD_MUTEX_INITIALIZER;

static unsigned prof_dump(void)
{
    unsigned total = s_nSamples;
    unsigned n = total < PROF_MAX ? total : PROF_MAX;
    unsigned first = total < PROF_MAX ? 0 : total % PROF_MAX;
    char tmp[64];
    FILE* f;
    DIR* d;
    struct dirent* e;

    pthread_mutex_lock(&s_profMu);
    f = prof_open("prof_samples.txt", tmp, sizeof tmp);
    if (f != NULL) {
        // Oldest first. The handler goes on writing while this reads, so the
        // oldest few may already be newer ones: a handful of samples.
        for (unsigned k = 0; k < n; k++) {
            unsigned i = (first + k) % PROF_MAX;
            fprintf(f, "%x %u\n", (unsigned)s_samples[i].pc, (unsigned)s_samples[i].tid);
        }
    }
    prof_close(f, tmp, "prof_samples.txt");
    f = prof_open("prof_maps.txt", tmp, sizeof tmp);
    if (f != NULL) copy_file("/proc/self/maps", f);
    prof_close(f, tmp, "prof_maps.txt");
    // The threads' names, for the samples' tids.
    f = prof_open("prof_threads.txt", tmp, sizeof tmp);
    d = opendir("/proc/self/task");
    if (f != NULL && d != NULL) {
        while ((e = readdir(d)) != NULL) {
            char path[64];
            if (e->d_name[0] == '.') continue;
            snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
            fprintf(f, "%s ", e->d_name);
            copy_file(path, f);
        }
    }
    if (d != NULL) closedir(d);
    prof_close(f, tmp, "prof_threads.txt");
    pthread_mutex_unlock(&s_profMu);
    return n;
}

static void prof_write(void)
{
    struct itimerval off = { { 0, 0 }, { 0, 0 } };
    unsigned n;

    setitimer(ITIMER_PROF, &off, NULL);
    n = prof_dump();
    printf("perf: %u CPU samples written to prof_samples.txt%s\n", n,
           s_nSamples > PROF_MAX ? " (the last ones: the ring wrapped)" : "");
}

// The writer: its own thread, at the lowest priority, so the game's don't
// wait on the card. It is sampled too (the timer's signal goes to the
// thread whose CPU time it is): its own tid, named "prof writer".
static void* prof_writer(void* arg)
{
    unsigned every = (unsigned)(uintptr_t)arg;

    pthread_setname_np(pthread_self(), "prof writer");
    (void)nice(19);
    for (;;) {
        sleep(every);
        unsigned n = prof_dump();
        printf("perf: %u CPU samples saved so far\n", n);
    }
    return NULL;
}

void r36s_prof_start(void)
{
    const char* e = getenv("NFSHP2_PROFILE");
    struct sigaction sa;
    struct itimerval it = { { 0, 1000 }, { 0, 1000 } };

    if (e == NULL || e[0] != '1' || s_samples != NULL) return;
    s_samples = calloc(PROF_MAX, sizeof *s_samples);
    if (s_samples == NULL) return;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = prof_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    atexit(prof_write);
    setitimer(ITIMER_PROF, &it, NULL);
    {
        const char* ev = getenv("NFSHP2_PROFILE_EVERY");
        int every = ev != NULL && ev[0] != '\0' ? atoi(ev) : 20;
        pthread_t t;

        if (every > 0
            && pthread_create(&t, NULL, prof_writer, (void*)(uintptr_t)every) == 0)
            pthread_detach(t);
        printf("perf: CPU sampler on, 1000 Hz of CPU time, saved every %d s\n",
               every > 0 ? every : 0);
    }
}
