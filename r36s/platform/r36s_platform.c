// The R36S process: what runs before main(), the 4 GB rule, and the SD
// card's paths.

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <3ds.h>

#include "fpctl.h"
#include "r36s_platform.h"

extern char* fake_heap_start;

// ------------------------------------------------------- crash report

// A fault prints where it happened (pc, lr, the faulting address and a
// backtrace) to stderr, which PortMaster's launcher keeps in log.txt. The
// addresses go through aarch64-linux-gnu-addr2line -f -e nfshp2.aarch64.
static void r36s_crash(int sig, siginfo_t* si, void* uc_)
{
    ucontext_t* uc = (ucontext_t*)uc_;
    char buf[256];
#if defined(__aarch64__)
    const unsigned long long* r = (const unsigned long long*)uc->uc_mcontext.regs;
    const int nregs = 31;
    unsigned long long pc = uc->uc_mcontext.pc, lr = r[30], sp = uc->uc_mcontext.sp;
#else
    // arm_r0 .. arm_r10, then fp, ip, sp, lr, pc, in that order.
    unsigned long long r[16];
    const unsigned long* m = &uc->uc_mcontext.arm_r0;
    const int nregs = 16;
    for (int i = 0; i < 16; i++) r[i] = m[i];
    unsigned long long pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr,
                       sp = uc->uc_mcontext.arm_sp;
#endif
    int n = snprintf(buf, sizeof buf, "R36S: signal %d at pc %llx lr %llx sp %llx, address %p\n",
                     sig, pc, lr, sp, si->si_addr);
    if (n > 0) write(2, buf, (size_t)n);
    for (int i = 0; i < nregs; i++) {
        n = snprintf(buf, sizeof buf, "  r%-2d %016llx%s", i, r[i], (i % 4) == 3 ? "\n" : "");
        if (n > 0) write(2, buf, (size_t)n);
    }
    write(2, "\nbacktrace:\n", 12);
    void* bt[48];
    int k = backtrace(bt, 48);
    backtrace_symbols_fd(bt, k, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

// Each thread gets a stack of its own for the report, so a stack overflow
// is reported too.
void r36s_thread_start(void)
{
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_size = 64 * 1024;
    ss.ss_sp = malloc(ss.ss_size);
    if (ss.ss_sp != NULL) sigaltstack(&ss, NULL);
}

// A stack from the heap has no guard page of its own, so its lowest 64 KB
// are made inaccessible while it is in use: an overflow faults there, and
// the report says so, instead of writing over the heap below it.
void r36s_stack_guard(void* stack, bool on)
{
    mprotect(stack, 0x10000, on ? PROT_NONE : PROT_READ | PROT_WRITE);
}

// main() runs on a thread whose stack is in the heap, below 4 GB, as every
// other thread's is (ctru_sys.c's threadCreate): the process's own stack is
// near the top of the address space, and the game may keep a local's
// address in a 32-bit slot. The link wraps main (Makefile).
int __real_main(int argc, char** argv);

struct r36s_main_args {
    int argc;
    char** argv;
    int rc;
};

static void* r36s_main_thread(void* p)
{
    struct r36s_main_args* a = (struct r36s_main_args*)p;
    fpctl_set((fpctl_get() & ~FPCTL_RZ) | FPCTL_RUNFAST);
    r36s_thread_start();
    a->rc = __real_main(a->argc, a->argv);
    return NULL;
}

int __wrap_main(int argc, char** argv)
{
    struct r36s_main_args a = { argc, argv, 1 };
    size_t size = 16u << 20;
    void* stack = memalign(0x10000, size);
    pthread_attr_t attr;
    pthread_t t;
    r36s_check_low(stack, "the main thread's stack");
    r36s_stack_guard(stack, true);
    r36s_prof_start();
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, size);
    if (pthread_create(&t, &attr, r36s_main_thread, &a) != 0) {
        fprintf(stderr, "R36S: cannot start the game's thread\n");
        return 1;
    }
    pthread_attr_destroy(&attr);
    pthread_join(t, NULL);
    return a.rc;
}

static void r36s_crash_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = r36s_crash;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// ------------------------------------------------------- before main()

// Every address the game keeps must fit 32 bits (r36s_platform.h). The
// executable is linked non-PIE, so its code and data sit at 0x400000; the
// game's heap is lowheap.c's region below 4 GB, and every thread's stack,
// main's too, comes from it.
__attribute__((constructor(101))) static void r36s_early_init(void)
{
    r36s_heap_bounds_refresh();
    // The 3DS's threads run in RunFast: flush-to-zero and default NaN,
    // rounding to nearest. The EE has no denormals either.
    fpctl_set((fpctl_get() & ~FPCTL_RZ) | FPCTL_RUNFAST);
    // Line-buffered logs, so a crash leaves the last lines on the card.
    setvbuf(stdout, NULL, _IOLBF, 0);
    r36s_thread_start();
    r36s_crash_install();
    // NFSHP2_CRASHTEST=1 faults here, to check the report.
    if (getenv("NFSHP2_CRASHTEST")) *(volatile int*)8 = 0;
}

void r36s_check_low(const void* p, const char* what)
{
    if ((uintptr_t)p > 0xFFFFFFFFu) {
        fprintf(stderr, "R36S: %s gave %p, above 4 GB; the game's 32-bit "
                "pointer slots cannot hold it\n", what, p);
        fflush(NULL);
        abort();
    }
}

// --------------------------------------------------------------- sdmc

static char s_root[1024];

const char* r36s_sdmc_root(void)
{
    if (s_root[0] == '\0') {
        const char* e = getenv("NFSHP2_ROOT");
        if (e != NULL && *e != '\0') {
            snprintf(s_root, sizeof s_root, "%s", e);
        } else if (getcwd(s_root, sizeof s_root) == NULL) {
            snprintf(s_root, sizeof s_root, ".");
        }
        size_t n = strlen(s_root);
        while (n > 1 && s_root[n - 1] == '/')
            s_root[--n] = '\0';
    }
    return s_root;
}

// The 3DS's card is FAT and its names match whatever their case; the
// R36S's may be ext4. A path that does not exist as written is looked up
// a component at a time, case blind, and the first match on disk taken.
static void fold_case(char* path)
{
    char built[1024];
    char* save = NULL;
    char work[1024];
    struct stat st;

    if (stat(path, &st) == 0) return;
    snprintf(work, sizeof work, "%s", path);
    built[0] = '\0';
    if (work[0] == '/') snprintf(built, sizeof built, "/");
    for (char* part = strtok_r(work, "/", &save); part != NULL;
         part = strtok_r(NULL, "/", &save)) {
        char next[1024];
        DIR* d;
        struct dirent* e;
        snprintf(next, sizeof next, "%s%s%s", built,
                 (built[0] == '\0' || built[strlen(built) - 1] == '/') ? "" : "/", part);
        if (stat(next, &st) != 0 && (d = opendir(built[0] ? built : ".")) != NULL) {
            while ((e = readdir(d)) != NULL) {
                if (strcasecmp(e->d_name, part) == 0) {
                    snprintf(next, sizeof next, "%s%s%s", built,
                             (built[0] == '\0' || built[strlen(built) - 1] == '/') ? "" : "/",
                             e->d_name);
                    break;
                }
            }
            closedir(d);
        }
        snprintf(built, sizeof built, "%s", next);
    }
    if (stat(built, &st) == 0) snprintf(path, 1024, "%s", built);
}

const char* r36s_path(const char* in, char* out, size_t n)
{
    if (in == NULL) return NULL;
    if (strncmp(in, "sdmc:/", 6) == 0) {
        snprintf(out, n, "%s/%s", r36s_sdmc_root(), in + 6);
    } else if (strncmp(in, "romfs:/", 7) == 0) {
        snprintf(out, n, "%s/%s", r36s_sdmc_root(), in + 7);
    } else {
        return in;
    }
    if (n >= 1024) fold_case(out);
    return out;
}

// The game's opens go through these (the Makefile's --wrap list), so its
// "sdmc:/NFSHP2/..." paths land under the port's folder.
FILE* __real_fopen(const char* path, const char* mode);
FILE* __real_freopen(const char* path, const char* mode, FILE* f);
int   __real_mkdir(const char* path, mode_t mode);
int   __real_open(const char* path, int flags, ...);
DIR*  __real_opendir(const char* path);
int   __real___xstat(int ver, const char* path, struct stat* st);
int   __real_stat(const char* path, struct stat* st);
int   __real_remove(const char* path);
int   __real_rename(const char* a, const char* b);
int   __real_unlink(const char* path);

FILE* __wrap_fopen(const char* path, const char* mode)
{
    char buf[1024];
    return __real_fopen(r36s_path(path, buf, sizeof buf), mode);
}

FILE* __wrap_freopen(const char* path, const char* mode, FILE* f)
{
    char buf[1024];
    return __real_freopen(r36s_path(path, buf, sizeof buf), mode, f);
}

int __wrap_mkdir(const char* path, mode_t mode)
{
    char buf[1024];
    return __real_mkdir(r36s_path(path, buf, sizeof buf), mode);
}

int __wrap_open(const char* path, int flags, ...)
{
    char buf[1024];
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return __real_open(r36s_path(path, buf, sizeof buf), flags, mode);
}

DIR* __wrap_opendir(const char* path)
{
    char buf[1024];
    return __real_opendir(r36s_path(path, buf, sizeof buf));
}

int __wrap___xstat(int ver, const char* path, struct stat* st)
{
    char buf[1024];
    return __real___xstat(ver, r36s_path(path, buf, sizeof buf), st);
}

int __wrap_stat(const char* path, struct stat* st)
{
    char buf[1024];
    return __real_stat(r36s_path(path, buf, sizeof buf), st);
}

int __wrap_remove(const char* path)
{
    char buf[1024];
    return __real_remove(r36s_path(path, buf, sizeof buf));
}

int __wrap_unlink(const char* path)
{
    char buf[1024];
    return __real_unlink(r36s_path(path, buf, sizeof buf));
}

int __wrap_rename(const char* a, const char* b)
{
    char ba[1024], bb[1024];
    return __real_rename(r36s_path(a, ba, sizeof ba), r36s_path(b, bb, sizeof bb));
}

#if !defined(__aarch64__)
// The armhf build is compiled with 64-bit file offsets (the Makefile's
// _FILE_OFFSET_BITS=64: a 32-bit stat or readdir fails with EOVERFLOW on a
// file system with 64-bit inode numbers), so glibc's headers send the
// game's calls to these names.
FILE* __real_fopen64(const char* path, const char* mode);
FILE* __real_freopen64(const char* path, const char* mode, FILE* f);
int   __real_open64(const char* path, int flags, ...);
int   __real___xstat64(int ver, const char* path, struct stat* st);

FILE* __wrap_fopen64(const char* path, const char* mode)
{
    char buf[1024];
    return __real_fopen64(r36s_path(path, buf, sizeof buf), mode);
}

FILE* __wrap_freopen64(const char* path, const char* mode, FILE* f)
{
    char buf[1024];
    return __real_freopen64(r36s_path(path, buf, sizeof buf), mode, f);
}

int __wrap_open64(const char* path, int flags, ...)
{
    char buf[1024];
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return __real_open64(r36s_path(path, buf, sizeof buf), flags, mode);
}

int __wrap___xstat64(int ver, const char* path, struct stat* st)
{
    char buf[1024];
    return __real___xstat64(ver, r36s_path(path, buf, sizeof buf), st);
}
#endif
