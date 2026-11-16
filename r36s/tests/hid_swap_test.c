// The pad layer's D-pad and left stick swap (compat/src/ctru_hid.c), on
// SDL's virtual game controller: off, the D-pad is the D-pad and the stick
// the Circle Pad; Select + R1 swaps them, keeps that R1 from the game and
// posts a note; again swaps them back. Built and run by
// r36s/tests/hid_swap_test.sh.

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>
#include "helpglyph.h"
#include "joyhid.h"
#include "joypad.h"

JoypadReadFn gJoypadRead;

// r36s_perf.c's, which the test doesn't link.
static int s_toggles;
static char s_toast[32];
void r36s_fps_meter_toggle(void) { s_toggles++; }
void r36s_toast(const char* text) { snprintf(s_toast, sizeof s_toast, "%s", text); }
static int s_quality;
void r36s_quality_next(void) { s_quality++; }
void r36s_video_resized(void) {}

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); s_fail++; } } while (0)

static SDL_Joystick* s_joy;

static void press(SDL_GameControllerButton b, int on) { SDL_JoystickSetVirtualButton(s_joy, b, on); }
static void axis(SDL_GameControllerAxis a, int v) { SDL_JoystickSetVirtualAxis(s_joy, a, (Sint16)v); }
static void frame(void) { hidScanInput(); }

static void check_ps2_buttons(void)
{
    static const struct {
        SDL_GameControllerButton button;
        u16 ds2;
        unsigned char glyph;
    } face[] = {
        { SDL_CONTROLLER_BUTTON_A, 1u << 14, 0x86 }, // Cross
        { SDL_CONTROLLER_BUTTON_Y, 1u << 12, 0x87 }, // Triangle
        { SDL_CONTROLLER_BUTTON_X, 1u << 15, 0x88 }, // Square
        { SDL_CONTROLLER_BUTTON_B, 1u << 13, 0x89 }, // Circle
    };
    uint8_t buf[32];
    JoyHidState state = {0};

    for (size_t i = 0; i < sizeof face / sizeof face[0]; i++) {
        press(face[i].button, 1);
        frame();
        state.keys = hidKeysHeld();
        for (int layout = JOYHID_LAYOUT_MENU; layout <= JOYHID_LAYOUT_FE; layout++) {
            state.race = layout;
            joyhid_build(&state, buf);
            CHECK((uint16_t)~(buf[2] | buf[3] << 8) == face[i].ds2);
            CHECK(helpglyph_map(face[i].glyph, layout) == face[i].glyph);
        }
        CHECK(helpglyph_map(face[i].glyph, HELPGLYPH_LAYOUT_SCANNERS) == face[i].glyph);
        press(face[i].button, 0);
        frame();
    }

    state.race = JOYHID_LAYOUT_COP;
    state.keys = KEY_R3;
    joyhid_build(&state, buf);
    CHECK((uint16_t)~(buf[2] | buf[3] << 8) == (1u << 2));
    state.keys = KEY_B | KEY_ZR;
    joyhid_build(&state, buf);
    CHECK((uint16_t)~(buf[2] | buf[3] << 8) == (JOYHID_DS2_TRIANGLE | JOYHID_DS2_R2));
    char call[] = "\x87 + \x8d";
    CHECK(helpglyph_rewrite(call, JOYHID_LAYOUT_COP) == 5);
}

int main(int argc, char** argv)
{
    const u32 dpad = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;
    circlePosition cp;
    int idx;

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                    SDL_CONTROLLER_BUTTON_MAX, 0);
    if (idx < 0 || !SDL_IsGameController(idx)) {
        printf("virtual pad: %s\n", SDL_GetError());
        return 2;
    }
    // HID_TEST_MAPS: the firmware's mapping for this pad, then another
    // pad's line, as the launcher leaves the variable. Ours must hold.
    if (getenv("HID_TEST_MAPS") != NULL) {
        char guid[33], value[512];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(idx), guid, sizeof guid);
        snprintf(value, sizeof value,
                 "%s,Test pad,a:b1,b:b0,back:b4,start:b6,leftx:a0,lefty:a1,platform:Linux,\n"
                 "190000004b4800000011000000010000,GO-Super Gamepad,a:b0,b:b1,back:b12,start:b13,"
                 "platform:Linux,\n", guid);
        setenv("SDL_GAMECONTROLLERCONFIG", value, 1);
        frame();
        s_joy = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(idx));
        if (s_joy == NULL) { printf("pad not opened\n"); return 2; }
        SDL_JoystickSetVirtualButton(s_joy, 1, 1);      // this mapping's Cross
        frame();
        CHECK(hidKeysHeld() & KEY_A);
        CHECK(!(hidKeysHeld() & KEY_Y));
        SDL_JoystickSetVirtualButton(s_joy, 1, 0);
        SDL_JoystickSetVirtualButton(s_joy, 0, 1);      // this mapping's Circle
        frame();
        CHECK(hidKeysHeld() & KEY_Y);
        CHECK(!(hidKeysHeld() & KEY_A));
        printf(s_fail ? "hid_swap_test (mappings): %d failed\n" : "hid_swap_test (mappings): all passed\n",
               s_fail);
        return s_fail != 0;
    }

    frame();   // opens the pad
    s_joy = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(idx));
    if (s_joy == NULL) { printf("pad not opened\n"); return 2; }

    check_ps2_buttons();

    // HID_TEST_REST: the left stick rests off-centre, as one read on the
    // device. Its rest is measured, and it reads from there.
    if (getenv("HID_TEST_REST") != NULL) {
        const char* cal = getenv("NFSHP2_STICK_CAL");
        bool off = cal != NULL && cal[0] == '0';
        axis(SDL_CONTROLLER_AXIS_LEFTX, 9000);
        for (int i = 0; i < 40; i++) frame();
        hidCircleRead(&cp);
        if (off) {
            CHECK(cp.dx != 0);   // what the device did: steering at rest
            printf(s_fail ? "hid_swap_test (rest, unmeasured): %d failed\n"
                          : "hid_swap_test (rest, unmeasured): all passed\n", s_fail);
            return s_fail != 0;
        }
        CHECK(cp.dx == 0 && cp.dy == 0);
        CHECK(!(hidKeysHeld() & (KEY_CPAD_RIGHT | KEY_CPAD_LEFT)));
        axis(SDL_CONTROLLER_AXIS_LEFTX, 11000);   // jitter inside the dead zone
        frame();
        hidCircleRead(&cp);
        CHECK(cp.dx == 0);
        axis(SDL_CONTROLLER_AXIS_LEFTX, 32767);   // full tilt is still full, both ways
        frame();
        hidCircleRead(&cp);
        CHECK(cp.dx == 156);
        axis(SDL_CONTROLLER_AXIS_LEFTX, -32768);
        frame();
        hidCircleRead(&cp);
        CHECK(cp.dx == -156);
        axis(SDL_CONTROLLER_AXIS_LEFTX, 21000);   // part tilt is part, and to the right
        frame();
        hidCircleRead(&cp);
        CHECK(cp.dx > 0 && cp.dx < 156);
        axis(SDL_CONTROLLER_AXIS_LEFTX, 9000);
        frame();
        hidCircleRead(&cp);
        CHECK(cp.dx == 0);
        printf(s_fail ? "hid_swap_test (rest): %d failed\n" : "hid_swap_test (rest): all passed\n",
               s_fail);
        return s_fail != 0;
    }

    // The sticks' rest is measured over the first reads.
    for (int i = 0; i < 14; i++) frame();

    // NFSHP2_DPAD_STICK=1: swapped from the first frame.
    if (getenv("NFSHP2_DPAD_STICK") != NULL) {
        press(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1);
        frame();
        hidCircleRead(&cp);
        CHECK((hidKeysHeld() & dpad) == 0);
        CHECK(cp.dx == -156);
        printf(s_fail ? "hid_swap_test (started swapped): %d failed\n"
                      : "hid_swap_test (started swapped): all passed\n", s_fail);
        return s_fail != 0;
    }

    // Off: the D-pad is the D-pad, the stick the Circle Pad.
    press(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 32767);
    frame();
    hidCircleRead(&cp);
    CHECK((hidKeysHeld() & dpad) == KEY_DLEFT);
    CHECK(cp.dx == 156 && cp.dy == 0);
    press(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
    frame();

    // R1 alone reaches the game.
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    frame();
    CHECK(hidKeysHeld() & KEY_R);
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    frame();

    // Select + R1: swapped, a note, and that R1 kept from the game while held.
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    frame();
    CHECK(hidKeysHeld() & KEY_SELECT);
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    frame();
    CHECK(strcmp(s_toast, "D-PAD STEERS") == 0);
    CHECK(!(hidKeysHeld() & KEY_R));
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    frame();
    CHECK(!(hidKeysHeld() & KEY_R));   // still the combo's
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    frame();
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    frame();
    CHECK(hidKeysHeld() & KEY_R);      // a new press is the game's
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    frame();

    // Swapped: the D-pad is the Circle Pad at full tilt, up positive.
    press(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1);
    press(SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
    frame();
    hidCircleRead(&cp);
    CHECK((hidKeysHeld() & dpad) == 0);
    CHECK(cp.dx == -156 && cp.dy == 156);
    CHECK((hidKeysHeld() & (KEY_CPAD_LEFT | KEY_CPAD_UP)) == (KEY_CPAD_LEFT | KEY_CPAD_UP));
    press(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0);
    press(SDL_CONTROLLER_BUTTON_DPAD_UP, 0);
    frame();
    hidCircleRead(&cp);
    CHECK(cp.dx == 0 && cp.dy == 0);

    // ...and the stick is the D-pad: pressed past half tilt, let go under 30%.
    axis(SDL_CONTROLLER_AXIS_LEFTX, 12000);    // 37%: not yet
    frame();
    CHECK((hidKeysHeld() & dpad) == 0);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 20000);    // 61%
    frame();
    hidCircleRead(&cp);
    CHECK((hidKeysHeld() & dpad) == KEY_DRIGHT);
    CHECK(cp.dx == 0);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 12000);    // 37%: still held
    frame();
    CHECK((hidKeysHeld() & dpad) == KEY_DRIGHT);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 5000);     // 15%: let go
    frame();
    CHECK((hidKeysHeld() & dpad) == 0);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
    axis(SDL_CONTROLLER_AXIS_LEFTY, -32767);   // SDL's up is negative
    frame();
    CHECK((hidKeysHeld() & dpad) == KEY_DUP);
    CHECK(hidKeysDown() & KEY_DUP);
    axis(SDL_CONTROLLER_AXIS_LEFTY, 0);
    frame();

    // Select + R1 again: back as it was.
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    frame();
    CHECK(strcmp(s_toast, "STICK STEERS") == 0);
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    frame();
    press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 1);
    axis(SDL_CONTROLLER_AXIS_LEFTX, -32767);
    frame();
    hidCircleRead(&cp);
    CHECK((hidKeysHeld() & dpad) == KEY_DRIGHT);
    CHECK(cp.dx == -156);

    press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
    frame();

    // The meter's combos: Select + R3, and Select + L1 with that L1 kept
    // from the game while held.
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    press(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
    frame();
    CHECK(s_toggles == 1);
    CHECK(!(hidKeysHeld() & KEY_R3));
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    frame();
    CHECK(!(hidKeysHeld() & KEY_R3));
    press(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
    frame();
    press(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 1);
    frame();
    CHECK(hidKeysHeld() & KEY_R3);
    CHECK(s_toggles == 1);
    press(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0);
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    frame();
    press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);
    frame();
    CHECK(s_toggles == 2);
    CHECK(!(hidKeysHeld() & KEY_L));
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    frame();
    CHECK(!(hidKeysHeld() & KEY_L));
    press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0);
    frame();
    press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);
    frame();
    CHECK(hidKeysHeld() & KEY_L);
    CHECK(s_toggles == 2);
    press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0);
    frame();

    // The quality preset: Select + R2, with that R2 kept from the game.
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    frame();
    CHECK(s_quality == 0);
    CHECK(hidKeysHeld() & KEY_ZR);        // R2 alone is the game's
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    frame();
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    frame();
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    frame();
    CHECK(s_quality == 1);
    CHECK(!(hidKeysHeld() & KEY_ZR));
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    frame();
    CHECK(!(hidKeysHeld() & KEY_ZR));     // still the combo's
    CHECK(s_quality == 1);
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    frame();
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    frame();
    CHECK(hidKeysHeld() & KEY_ZR);

    printf(s_fail ? "hid_swap_test: %d failed\n" : "hid_swap_test: all passed\n", s_fail);
    SDL_Quit();
    return s_fail != 0;
}
