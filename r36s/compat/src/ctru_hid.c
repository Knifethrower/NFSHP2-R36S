// libctru's HID on SDL's game controller (and a keyboard, for a desktop
// run), and the event pump aptMainLoop runs.
//
// Buttons map by position, not by label: the 3DS's A is the right face
// button, B the bottom, X the top, Y the left, and SDL names its buttons
// by an Xbox pad's positions (A bottom, B right, X left, Y top). The
// R36S's L1/R1 are L/R, L2/R2 are ZL/ZR, the left stick the Circle Pad
// and the right stick the C-Stick, as the README's controls describe.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <3ds.h>

#include "r36s_platform.h"

// The 3DS's Circle Pad reads about +-156 at full tilt, up positive; the
// C-Stick about +-146. libctru sets the KEY_CPAD_* and KEY_CSTICK_* bits
// past about a quarter of the range.
#define CPAD_RANGE   156
#define CSTICK_RANGE 146
#define DIR_THRESHOLD 0.25f
#define TRIGGER_ON   (32767 / 3)
#define STICK_DEAD   3500

static SDL_GameController* s_pad;
static bool s_inited;
static bool s_quit;
static u32 s_held, s_down, s_up, s_prevHeld;
static circlePosition s_cpad, s_cstick;

// ------------------------------------------------------ scripted input

// NFSHP2_INPUT, for a run with nobody at the pad (r36s/run_qemu.sh): a
// comma-separated list of presses by input frame (hidScanInput's calls),
//   FRAME:KEY            press KEY for 4 frames at FRAME
//   FRAME:KEY*N/P        N such presses, one every P frames
// with KEY one of A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT.
// Each press is logged.
//
// NFSHP2_INPUT_AT names a screen ("New Main Menu"), and NFSHP2_INPUT_THEN's
// frames count from the first time the game enters it, where NFSHP2_INPUT's
// presses stop (so 'A*40/90' gets there however long it takes): a menu that opens
// after a movie timed by the clock comes at another frame each run under
// emulation. There two more KEYs: DRAW starts drawing in a run with
// NFSHP2_GL_DRAWFROM (glthread.c), SHOT saves the next frame (c3d_gles.c's
// shot_NNNNN.ppm).
#define SCRIPT_MAX 64
#define SCRIPT_HOLD 4
#define SCRIPT_DRAW 0x40000000u   // markers, never put in the keys held
#define SCRIPT_SHOT 0x20000000u
static struct { unsigned frame, count, period; u32 key; const char* name; bool then; } s_script[SCRIPT_MAX];
static int s_scriptN;
static unsigned s_frame;
static unsigned s_atFrame;        // the screen's first frame, + 1; 0 not yet

void rgl_draw_now(void);           // glthread.c
void r36s_shot_next(void);         // c3d_gles.c

void r36s_input_screen(const char* screen)
{
    const char* at = getenv("NFSHP2_INPUT_AT");
    if (s_atFrame == 0 && at != NULL && screen != NULL && strcmp(at, screen) == 0) {
        s_atFrame = s_frame + 1;
        printf("input: frame %u, %s entered; NFSHP2_INPUT_THEN counts from here\n", s_frame, screen);
    }
}

static void script_parse_one(const char* p, bool then)
{
    static const struct { const char* name; u32 key; } names[] = {
        { "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y },
        { "L", KEY_L }, { "R", KEY_R }, { "ZL", KEY_ZL }, { "ZR", KEY_ZR },
        { "L3", KEY_L3 }, { "R3", KEY_R3 },
        { "START", KEY_START }, { "SELECT", KEY_SELECT },
        { "UP", KEY_DUP }, { "DOWN", KEY_DDOWN }, { "LEFT", KEY_DLEFT },
        { "RIGHT", KEY_DRIGHT }, { "DRAW", SCRIPT_DRAW }, { "SHOT", SCRIPT_SHOT },
    };
    while (p != NULL && *p != '\0' && s_scriptN < SCRIPT_MAX) {
        char name[16];
        unsigned frame = 0, count = 1, period = 1;
        int used = 0;
        if (sscanf(p, "%u:%15[A-Z]%n", &frame, name, &used) < 2) break;
        p += used;
        if (*p == '*' && sscanf(p, "*%u/%u%n", &count, &period, &used) == 2) p += used;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (strcmp(name, names[i].name) == 0) {
                s_script[s_scriptN].frame = frame;
                s_script[s_scriptN].count = count ? count : 1;
                s_script[s_scriptN].period = period ? period : 1;
                s_script[s_scriptN].key = names[i].key;
                s_script[s_scriptN].name = names[i].name;
                s_script[s_scriptN].then = then;
                s_scriptN++;
            }
        while (*p != '\0' && *p != ',') p++;
        if (*p == ',') p++;
    }
}

static void script_parse(void)
{
    script_parse_one(getenv("NFSHP2_INPUT"), false);
    script_parse_one(getenv("NFSHP2_INPUT_THEN"), true);
}

static u32 script_keys(void)
{
    u32 k = 0;
    for (int i = 0; i < s_scriptN; i++) {
        unsigned f = s_frame, start = s_script[i].frame;
        if (s_script[i].then) {
            if (s_atFrame == 0) continue;
            f = s_frame - (s_atFrame - 1);
        } else if (s_atFrame != 0) {
            continue;   // NFSHP2_INPUT ends where NFSHP2_INPUT_THEN starts
        }
        if (f < start) continue;
        unsigned n = (f - start) / s_script[i].period;
        unsigned in = (f - start) % s_script[i].period;
        if (n < s_script[i].count && in < SCRIPT_HOLD) {
            u32 key = s_script[i].key;
            if (in == 0) {
                printf("input: frame %u, %s%s\n", s_frame, s_script[i].name,
                       s_script[i].then ? " (then)" : "");
                if (key == SCRIPT_DRAW) rgl_draw_now();
                if (key == SCRIPT_SHOT) r36s_shot_next();
            }
            if (key != SCRIPT_DRAW && key != SCRIPT_SHOT) k |= key;
        }
    }
    return k;
}

// Select+Start held together for a second quits, as PortMaster's gptokeyb
// does when PortMaster is there: a fresh dArkOS doesn't have it. The quit
// is aptMainLoop's answer, so the game leaves its main loop and shuts down
// as the 3DS's does when closed from HOME. If it hasn't exited 5 seconds
// later (stuck somewhere that doesn't pump events), the process ends.
#define QUIT_HOLD_MS  1000
#define QUIT_GRACE_MS 5000

static Uint32 quit_backstop(Uint32 interval, void* param)
{
    (void)interval; (void)param;
    fprintf(stderr, "quit: the game didn't exit in %d ms, ending it\n", QUIT_GRACE_MS);
    fflush(NULL);
    _Exit(0);
    return 0;
}

static void quit_combo(void)
{
    static Uint32 since;
    bool both = s_pad != NULL
        && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_BACK)
        && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_START);
    if (!both || s_quit) {
        since = 0;
        return;
    }
    if (since == 0) since = SDL_GetTicks() | 1;
    if (SDL_GetTicks() - since >= QUIT_HOLD_MS) {
        printf("quit: Select+Start held\n");
        s_quit = true;
        SDL_AddTimer(QUIT_GRACE_MS, quit_backstop, NULL);
    }
}

// Select + L3 or R3 shows or hides the frame counter (r36s_perf.c), as
// Select + R3 does in the other R36S ports; Select + L1 too, for devices
// with no sticks. The 3DS has no stick buttons, so the game never sees
// them; the combo's L1 is kept from the game until it's let go (no
// look back, no roadblock). Select reaches it as it would.
static bool s_eatL;
static bool s_eatR;
static bool s_eatL3, s_eatR3;
// The speed preset is Options' Display Options, Graphics (displayoptions.c);
// Select + R2 changed it until 2026-11-20.

static void meter_combo(void)
{
    static bool wasL3, wasR3, wasL1;
    bool sel = s_pad != NULL && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_BACK);
    bool l3 = s_pad != NULL
        && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    bool r3 = s_pad != NULL
        && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    bool l1 = s_pad != NULL
        && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    bool toggle = false;
    if (sel && l3 && !wasL3) s_eatL3 = toggle = true;
    if (sel && r3 && !wasR3) s_eatR3 = toggle = true;
    if (sel && l1 && !wasL1) {
        toggle = true;
        s_eatL = true;
    }
    if (!l1) s_eatL = false;
    if (toggle) r36s_fps_meter_toggle();
    if (!l3) s_eatL3 = false;
    if (!r3) s_eatR3 = false;
    wasL3 = l3;
    wasR3 = r3;
    wasL1 = l1;
}

// Select + R1 swaps the D-pad and the left stick, for a device with no
// stick or a player who'd rather steer with the other: swapped, the D-pad
// is the Circle Pad at full tilt and the stick past half tilt is the
// D-pad. NFSHP2_DPAD_STICK=1 starts swapped. That R1 is the combo's, so
// the game doesn't see it (no handbrake, no helicopter) until it's let go;
// Select reaches the game as it would. Scripted input and a desktop's
// keyboard are never swapped.
#define SWAP_ON  0.5f   // stick to D-pad: pressed past half tilt,
#define SWAP_OFF 0.3f   // let go under 30%
static int s_swap = -1;       // -1: not read from NFSHP2_DPAD_STICK yet

static bool swapped(void)
{
    if (s_swap < 0) {
        const char* e = getenv("NFSHP2_DPAD_STICK");
        s_swap = e != NULL && e[0] == '1';
        if (s_swap) printf("input: D-pad and left stick swapped (NFSHP2_DPAD_STICK=1)\n");
    }
    return s_swap == 1;
}

static void swap_combo(void)
{
    static bool was;
    bool sel = s_pad != NULL && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_BACK);
    bool r1 = s_pad != NULL
        && SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (sel && r1 && !was) {
        s_swap = !swapped();
        s_eatR = true;
        printf("input: D-pad and left stick %s\n", s_swap ? "swapped" : "as normal");
        r36s_toast(s_swap ? "D-PAD STEERS" : "STICK STEERS");
    }
    if (!r1) s_eatR = false;
    was = r1;
}

// ------------------------------------------------ the sticks' rest positions
//
// A stick needn't read 0 at rest: on the device after a dArkOS reinstall,
// the car steered right with nobody touching it. A rest past the dead zone
// is a steady input to a game that takes the smallest tilt. So the sticks'
// rest is measured when the pad opens, over CAL_POLLS reads that barely
// move and aren't far from centre, and each stick reads from there, with
// full tilt still full. Until the first try is done they read 0; if a stick
// was being moved, it reads as it is and the measuring goes on.
// NFSHP2_STICK_CAL=0: no measuring, the sticks read as SDL gives them.
#define CAL_POLLS  12
#define CAL_SPREAD 3000     // how far the reads may wander
#define CAL_MAX    20000    // a rest further out than this is a held stick

static const SDL_GameControllerAxis kStickAxis[4] = {
    SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
};
enum { STICK_LX, STICK_LY, STICK_RX, STICK_RY };
static int  s_raw[4], s_centre[4];
static bool s_calDone, s_calTried;
static int  s_calN, s_calMin[4], s_calMax[4];
static long s_calSum[4];

static void pad_describe(void)
{
    char guid[33] = "";
    SDL_Joystick* j = SDL_GameControllerGetJoystick(s_pad);
    char* map = SDL_GameControllerMapping(s_pad);
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), guid, sizeof guid);
    printf("input: pad \"%s\", GUID %s, %d axes, %d buttons, %d hats; mapping %s\n",
           SDL_JoystickName(j), guid, SDL_JoystickNumAxes(j), SDL_JoystickNumButtons(j),
           SDL_JoystickNumHats(j), map != NULL ? map : "none");
    SDL_free(map);
}

static void sticks_read(void)
{
    static int mode = -1;
    bool ok = true;
    if (s_pad == NULL) return;
    for (int i = 0; i < 4; i++) s_raw[i] = SDL_GameControllerGetAxis(s_pad, kStickAxis[i]);
    if (s_calDone) return;
    if (mode < 0) {
        const char* e = getenv("NFSHP2_STICK_CAL");
        mode = e == NULL || e[0] != '0';
        pad_describe();
    }
    if (!mode) {
        s_calDone = true;
        printf("input: the sticks' rest isn't measured (NFSHP2_STICK_CAL=0)\n");
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (s_calN == 0 || s_raw[i] < s_calMin[i]) s_calMin[i] = s_raw[i];
        if (s_calN == 0 || s_raw[i] > s_calMax[i]) s_calMax[i] = s_raw[i];
        s_calSum[i] = (s_calN == 0 ? 0 : s_calSum[i]) + s_raw[i];
    }
    if (++s_calN < CAL_POLLS) return;
    for (int i = 0; i < 4; i++) {
        long mean = s_calSum[i] / CAL_POLLS;
        if (s_calMax[i] - s_calMin[i] > CAL_SPREAD || mean > CAL_MAX || mean < -CAL_MAX) ok = false;
    }
    if (ok) {
        for (int i = 0; i < 4; i++) s_centre[i] = (int)(s_calSum[i] / CAL_POLLS);
        s_calDone = true;
        printf("input: the sticks rest at left %d,%d right %d,%d\n", s_centre[STICK_LX],
               s_centre[STICK_LY], s_centre[STICK_RX], s_centre[STICK_RY]);
    } else if (!s_calTried) {
        printf("input: a stick was moving; measuring its rest again\n");
    }
    s_calTried = true;
    s_calN = 0;
}

// A stick's value from its rest, -32767 to 32767.
static int stick_value(int i)
{
    int c = s_centre[i], v;
    if (s_pad == NULL || (!s_calDone && !s_calTried)) return 0;
    v = s_raw[i] - c;
    v = v > 0 ? (int)((long)v * 32767 / (32767 - c)) : (int)((long)v * 32767 / (32767 + c));
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}

void r36s_pad_text(char* out, size_t n)
{
    snprintf(out, n, "pad: sticks read left %d,%d right %d,%d, rest left %d,%d right %d,%d;"
             " Circle Pad %d,%d; keys %08x", s_raw[STICK_LX], s_raw[STICK_LY], s_raw[STICK_RX],
             s_raw[STICK_RY], s_centre[STICK_LX], s_centre[STICK_LY], s_centre[STICK_RX],
             s_centre[STICK_RY], s_cpad.dx, s_cpad.dy, (unsigned)s_held);
}

// The left stick as D-pad keys, with a gap between press and release so a
// stick held near the line doesn't press and release every frame.
static u32 stick_dpad(void)
{
    static u32 held;
    int on = (int)(32767 * SWAP_ON), off = (int)(32767 * SWAP_OFF);
    int x = stick_value(STICK_LX);
    int y = stick_value(STICK_LY);   // + down
    const struct { u32 key; int v; } dirs[] = {
        { KEY_DRIGHT, x }, { KEY_DLEFT, -x }, { KEY_DDOWN, y }, { KEY_DUP, -y },
    };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        if (dirs[i].v > on) held |= dirs[i].key;
        else if (dirs[i].v < off) held &= ~dirs[i].key;
    }
    return held;
}

// The game's face buttons are by position (hidScanInput's map): SDL's A the
// bottom one, B the right, X the left, Y the top, as SDL means them. muOS's
// own pad comes with a mapping by the letters printed on the buttons, and
// most of its handhelds (Anbernic's) print them Nintendo's way, A on the
// right and B at the bottom: each face button then did another's job, and
// the controller page named the wrong ones. That mapping's four face buttons
// are put back by position; the rest of it stays. The launcher's lines name
// the pads it knows by their GUID; this covers the muOS devices it doesn't.
// NFSHP2_FACE_FIX=0 leaves the mapping as muOS gives it.
static void face_fix(void)
{
    static const char kLabel[] = ",a:b2,b:b3,x:b4,y:b5,";
    static const char kPlace[] = ",a:b3,b:b2,x:b5,y:b4,";
    const char* off = getenv("NFSHP2_FACE_FIX");
    const char* name = SDL_GameControllerName(s_pad);
    char* m;
    char* at;

    if ((off != NULL && off[0] == '0') || name == NULL || strcmp(name, "muOS-Keys") != 0)
        return;
    m = SDL_GameControllerMapping(s_pad);
    if (m == NULL) return;
    at = strstr(m, kLabel);
    if (at != NULL) {
        memcpy(at, kPlace, sizeof kPlace - 1);
        if (SDL_GameControllerAddMapping(m) >= 0)
            printf("input: muOS's pad maps its face buttons by their letters; "
                   "by position here (NFSHP2_FACE_FIX=0 to keep them)\n");
    }
    SDL_free(m);
}

// Options' Controller, Face buttons: Standard, or Swap A/B & X/Y, for a pad
// whose buttons still come out crossed (some muOS devices). Swapped, the
// bottom and right buttons trade jobs and so do the left and top ones, on
// top of whatever the pad's mapping already is: every control the game
// reads moves with them, menus and the controller page included, while the
// letters on screen stay as they are. The choice is kept in the save folder.
// NFSHP2_SWAP_AB=1 and NFSHP2_SWAP_XY=1 swap each pair whatever the menu
// says; 0 or unset leave it to the menu. Scripted input and a desktop's
// keyboard are never swapped.
#define FACE_SAVE_DIR  "sdmc:/NFSHP2/SAVES"
#define FACE_SAVE_FILE FACE_SAVE_DIR "/face.txt"

static int s_faceMenu = -1;   // -1 not read yet; 0 Standard, 1 Swap A/B & X/Y

int r36s_face_swap(void)
{
    if (s_faceMenu < 0) {
        char  path[512];
        FILE* f = fopen(r36s_path(FACE_SAVE_FILE, path, sizeof path), "r");
        int   v;

        s_faceMenu = 0;
        if (f != NULL) {
            if (fscanf(f, "%d", &v) == 1 && (v == 0 || v == 1)) s_faceMenu = v;
            fclose(f);
        }
        if (s_faceMenu) printf("input: face buttons swapped (Options, Controller)\n");
    }
    return s_faceMenu;
}

void r36s_face_swap_set(int on)
{
    char  path[512];
    FILE* f;

    s_faceMenu = on != 0;
    printf("input: face buttons %s\n", s_faceMenu ? "swapped A/B and X/Y" : "standard");
    mkdir(r36s_path(FACE_SAVE_DIR, path, sizeof path), 0755);
    f = fopen(r36s_path(FACE_SAVE_FILE, path, sizeof path), "w");
    if (f != NULL) {
        fprintf(f, "%d\n", s_faceMenu);
        fclose(f);
    } else {
        printf("input: the face buttons choice couldn't be saved to %s\n", path);
    }
}

static int env_on(const char* name)
{
    const char* e = getenv(name);
    return e != NULL && e[0] == '1';
}

static SDL_GameControllerButton face_button(SDL_GameControllerButton b)
{
    static int envAB = -1, envXY;
    int        menu = r36s_face_swap();
    int        ab, xy;

    if (envAB < 0) {
        envAB = env_on("NFSHP2_SWAP_AB");
        envXY = env_on("NFSHP2_SWAP_XY");
        if (envAB || envXY)
            printf("input: face buttons swapped by%s%s\n", envAB ? " NFSHP2_SWAP_AB=1" : "",
                   envXY ? " NFSHP2_SWAP_XY=1" : "");
    }
    ab = menu || envAB;
    xy = menu || envXY;
    if (ab) {
        if (b == SDL_CONTROLLER_BUTTON_A) return SDL_CONTROLLER_BUTTON_B;
        if (b == SDL_CONTROLLER_BUTTON_B) return SDL_CONTROLLER_BUTTON_A;
    }
    if (xy) {
        if (b == SDL_CONTROLLER_BUTTON_X) return SDL_CONTROLLER_BUTTON_Y;
        if (b == SDL_CONTROLLER_BUTTON_Y) return SDL_CONTROLLER_BUTTON_X;
    }
    return b;
}

static void open_pad(void)
{
    for (int i = 0; i < SDL_NumJoysticks() && s_pad == NULL; i++) {
        if (SDL_IsGameController(i))
            s_pad = SDL_GameControllerOpen(i);
    }
    if (s_pad != NULL) face_fix();
}

static void hid_init(void)
{
    if (s_inited) return;
    s_inited = true;
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0)
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    {
        // The firmware hands the device's mapping over in this variable
        // (PortMaster, Batocera and Knulli), and the launcher adds lines
        // of its own. Each line is one pad's: given whole, the later lines
        // would be read as more of the first pad's buttons.
        const char* map = getenv("SDL_GAMECONTROLLERCONFIG");
        while (map != NULL && *map != '\0') {
            const char* end = strchr(map, '\n');
            size_t n = end != NULL ? (size_t)(end - map) : strlen(map);
            if (n > 0 && map[0] != '#') {
                char* line = (char*)malloc(n + 1);
                if (line != NULL) {
                    memcpy(line, map, n);
                    line[n] = '\0';
                    SDL_GameControllerAddMapping(line);
                    free(line);
                }
            }
            map = end != NULL ? end + 1 : NULL;
        }
    }
    open_pad();
    if (s_pad == NULL) {
        // No pad SDL can name the buttons of. The log says which, so that
        // its mapping can be added.
        int n = SDL_NumJoysticks();
        printf("input: no mapped pad among %d joysticks\n", n);
        for (int i = 0; i < n; i++) {
            char guid[33] = "";
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
            printf("input: joystick %d \"%s\", GUID %s, no mapping\n", i,
                   SDL_JoystickNameForIndex(i), guid);
        }
    }
}

bool r36s_pump_events(void)
{
    SDL_Event e;
    hid_init();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            s_quit = true;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) r36s_video_resized();
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (s_pad == NULL) open_pad();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (s_pad != NULL
                && e.cdevice.which == SDL_JoystickInstanceID(
                       SDL_GameControllerGetJoystick(s_pad))) {
                SDL_GameControllerClose(s_pad);
                s_pad = NULL;
                s_calDone = s_calTried = false;   // another pad, another rest
                s_calN = 0;
                memset(s_centre, 0, sizeof s_centre);
                open_pad();
            }
            break;
        default:
            break;
        }
    }
    quit_combo();
    meter_combo();
    swap_combo();
    sticks_read();
    return !s_quit;
}

// From the dead zone's edge outward, so the first tilt past it is a small
// one and full tilt is still full.
static s16 stick(int which, int range, bool flip)
{
    int v = stick_value(which);
    if (v > -STICK_DEAD && v < STICK_DEAD) v = 0;
    else v = (int)((long)(v > 0 ? v - STICK_DEAD : v + STICK_DEAD) * 32767 / (32767 - STICK_DEAD));
    if (flip) v = -v;
    v = v * range / 32767;
    if (v > range) v = range;
    if (v < -range) v = -range;
    return (s16)v;
}

static u32 dir_bits(const circlePosition* p, int range, u32 r, u32 l, u32 u, u32 d)
{
    u32 k = 0;
    int t = (int)(range * DIR_THRESHOLD);
    if (p->dx > t) k |= r;
    if (p->dx < -t) k |= l;
    if (p->dy > t) k |= u;
    if (p->dy < -t) k |= d;
    return k;
}

void hidScanInput(void)
{
    const Uint8* kb;
    u32 k = 0, pad = 0;
    bool swap;

    r36s_pump_events();
    if (s_frame == 0) script_parse();
    k |= script_keys();
    s_frame++;
    swap = swapped();
    if (s_pad != NULL) {
        static const struct { SDL_GameControllerButton b; u32 key; } map[] = {
            { SDL_CONTROLLER_BUTTON_A, KEY_A }, // Cross, south
            { SDL_CONTROLLER_BUTTON_Y, KEY_B }, // Triangle, north
            { SDL_CONTROLLER_BUTTON_X, KEY_X }, // Square, west
            { SDL_CONTROLLER_BUTTON_B, KEY_Y }, // Circle, east
            { SDL_CONTROLLER_BUTTON_LEFTSTICK, KEY_L3 },
            { SDL_CONTROLLER_BUTTON_RIGHTSTICK, KEY_R3 },
            { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, KEY_L },
            { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, KEY_R },
            { SDL_CONTROLLER_BUTTON_START, KEY_START },
            { SDL_CONTROLLER_BUTTON_BACK, KEY_SELECT },
            { SDL_CONTROLLER_BUTTON_DPAD_UP, KEY_DUP },
            { SDL_CONTROLLER_BUTTON_DPAD_DOWN, KEY_DDOWN },
            { SDL_CONTROLLER_BUTTON_DPAD_LEFT, KEY_DLEFT },
            { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, KEY_DRIGHT },
        };
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
            if (SDL_GameControllerGetButton(s_pad, face_button(map[i].b))) pad |= map[i].key;
        if (SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_ON)
            pad |= KEY_ZL;
        if (SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_ON)
            pad |= KEY_ZR;
        if (s_eatL) pad &= ~KEY_L;
        if (s_eatR) pad &= ~KEY_R;
        if (s_eatL3) pad &= ~KEY_L3;
        if (s_eatR3) pad &= ~KEY_R3;
    }
    if (swap && s_pad != NULL) {
        const u32 dpad = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;
        u32 d = pad & dpad;
        pad = (pad & ~dpad) | stick_dpad();
        s_cpad.dx = (d & KEY_DRIGHT) ? CPAD_RANGE : (d & KEY_DLEFT) ? -CPAD_RANGE : 0;
        s_cpad.dy = (d & KEY_DUP) ? CPAD_RANGE : (d & KEY_DDOWN) ? -CPAD_RANGE : 0;
    } else {
        s_cpad.dx = stick(STICK_LX, CPAD_RANGE, false);
        s_cpad.dy = stick(STICK_LY, CPAD_RANGE, true);
    }
    k |= pad;
    s_cstick.dx = stick(STICK_RX, CSTICK_RANGE, false);
    s_cstick.dy = stick(STICK_RY, CSTICK_RANGE, true);

    // A keyboard, for a run on a desktop: arrows, X/Z for A/B, S/A for
    // X/Y, Q/W for L/R, 1/2 for ZL/ZR, Enter START, Backspace SELECT.
    kb = SDL_GetKeyboardState(NULL);
    if (kb != NULL) {
        static const struct { SDL_Scancode s; u32 key; } keys[] = {
            { SDL_SCANCODE_X, KEY_A }, { SDL_SCANCODE_Z, KEY_B },
            { SDL_SCANCODE_S, KEY_X }, { SDL_SCANCODE_A, KEY_Y },
            { SDL_SCANCODE_Q, KEY_L }, { SDL_SCANCODE_W, KEY_R },
            { SDL_SCANCODE_1, KEY_ZL }, { SDL_SCANCODE_2, KEY_ZR },
            { SDL_SCANCODE_3, KEY_L3 }, { SDL_SCANCODE_4, KEY_R3 },
            { SDL_SCANCODE_RETURN, KEY_START },
            { SDL_SCANCODE_BACKSPACE, KEY_SELECT },
            { SDL_SCANCODE_UP, KEY_DUP }, { SDL_SCANCODE_DOWN, KEY_DDOWN },
            { SDL_SCANCODE_LEFT, KEY_DLEFT }, { SDL_SCANCODE_RIGHT, KEY_DRIGHT },
        };
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
            if (kb[keys[i].s]) k |= keys[i].key;
    }

    k |= dir_bits(&s_cpad, CPAD_RANGE, KEY_CPAD_RIGHT, KEY_CPAD_LEFT,
                  KEY_CPAD_UP, KEY_CPAD_DOWN);
    k |= dir_bits(&s_cstick, CSTICK_RANGE, KEY_CSTICK_RIGHT, KEY_CSTICK_LEFT,
                  KEY_CSTICK_UP, KEY_CSTICK_DOWN);

    s_prevHeld = s_held;
    s_held = k;
    s_down = s_held & ~s_prevHeld;
    s_up = ~s_held & s_prevHeld;
}

u32 hidKeysHeld(void) { return s_held; }
u32 hidKeysDown(void) { return s_down; }
u32 hidKeysUp(void) { return s_up; }
void hidCircleRead(circlePosition* pos) { if (pos) *pos = s_cpad; }
void hidCstickRead(circlePosition* pos) { if (pos) *pos = s_cstick; }
void hidTouchRead(touchPosition* pos) { if (pos) memset(pos, 0, sizeof *pos); }
