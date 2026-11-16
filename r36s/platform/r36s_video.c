// The window: one surface the size of the handheld's screen, with a GLES 3
// (else 2) context, presented at the display's vblank. Any size works: the
// presenter letterboxes the game's 4:3 picture into it. With SDL_VIDEODRIVER=dummy (a
// headless run under QEMU) there is no context and the GL layer draws
// nothing, so the game's logic can still be run and logged.
//
// NFSHP2_GL=pbuffer, for a headless run that draws (run_qemu.sh): a
// 640x480 EGL pbuffer on Mesa's surfaceless platform and its software
// renderer, with SDL kept for events and sound. EGL is opened at run
// time, so the game's binary does not depend on it.

#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r36s_platform.h"

static SDL_Window*   s_win;
static SDL_GLContext s_gl;
static bool          s_tried;
static bool          s_pbuffer;
// The pbuffer's EGL objects, to make its context current on a thread.
static void*         s_eglDpy;
static void*         s_eglSurf;
static void*         s_eglCtx;
static unsigned    (*s_eglMakeCurrent)(void*, void*, void*, void*);
static int           s_w = 640, s_h = 480;

// ------------------------------------------------------ EGL pbuffer

typedef int EGLint_;

static bool pbuffer_init(void)
{
    void* lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    void* (*getProc)(const char*);
    void* (*getPlatformDisplay)(unsigned, void*, const EGLint_*);
    void* (*getDisplay)(void*);
    unsigned (*initialize)(void*, EGLint_*, EGLint_*);
    unsigned (*bindApi)(unsigned);
    unsigned (*chooseConfig)(void*, const EGLint_*, void**, EGLint_, EGLint_*);
    void* (*createPbuffer)(void*, void*, const EGLint_*);
    void* (*createContext)(void*, void*, void*, const EGLint_*);
    unsigned (*makeCurrent)(void*, void*, void*, void*);
    // EGL_SURFACE_TYPE PBUFFER, EGL_RENDERABLE_TYPE ES3 (else ES2), RGBA8,
    // depth 24, stencil 8; a 640x480 pbuffer; an ES 3 context (instanced
    // draws, c3d_gles.c), else ES 2.
    EGLint_ cfgAttr[] = { 0x3033, 0x0001, 0x3040, 0x0040, 0x3024, 8, 0x3023, 8,
                          0x3022, 8, 0x3021, 8, 0x3025, 24, 0x3026, 8, 0x3038 };
    // NFSHP2_SCREEN=WxH: a test run on another shape of screen.
    int pbW = 640, pbH = 480;
    const char* pbSize = getenv("NFSHP2_SCREEN");
    if (pbSize == NULL || sscanf(pbSize, "%dx%d", &pbW, &pbH) != 2 || pbW < 16 || pbH < 16) {
        pbW = 640;
        pbH = 480;
    }
    const EGLint_ pbAttr[] = { 0x3057, pbW, 0x3056, pbH, 0x3038 };
    EGLint_ ctxAttr[] = { 0x3098, 3, 0x3038 };
    EGLint_ maj = 0, min = 0, n = 0;
    void *dpy, *cfg = NULL, *surf, *ctx;

    if (lib == NULL) return false;
    getProc = (void* (*)(const char*))dlsym(lib, "eglGetProcAddress");
    getDisplay = (void* (*)(void*))dlsym(lib, "eglGetDisplay");
    initialize = (unsigned (*)(void*, EGLint_*, EGLint_*))dlsym(lib, "eglInitialize");
    bindApi = (unsigned (*)(unsigned))dlsym(lib, "eglBindAPI");
    chooseConfig = (unsigned (*)(void*, const EGLint_*, void**, EGLint_, EGLint_*))
                   dlsym(lib, "eglChooseConfig");
    createPbuffer = (void* (*)(void*, void*, const EGLint_*))dlsym(lib, "eglCreatePbufferSurface");
    createContext = (void* (*)(void*, void*, void*, const EGLint_*))dlsym(lib, "eglCreateContext");
    makeCurrent = (unsigned (*)(void*, void*, void*, void*))dlsym(lib, "eglMakeCurrent");
    if (!getProc || !getDisplay || !initialize || !bindApi || !chooseConfig || !createPbuffer
        || !createContext || !makeCurrent)
        return false;
    // EGL_PLATFORM_SURFACELESS_MESA, else the default display.
    getPlatformDisplay = (void* (*)(unsigned, void*, const EGLint_*))getProc("eglGetPlatformDisplayEXT");
    dpy = getPlatformDisplay ? getPlatformDisplay(0x31DD, NULL, NULL) : NULL;
    if (dpy == NULL) dpy = getDisplay(NULL);
    if (dpy == NULL || !initialize(dpy, &maj, &min)) return false;
    bindApi(0x30A0);   // EGL_OPENGL_ES_API
    if (!chooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) {
        cfgAttr[3] = 0x0004;
        ctxAttr[1] = 2;
        if (!chooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) return false;
    }
    surf = createPbuffer(dpy, cfg, pbAttr);
    ctx = createContext(dpy, cfg, NULL, ctxAttr);
    if (ctx == NULL && ctxAttr[1] == 3) {
        ctxAttr[1] = 2;
        ctx = createContext(dpy, cfg, NULL, ctxAttr);
    }
    if (surf == NULL || ctx == NULL || !makeCurrent(dpy, surf, surf, ctx)) return false;
    s_eglDpy = dpy;
    s_eglSurf = surf;
    s_eglCtx = ctx;
    s_eglMakeCurrent = makeCurrent;
    s_w = pbW;
    s_h = pbH;
    printf("R36S: EGL %d.%d pbuffer %dx%d (headless)\n", (int)maj, (int)min, s_w, s_h);
    return true;
}

// No window on a device means a black screen with the game running behind
// it, for good: stop, and let the launcher say why. A headless run
// (SDL_VIDEODRIVER=dummy, NFSHP2_GL=pbuffer) doesn't come through here.
#define R36S_EXIT_NO_DISPLAY 75
static void no_display(void)
{
    fflush(stdout);
    exit(R36S_EXIT_NO_DISPLAY);
}

bool r36s_video_init(void)
{
    const char* mode = getenv("NFSHP2_GL");
    if (s_tried) return s_win != NULL || s_pbuffer;
    s_tried = true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        const char* want = getenv("SDL_VIDEODRIVER");
        printf("R36S: no video (%s)\n", SDL_GetError());
        if (mode == NULL && (want == NULL || strcmp(want, "dummy") != 0)) no_display();
        return false;
    }
    if (mode != NULL && strcmp(mode, "pbuffer") == 0) {
        s_pbuffer = pbuffer_init();
        if (!s_pbuffer) printf("R36S: no EGL pbuffer; drawing nothing\n");
        return s_pbuffer;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    // ES 3 for instanced draws (c3d_gles.c), else ES 2.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    // The window only shows the top target (present_screens): it needs no
    // depth or stencil, which a tile GPU would otherwise store each frame.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            s_w = dm.w;
            s_h = dm.h;
        }
    }
    {
        const char* drv = SDL_GetCurrentVideoDriver();
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
        if (drv != NULL && SDL_strcmp(drv, "dummy") == 0) {
            printf("R36S: dummy video driver, drawing nothing\n");
            return false;
        }
        // Every handheld has the one screen, whatever draws to it: KMSDRM
        // on dArkOS and the like, mali (fbdev) on Knulli, a compositor
        // (wayland, x11) on others, where the screen's mode isn't ours to
        // change. NFSHP2_WINDOWED=1: a 640x480 window, on a desktop.
        if (getenv("NFSHP2_WINDOWED") != NULL)
            s_w = 640, s_h = 480;
        else if (drv != NULL && (SDL_strcmp(drv, "KMSDRM") == 0 || SDL_strcmp(drv, "mali") == 0))
            flags |= SDL_WINDOW_FULLSCREEN;
        else
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        s_win = SDL_CreateWindow("Need for Speed: Hot Pursuit 2",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 s_w, s_h, flags);
    }
    if (s_win == NULL) {
        printf("R36S: no window (%s)\n", SDL_GetError());
        no_display();
    }
    s_gl = SDL_GL_CreateContext(s_win);
    if (s_gl == NULL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        s_gl = SDL_GL_CreateContext(s_win);
    }
    if (s_gl == NULL) {
        printf("R36S: no GLES 2 context (%s)\n", SDL_GetError());
        SDL_DestroyWindow(s_win);
        s_win = NULL;
        no_display();
    }
    {
        // The display's modes, logged: the refresh decides which frame
        // rates are even (a frame lasts whole vblanks). NFSHP2_HZ=N picks
        // the mode of the current size with that refresh, if there is one.
        SDL_DisplayMode cur, m;
        const char* hz = getenv("NFSHP2_HZ");
        int want = hz ? atoi(hz) : 0, n = SDL_GetNumDisplayModes(0);
        if (SDL_GetCurrentDisplayMode(0, &cur) == 0)
            printf("R36S: display %dx%d at %d Hz; %d modes:", cur.w, cur.h, cur.refresh_rate, n);
        for (int i = 0; i < n && i < 16; i++)
            if (SDL_GetDisplayMode(0, i, &m) == 0) printf(" %dx%d@%d", m.w, m.h, m.refresh_rate);
        printf("\n");
        if (want > 0 && want != cur.refresh_rate) {
            bool set = false;
            for (int i = 0; i < n && !set; i++) {
                if (SDL_GetDisplayMode(0, i, &m) != 0 || m.w != cur.w || m.h != cur.h
                    || m.refresh_rate != want)
                    continue;
                set = SDL_SetWindowDisplayMode(s_win, &m) == 0
                      && SDL_SetWindowFullscreen(s_win, SDL_WINDOW_FULLSCREEN) == 0;
            }
            printf("R36S: %d Hz %s\n", want, set ? "set" : "not available");
        }
    }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GL_GetDrawableSize(s_win, &s_w, &s_h);
    printf("R36S: video %dx%d on %s\n", s_w, s_h, SDL_GetCurrentVideoDriver());
    return true;
}

bool r36s_video_has_gl(void) { return s_gl != NULL || s_pbuffer; }
bool r36s_video_headless(void) { return s_pbuffer; }

void r36s_video_swap(void)
{
    if (s_win != NULL) {
        SDL_GL_SwapWindow(s_win);
    } else if (s_pbuffer) {
        // No display to wait for: pace at 60 Hz, as the swap would.
        static Uint64 next;
        Uint64 now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
        Uint64 period = f / 60;
        if (next == 0 || now > next + period) next = now;
        next += period;
        if (next > now) SDL_Delay((Uint32)((next - now) * 1000 / f));
    }
}

// ------------------------------------------------ a second context
//
// For the present thread (glthread.c): a second context sharing the
// first's textures and fences, current on the window, while the first
// draws with no surface at all (EGL_KHR_surfaceless_context) on the GL
// thread. SDL's GL context is the EGL context on its EGL backends.

static struct {
    bool  on;
    void* dpy;
    void* surf;
    void* ctxA;
    void* ctxB;
    unsigned (*makeCurrent)(void*, void*, void*, void*);
    unsigned (*destroyContext)(void*, void*);
} s_split;

bool r36s_video_split(void)
{
    void* lib;
    void* (*getCurrentDisplay)(void);
    void* (*getCurrentContext)(void);
    void* (*getCurrentSurface)(EGLint_);
    const char* (*queryString)(void*, EGLint_);
    unsigned (*queryContext)(void*, void*, EGLint_, EGLint_*);
    unsigned (*chooseConfig)(void*, const EGLint_*, void**, EGLint_, EGLint_*);
    void* (*createContext)(void*, void*, void*, const EGLint_*);
    unsigned (*makeCurrent)(void*, void*, void*, void*);
    unsigned (*destroyContext)(void*, void*);
    const char* ext;
    EGLint_ id = 0, version = 0, n = 0;
    EGLint_ cfgAttr[] = { 0x3028 /* EGL_CONFIG_ID */, 0, 0x3038 };
    EGLint_ ctxAttr[] = { 0x3098 /* EGL_CONTEXT_CLIENT_VERSION */, 0, 0x3038 };
    void *dpy, *surf, *ctxA, *ctxB, *cfg = NULL;

    if (s_split.on) return true;
    if (s_win == NULL && !s_pbuffer) return false;
    lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (lib == NULL) lib = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (lib == NULL) {
        printf("R36S: present thread off: no libEGL\n");
        return false;
    }
    getCurrentDisplay = (void* (*)(void))dlsym(lib, "eglGetCurrentDisplay");
    getCurrentContext = (void* (*)(void))dlsym(lib, "eglGetCurrentContext");
    getCurrentSurface = (void* (*)(EGLint_))dlsym(lib, "eglGetCurrentSurface");
    queryString = (const char* (*)(void*, EGLint_))dlsym(lib, "eglQueryString");
    queryContext = (unsigned (*)(void*, void*, EGLint_, EGLint_*))dlsym(lib, "eglQueryContext");
    chooseConfig = (unsigned (*)(void*, const EGLint_*, void**, EGLint_, EGLint_*))
                   dlsym(lib, "eglChooseConfig");
    createContext = (void* (*)(void*, void*, void*, const EGLint_*))dlsym(lib, "eglCreateContext");
    makeCurrent = (unsigned (*)(void*, void*, void*, void*))dlsym(lib, "eglMakeCurrent");
    destroyContext = (unsigned (*)(void*, void*))dlsym(lib, "eglDestroyContext");
    if (!getCurrentDisplay || !getCurrentContext || !getCurrentSurface || !queryString
        || !queryContext || !chooseConfig || !createContext || !makeCurrent || !destroyContext) {
        printf("R36S: present thread off: EGL functions missing\n");
        return false;
    }
    dpy = getCurrentDisplay();
    ctxA = getCurrentContext();
    surf = getCurrentSurface(0x3059 /* EGL_DRAW */);
    ext = dpy ? queryString(dpy, 0x3055 /* EGL_EXTENSIONS */) : NULL;
    if (dpy == NULL || ctxA == NULL || surf == NULL) {
        printf("R36S: present thread off: no current EGL context\n");
        return false;
    }
    if (ext == NULL || strstr(ext, "EGL_KHR_surfaceless_context") == NULL) {
        printf("R36S: present thread off: no EGL_KHR_surfaceless_context\n");
        return false;
    }
    if (!queryContext(dpy, ctxA, 0x3028, &id) || !queryContext(dpy, ctxA, 0x3098, &version)
        || version < 3) {
        printf("R36S: present thread off: needs a GLES 3 context (%d)\n", (int)version);
        return false;
    }
    cfgAttr[1] = id;
    ctxAttr[1] = version;
    if (!chooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) {
        printf("R36S: present thread off: the context's config not found\n");
        return false;
    }
    ctxB = createContext(dpy, cfg, ctxA, ctxAttr);
    if (ctxB == NULL) {
        printf("R36S: present thread off: no second context\n");
        return false;
    }
    // The first context keeps the calling thread, without the surface.
    if (!makeCurrent(dpy, NULL, NULL, ctxA)) {
        makeCurrent(dpy, surf, surf, ctxA);
        destroyContext(dpy, ctxB);
        printf("R36S: present thread off: the context won't go without a surface\n");
        return false;
    }
    s_split.dpy = dpy;
    s_split.surf = surf;
    s_split.ctxA = ctxA;
    s_split.ctxB = ctxB;
    s_split.makeCurrent = makeCurrent;
    s_split.destroyContext = destroyContext;
    s_split.on = true;
    return true;
}

// Back to one context, current with the window on the calling thread (the
// present thread didn't start).
void r36s_video_unsplit(void)
{
    if (!s_split.on) return;
    s_split.on = false;
    s_split.makeCurrent(s_split.dpy, NULL, NULL, NULL);
    s_split.destroyContext(s_split.dpy, s_split.ctxB);
    if (s_win != NULL) SDL_GL_MakeCurrent(s_win, s_gl);
    else s_split.makeCurrent(s_split.dpy, s_split.surf, s_split.surf, s_split.ctxA);
}

// The second context, made current with the window on the present thread.
// Through SDL for a window, so SDL_GL_SwapWindow knows it's current there.
bool r36s_video_present_current(void)
{
    if (!s_split.on) return false;
    if (s_win != NULL) {
        if (SDL_GL_MakeCurrent(s_win, (SDL_GLContext)s_split.ctxB) != 0) {
            printf("R36S: present thread: context not made current: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }
    return s_split.makeCurrent(s_split.dpy, s_split.surf, s_split.surf, s_split.ctxB) != 0;
}

bool r36s_video_make_current(bool on)
{
    if (s_split.on) {
        if (s_split.makeCurrent(s_split.dpy, NULL, NULL, on ? s_split.ctxA : NULL)) return true;
        printf("R36S: GL context %s without a surface\n", on ? "not made current" : "not released");
        return false;
    }
    if (s_win != NULL && s_gl != NULL) {
        if (SDL_GL_MakeCurrent(s_win, on ? s_gl : NULL) != 0) {
            printf("R36S: GL context %s: %s\n", on ? "not made current" : "not released",
                   SDL_GetError());
            return false;
        }
    } else if (s_pbuffer && s_eglMakeCurrent != NULL) {
        if (on) return s_eglMakeCurrent(s_eglDpy, s_eglSurf, s_eglSurf, s_eglCtx) != 0;
        return s_eglMakeCurrent(s_eglDpy, NULL, NULL, NULL) != 0;
    }
    return true;
}

// Whether the GL context is known to work from a thread of its own here.
// Run on the device with KMSDRM; mali is the same kind of backend. A
// compositor's backend (wayland, x11) hasn't been run that way: it gets the
// GL calls on the game's thread, slower but the way every SDL program works.
bool r36s_video_thread_ok(void)
{
    const char* drv = SDL_GetCurrentVideoDriver();
    if (s_pbuffer) return true;
    return drv != NULL && (SDL_strcmp(drv, "KMSDRM") == 0 || SDL_strcmp(drv, "mali") == 0);
}

const char* r36s_video_driver(void)
{
    const char* drv = SDL_GetCurrentVideoDriver();
    return drv != NULL ? drv : "none";
}

void r36s_video_size(int* w, int* h)
{
    if (w) *w = s_w;
    if (h) *h = s_h;
}

// The whole screen in the 640x480's units (screen.h): the 640x480 fitted in
// its middle, the rest of the screen around it. Square pixels, so nothing's
// stretched. NFSHP2_ASPECT=0: 640x480, with bars. Limited to 2.4:1 wide, past
// which the picture has bars rather than a world the game never drew.
static float s_fullW = 640.0f, s_fullH = 480.0f;

static void full_size(void)
{
    static bool done;
    const char* e;
    if (done) return;
    r36s_video_init();
    done = true;
    e = getenv("NFSHP2_ASPECT");
    if (r36s_video_has_gl() && s_w > 0 && s_h > 0 && !(e != NULL && e[0] == '0')) {
        float a = (float)s_w / (float)s_h;
        if (a > 2.4f) a = 2.4f;
        if (a >= 640.0f / 480.0f) {
            s_fullW = 480.0f * a;
        } else {
            s_fullH = 640.0f / a;
        }
    }
    printf("R36S: screen %dx%d, picture %.0fx%.0f in the 640x480's units\n", s_w, s_h, s_fullW,
           s_fullH);
}

float r36s_screen_full_w(void)
{
    full_size();
    return s_fullW;
}

float r36s_screen_full_h(void)
{
    full_size();
    return s_fullH;
}

// The top target's pixels: the picture at the screen's own resolution.
void r36s_screen_pixels(int* w, int* h)
{
    float k;
    full_size();
    k = (float)s_w / s_fullW;
    if ((float)s_h / s_fullH < k) k = (float)s_h / s_fullH;
    *w = (int)(s_fullW * k + 0.5f);
    *h = (int)(s_fullH * k + 0.5f);
    if (*w > s_w) *w = s_w;
    if (*h > s_h) *h = s_h;
}

// A compositor can give the window its size after it's made.
void r36s_video_resized(void)
{
    if (s_win != NULL) {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(s_win, &w, &h);
        if (w > 0 && h > 0 && (w != s_w || h != s_h)) {
            s_w = w;
            s_h = h;
            printf("R36S: video now %dx%d\n", s_w, s_h);
        }
    }
}
