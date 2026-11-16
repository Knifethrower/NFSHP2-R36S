// The R36S build's own services, shared by the compat layer and the
// platform files.

#ifndef R36S_PLATFORM_H
#define R36S_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The linear heap's budget when NFSHP2_LINEAR_MB is unset: what the game's
// GPU memory measure (linearSpaceFree) counts against.
#define R36S_LINEAR_DEFAULT_MB 160

// Aborts, naming `what`, when `p` is at or above 4 GB: the game keeps
// pointers in 32-bit slots (p32.h, the image's records), so nothing it
// points at may live up there. Debug builds check every allocation the
// game makes through linearAlloc and p32.
void r36s_check_low(const void* p, const char* what);

// Pumps SDL's events into the input state; false once the window or the
// system asks the game to quit (aptMainLoop's answer).
bool r36s_pump_events(void);

// newlib's sbrk window, refreshed for threadguard's report.
void r36s_heap_bounds_refresh(void);

// The window and its GL context, made by the first of gfxInitDefault and
// C3D_Init. False when there is no display (SDL_VIDEODRIVER=dummy).
bool r36s_video_init(void);
bool r36s_video_has_gl(void);
// True for NFSHP2_GL=pbuffer, a headless test run.
bool r36s_video_headless(void);
void r36s_video_swap(void);
// The GL context made current on the calling thread, or released from it
// (glthread.c moves it to its own thread).
bool r36s_video_make_current(bool on);
bool r36s_video_thread_ok(void);
// The present thread's second context (glthread.c): split makes it, sharing
// the current one, which stays current on the calling thread with no
// surface; unsplit undoes that; present_current makes the second current
// with the window on the calling thread.
bool r36s_video_split(void);
void r36s_video_unsplit(void);
bool r36s_video_present_current(void);
const char* r36s_video_driver(void);
void r36s_video_size(int* w, int* h);
void r36s_video_resized(void);
float r36s_screen_full_w(void);
float r36s_screen_full_h(void);
void r36s_screen_pixels(int* w, int* h);
// The race's world was drawn this frame (c3d_gles.c, the present).
void r36s_screen_world(void);

// ---------------------------------------------------------------- speed
// (r36s_perf.c)

// What the GL layer did this frame, counted by c3d_gles.c and c2d_gles.c
// and read and cleared by r36s_perf_frame.
typedef struct {
    unsigned draws;         // citro3d draws handed to GL
    unsigned programs;      // glUseProgram calls (a change of program)
    unsigned textures;      // glBindTexture calls
    unsigned uniforms;      // glUniform* calls
    unsigned uploads;       // textures uploaded (their memory changed)
    unsigned uploadKB;      // what those uploads converted and sent
    unsigned targets;       // framebuffer changes
    unsigned quads2d;       // citro2d's quads
    unsigned flush2d;       // and the draws they took
    unsigned bufDraws;      // draws whose vertices came from a mesh's GPU buffer
    unsigned bufUploads;    // mesh buffers made or refreshed
    unsigned bufUploadKB;   // and their size
    unsigned held;          // draws held for grouping (c3d_gles.c)
    unsigned groups;        // and the instanced draws they became
    unsigned glBusyUs;      // the GL thread's time playing the frame, but the swap
    unsigned waitUs;        // the game thread's wait for the GL thread (the frame before)
    unsigned gameUs;        // the game thread's time making the frame
    unsigned playCpuUs;     // the GL thread's CPU time, up to the swap
    unsigned swapCpuUs;     // and in the swap: the rest of the swap is a wait
    unsigned progBuilds;    // programs made ready this frame
    unsigned progCompiled;  // of them, compiled and not from the cache
    unsigned progUs;        // the time they took
    unsigned presentUs;     // the present thread's last swap (0: no present thread)
} R36sGlCounts;
// The present thread (glthread.c): on, and whether the GL thread waits for
// each swap as without it (NFSHP2_PRESENT_AB alternates that, r36s_perf.c).
extern bool r36s_present_thread;
extern volatile int r36s_present_serial;
extern R36sGlCounts r36s_glcounts;
extern unsigned r36s_gl_textures_made;   // GL textures made since the start

// Once a frame, after the swap. `swapMs` is the swap's own time: its wait
// for the display and for the GPU to catch up.
void r36s_perf_frame(double swapMs);
// The same with the frame's counts given (the GL thread's, glthread.c).
void r36s_perf_frame_with(double swapMs, const R36sGlCounts* counts);

// The frame counter (Select + L3, R3 or R1, or NFSHP2_SHOW_FPS=1): whether it
// shows, a toggle, and its two lines of text.
bool r36s_fps_meter_on(void);
void r36s_fps_meter_toggle(void);
void r36s_fps_meter_text(char* line1, size_t n1, char* line2, size_t n2);

// The baked meshes' memory, for the GL layer's GPU buffers (c3d_buffers.c):
// a block of vertices and its indices written once, at its bake
// (geobake.c); a block the game changed (georender_refresh); and memory
// freed (linearFree), which forgets any block at that address.
void r36s_mesh_static(const void* verts, size_t vbytes, const void* indices, size_t ibytes);
void r36s_mesh_changed(const void* verts);
void r36s_mesh_forget(const void* mem);

// The speed presets (Select + R2, or NFSHP2_QUALITY=N), which change the
// picture: 0 as the game draws it; 1 the reflections one face a frame and
// the mirror every second frame; 2 also the 3D at 75% of the screen's size.
// NFSHP2_ENVMAP_FACES, NFSHP2_MIRROR_EVERY and NFSHP2_RENDER_SCALE override
// a preset's parts.
int  r36s_quality(void);
void r36s_quality_next(void);
// The top screen's drawn size, percent of its own.
float r36s_render_scale(void);

// The pad's state, for the log (ctru_hid.c).
void r36s_pad_text(char* out, size_t n);
// Memory freed that may hold a texture's pixels: its GL texture goes too.
void r36s_tex_memory_freed(const void* mem);

// A note shown at the top of the screen for two seconds, in the meter's
// font (capitals, digits, '.', '-', space): posting it, and its text while
// it shows (NULL otherwise).
void r36s_toast(const char* text);
const char* r36s_toast_text(void);

// NFSHP2_PROFILE=1: CPU samples of every thread, a thousand a second of
// CPU time, written at exit to prof_samples.txt ("<pc> <tid>") with
// prof_maps.txt (the process's mappings, to name library samples).
void r36s_prof_start(void);

#endif
