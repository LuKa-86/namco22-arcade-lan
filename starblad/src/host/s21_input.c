/*
 * s21_input.c -- Cyber Sled's cabinet on a keyboard, gamepads and joysticks: two sticks (the sled is driven tank-style, left and right
 * stick forward/back), Gun, Missile, Viewport, coin, start, the Test switch (a TOGGLE, like MAME's PORT_SERVICE) and the Service
 * button. EVERY direction of both sticks and every button is bound on the Controls page (engine/twin_stick.c): a key, a gamepad
 * button or stick, or any joystick's axis, hat or button -- two joysticks of the same model are told apart. The defaults are MAME's
 * keys for cybsled (left stick E/D and S/F, right stick I/K and J/L), the arrows as "tank" controls and a gamepad's two sticks; a
 * held key ramps the stick 16 counts a frame (PORT_KEYDELTA 16) and it springs back when released, an axis sets it directly.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "eng_cfg.h"
#include "eng_ui.h"
#include "twin_stick.h"
#include "eng_party.h"
#include "s21_host.h"
#include "s21_board.h"
#include "s21_io.h"
#include "ss22_host.h"
#include "s21_cpu.h"                               /* s21_bus_read: the credit count (free play) */

/* the Controls page's bindable rows (twin_stick.h: the eight directions first) */
/* STARBLADE: one gun (two pots: AN1 = X, AN2 = Y) with a fire button on each side -- SHOOT1 (MCUH 0x20) and SHOOT2 (0x08); both fire
 * in the game, the test menu uses SHOOT1 to enter and SHOOT2 to leave. The mouse aims it over the picture (left button SHOOT1, right
 * SHOOT2, middle START); the rows below aim it from keys and pads (either stick), and fire. Test menu: Menu up / down (MCUH 0x01 / 0x02)
 * pick a line, the gun pushed right / left changes it. */
enum { CS_GUN = TS_NDIR, CS_SHOOT2, CS_SEL_UP, CS_SEL_DOWN, CS_COIN, CS_START, CS_SERVICE, CS_NCTL };
static const ts_def cs_ctl[CS_NCTL] = {
    [TS_L_UP]    = { "Aim up",    "aim_up",    "Up", "-lefty,dpup" },       [TS_L_DOWN]  = { "Aim down",  "aim_down",  "Down", "+lefty,dpdown" },
    [TS_L_LEFT]  = { "Aim left",  "aim_left",  "Left", "-leftx,dpleft" },   [TS_L_RIGHT] = { "Aim right", "aim_right", "Right", "+leftx,dpright" },
    [TS_R_UP]    = { "Aim up (2nd)",    "aim2_up",    "W", "-righty" },     [TS_R_DOWN]  = { "Aim down (2nd)",  "aim2_down",  "S", "+righty" },
    [TS_R_LEFT]  = { "Aim left (2nd)",  "aim2_left",  "A", "-rightx" },     [TS_R_RIGHT] = { "Aim right (2nd)", "aim2_right", "D", "+rightx" },
    [CS_GUN]     = { "Fire (left button)",  "fire1", "Left Ctrl,Z,Space", "a,+righttrigger" },
    [CS_SHOOT2]  = { "Fire (right button)", "fire2", "Left Alt,X", "b,+lefttrigger" },
    [CS_SEL_UP]  = { "Test menu up",   "select_up",   "Page Up", "leftshoulder" },
    [CS_SEL_DOWN] = { "Test menu down", "select_down", "Page Down", "rightshoulder" },
    [CS_COIN]    = { "Coin",     "coin",    "5", "back" },    [CS_START] = { "Start", "start", "1,Return", "start" },
    [CS_SERVICE] = { "Service button", "service", "9", NULL },
};
static void cfg_save(const char *k, const char *v) { eng_cfg_set(k, v); }

static int test_on, test_prev;
static int axis[4] = { 0x7F, 0x7F, 0x7F, 0x7F };       /* an0 right Y, an1 left Y, an2 right X, an3 left X */

/* CS_FAKEPAD=1 (TEST): an SDL virtual game controller (Xbox 360 ids) attached at start and driven by a fixed script, so both analog
 * sticks reach the levers through the same SDL game-controller path a real pad uses (the Linux driver is the one part it skips).
 * Script, in frames: Back (coin) 240, Start 330, A x4 from 420 (picks Training), then from 2000: both sticks up 180 f, left down +
 * right up 120 f, both left 120 f, both half up 120 f, centre, D-pad up 120 f. Read the result with CS_INLOG=1. */
static int fake_idx = -1;
static SDL_Joystick *fake_js;
static void fakepad_init(void)
{
    if (!getenv("CS_FAKEPAD")) return;
#if SDL_VERSION_ATLEAST(2, 24, 0)                        /* SDL_JoystickAttachVirtualEx: SDL 2.24 (Ubuntu 22.04 has 2.0.20) */
    SDL_VirtualJoystickDesc d; SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION; d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX; d.nbuttons = SDL_CONTROLLER_BUTTON_MAX; d.vendor_id = 0x045E; d.product_id = 0x028E;
    d.name = "Virtual X-Box 360 pad (CS_FAKEPAD)";
    fake_idx = SDL_JoystickAttachVirtualEx(&d);
    if (fake_idx >= 0) fake_js = SDL_JoystickOpen(fake_idx);
    fprintf(stderr, "[INPUT] CS_FAKEPAD: virtual pad %s\n", fake_js ? "attached" : SDL_GetError());
#else
    fprintf(stderr, "[INPUT] CS_FAKEPAD: needs SDL 2.24 or later (this build: %d.%d.%d)\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#endif
}
static void fakepad_frame(void)
{
    if (!fake_js) return;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    static int f; f++;
    int lx = 0, ly = 0, rx = 0, ry = 0, hat_up = 0;
    const int t = f - 2000;
    if (t >= 0 && t < 180) { ly = -32767; ry = -32767; }
    else if (t >= 180 && t < 300) { ly = 32767; ry = -32767; }
    else if (t >= 300 && t < 420) { lx = -32767; rx = -32767; }
    else if (t >= 420 && t < 540) { ly = -16000; ry = -16000; }
    else if (t >= 600 && t < 720) hat_up = 1;
    SDL_JoystickSetVirtualAxis(fake_js, SDL_CONTROLLER_AXIS_LEFTX, (Sint16)lx); SDL_JoystickSetVirtualAxis(fake_js, SDL_CONTROLLER_AXIS_LEFTY, (Sint16)ly);
    SDL_JoystickSetVirtualAxis(fake_js, SDL_CONTROLLER_AXIS_RIGHTX, (Sint16)rx); SDL_JoystickSetVirtualAxis(fake_js, SDL_CONTROLLER_AXIS_RIGHTY, (Sint16)ry);
    SDL_JoystickSetVirtualButton(fake_js, SDL_CONTROLLER_BUTTON_DPAD_UP, (Uint8)hat_up);
    SDL_JoystickSetVirtualButton(fake_js, SDL_CONTROLLER_BUTTON_BACK, (Uint8)(f >= 240 && f < 246));
    SDL_JoystickSetVirtualButton(fake_js, SDL_CONTROLLER_BUTTON_START, (Uint8)(f >= 330 && f < 336));
    SDL_JoystickSetVirtualButton(fake_js, SDL_CONTROLLER_BUTTON_A, (Uint8)(f >= 420 && f < 700 && (f - 420) % 72 < 6));
#endif
}

/* ---- A WHEEL AND PEDALS (raw joysticks, the ones SDL has no gamepad mapping for) --------------------------------------------
 * The wheel TURNS the sled (the two levers opposite, as the arrows' Left/Right; outside a battle it moves both levers sideways to
 * pick in the menus), the gas pedal pushes both levers forward, the brake pedal both back. Bound on the Controls page (Enter on a
 * row, then move the wheel / press the pedal) and saved as joy_turn / joy_gas / joy_brake in cs21_controls.cfg. An unbound wheel
 * is axis 0 of any raw joystick that has been turned (more than 1/8 of the range from where it first read, so a pedal set resting
 * off centre does not steer); pedals have no default. Cyber Commando does the same through rr_host.c. */
#define MAXJOY 8
static SDL_Joystick *joys[MAXJOY];
typedef struct { int axis; int invert; int dir; int rest; char guid[40]; } rawbind;   /* axis -1 = unbound; dir/rest: a pedal's travel */
static rawbind b_turn = { 0, 0, 0, 0, "" }, b_gas = { -1, 0, 0, 0, "" }, b_brake = { -1, 0, 0, 0, "" };
static int bind_loaded, learn = -1, learn_base[MAXJOY][32], turn_seen[MAXJOY], turn_rest[MAXJOY], turn_live[MAXJOY];

static void guid_of(SDL_Joystick *j, char *out) { SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), out, 40); }
static void bind_save(const char *key, const rawbind *b)
{
    char v[96]; snprintf(v, sizeof v, "%d,%d,%d,%d,%s", b->axis, b->invert, b->dir, b->rest, b->guid);
    eng_cfg_set(key, v);
}
static void bind_load(const char *key, rawbind *b)
{
    const char *v = eng_cfg_get(key); if (!v) return;
    rawbind t = *b; char g[48] = "";
    if (sscanf(v, "%d,%d,%d,%d,%47s", &t.axis, &t.invert, &t.dir, &t.rest, g) >= 4) { snprintf(t.guid, sizeof t.guid, "%s", g); *b = t; }
}
static void joy_open(int idx)
{
    if (SDL_IsGameController(idx)) return;
    if (!eng_party_allows(idx)) return;                       /* --pad K (a Party window): no wheel / raw joystick (engine/eng_party.h) */
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(idx);
    int free_slot = -1;
    for (int i = 0; i < MAXJOY; i++) { if (joys[i] && SDL_JoystickInstanceID(joys[i]) == id) return; if (!joys[i] && free_slot < 0) free_slot = i; }
    if (free_slot < 0) return;
    if ((joys[free_slot] = SDL_JoystickOpen(idx))) {
        turn_seen[free_slot] = turn_live[free_slot] = 0;
        fprintf(stderr, "[INPUT] joystick %d: %s (%d axes, %d buttons) -- wheel / pedals: Controls page, Enter on a row to bind\n", free_slot,
                SDL_JoystickName(joys[free_slot]), SDL_JoystickNumAxes(joys[free_slot]), SDL_JoystickNumButtons(joys[free_slot]));
    }
}
static int joy_axes(int i) { int n = SDL_JoystickNumAxes(joys[i]); return n > 32 ? 32 : n; }
static int bound_to(const rawbind *b, int i) { char g[40]; if (!b->guid[0]) return 1; guid_of(joys[i], g); return !strcmp(g, b->guid); }

/* the wheel, -1 (left) .. +1 (right); *on = it is turned past its deadzone */
static double raw_turn(int *on)
{
    double best = 0;
    for (int i = 0; i < MAXJOY; i++) {
        if (!joys[i] || b_turn.axis < 0 || b_turn.axis >= joy_axes(i) || !bound_to(&b_turn, i)) continue;
        if (!b_turn.guid[0] && ts_uses_joystick(joys[i])) continue;      /* the unbound default is no wheel on a joystick that IS a stick */
        int v = SDL_JoystickGetAxis(joys[i], b_turn.axis);
        if (!turn_seen[i]) { turn_seen[i] = 1; turn_rest[i] = v; }
        if (!turn_live[i] && (b_turn.guid[0] || abs(v - turn_rest[i]) > 4096)) turn_live[i] = 1;
        if (!turn_live[i]) continue;
        if (b_turn.invert) v = -v;
        double f = v / 32767.0;
        f = f > -0.003 && f < 0.003 ? 0 : (f > 0 ? f - 0.003 : f + 0.003) / 0.997;      /* a wheel's own sensor noise and nothing more */
        if (f > 1) f = 1; if (f < -1) f = -1;
        if (fabs(f) > fabs(best)) best = f;
    }
    *on = best != 0;
    return best;
}
static double raw_pedal(const rawbind *b)                /* 0 .. 1 */
{
    double best = 0;
    for (int i = 0; i < MAXJOY; i++) {
        if (!joys[i] || b->axis < 0 || b->axis >= joy_axes(i) || !bound_to(b, i)) continue;
        int r = SDL_JoystickGetAxis(joys[i], b->axis), range = b->dir > 0 ? 32767 - b->rest : b->rest + 32768, d = b->dir > 0 ? r - b->rest : b->rest - r;
        double f = range > 0 ? (double)d / range : 0;
        f = f > 0.03 ? (f - 0.03) / 0.97 : 0;
        if (f > 1) f = 1;
        if (f > best) best = f;
    }
    return best;
}
static void learn_begin(int which)
{
    learn = which;
    for (int i = 0; i < MAXJOY; i++) if (joys[i]) for (int a = 0; a < joy_axes(i); a++) learn_base[i][a] = SDL_JoystickGetAxis(joys[i], a);
    fprintf(stderr, "[INPUT] binding %s: move it now\n", which == 0 ? "the wheel (turn it RIGHT)" : which == 1 ? "the gas pedal (press it)" : "the brake pedal (press it)");
}
static void learn_step(void)
{
    for (int i = 0; i < MAXJOY && learn >= 0; i++) {
        if (!joys[i]) continue;
        for (int a = 0; a < joy_axes(i); a++) {
            int d = SDL_JoystickGetAxis(joys[i], a) - learn_base[i][a];
            if (abs(d) < 16000) continue;
            rawbind *b = learn == 0 ? &b_turn : learn == 1 ? &b_gas : &b_brake;
            b->axis = a; guid_of(joys[i], b->guid);
            if (learn == 0) { b->invert = d < 0; b->dir = b->rest = 0; turn_seen[i] = 0; turn_live[i] = 1; }
            else { b->invert = 0; b->dir = d > 0 ? 1 : -1; b->rest = learn_base[i][a]; }
            bind_save(learn == 0 ? "joy_turn" : learn == 1 ? "joy_gas" : "joy_brake", b);
            fprintf(stderr, "[INPUT] bound to %s axis %d%s\n", SDL_JoystickName(joys[i]), a, learn == 0 && b->invert ? " (inverted)" : "");
            learn = -1;
            break;
        }
    }
}
void s21_input_init(void)
{
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    ts_init(cs_ctl, CS_NCTL, cfg_save);
    ts_load_from(eng_cfg_get);
    fakepad_init();
}

/* the mouse wheel in the test menu: a notch = a short press of Menu up / down (8 frames down, then 8 up, so each notch is one step) */
static int wheel_up, wheel_down;
void s21_input_event(const SDL_Event *e)
{
    ts_event(e);
    if (e->type == SDL_MOUSEWHEEL && e->wheel.y > 0 && wheel_up <= 0 && wheel_down <= 0) wheel_up = 16;
    if (e->type == SDL_MOUSEWHEEL && e->wheel.y < 0 && wheel_up <= 0 && wheel_down <= 0) wheel_down = 16;
    if (e->type == SDL_JOYDEVICEADDED) joy_open(e->jdevice.which);
    else if (e->type == SDL_JOYDEVICEREMOVED) {
        for (int i = 0; i < MAXJOY; i++) if (joys[i] && SDL_JoystickInstanceID(joys[i]) == e->jdevice.which) { SDL_JoystickClose(joys[i]); joys[i] = NULL; }
    }
}

static void ramp(int *v, int dec, int inc)
{
    const int lo = 0x3F, hi = 0xBF, c = 0x7F;
    if (dec && !inc) *v = *v - 16 < lo ? lo : *v - 16;
    else if (inc && !dec) *v = *v + 16 > hi ? hi : *v + 16;
    else if (*v < c) *v = *v + 16 > c ? c : *v + 16;     /* springs back */
    else if (*v > c) *v = *v - 16 < c ? c : *v - 16;
}

static s21_inputs cur = { .mcub = 0xFF, .mcuc = 0xFF, .mcuh = 0xFF, .dsw = 0xDF, .an = { 0x7F, 0x7F, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF }, .dial = { 0xFF, 0xFF, 0xFF, 0xFF } };

/* move an axis toward a target at the keyboard's 16 counts a frame (MAME's PORT_KEYDELTA(16) for these ports) */
static void toward(int *v, int t) { if (*v < t) *v = *v + 16 > t ? t : *v + 16; else if (*v > t) *v = *v - 16 < t ? t : *v - 16; }

void s21_input_update(void)
{
    fakepad_frame();
    SDL_GameControllerUpdate();
    /* THE GUN (MAME starblad: AN1 = X 0x01..0xFF, AN2 = Y 0x18..0xE8; the game draws its own sight and calibrates the pots in its
     * test mode). The MOUSE aims it while it moves over the picture -- absolute, like the cabinet's gun (and like an absolute light
     * gun: Sinden, Gun4IR ...). A stick sets it directly (centre = middle); a key, hat or D-pad moves it 8 counts a frame and it stays
     * where it is left, as the real gun does. */
    static int gx = 0x80, gy = 0x80, mlx = -1, mly = -1;
    {
        float nx, ny; bool inside; int mx, my;
        SDL_GetMouseState(&mx, &my);
        if ((mx != mlx || my != mly) && ss22_host_pointer(&nx, &ny, &inside) && inside) {
            gx = 0x01 + (int)lround(nx * 0xFE); gy = 0x18 + (int)lround(ny * 0xD0);
        }
        mlx = mx; mly = my;
    }
    for (int s = 0; s < 2; s++) {
        bool dx, dy;
        const double sx = ts_lever(s ? TS_R_LEFT : TS_L_LEFT, s ? TS_R_RIGHT : TS_L_RIGHT, &dx), sy = ts_lever(s ? TS_R_UP : TS_L_UP, s ? TS_R_DOWN : TS_L_DOWN, &dy);
        if (dx) gx += sx < 0 ? -8 : sx > 0 ? 8 : 0; else if (sx != 0) gx = 0x80 + (int)lround(sx * 0x7F);
        if (dy) gy += sy < 0 ? -8 : sy > 0 ? 8 : 0; else if (sy != 0) gy = 0x80 + (int)lround(sy * 0x68);
    }
    gx = gx < 0x01 ? 0x01 : gx > 0xFF ? 0xFF : gx; gy = gy < 0x18 ? 0x18 : gy > 0xE8 ? 0xE8 : gy;
    s21_inputs in = cur;
    in.an[0] = in.an[3] = 0xFF;                          /* unused channels (MAME: IP_ACTIVE_LOW) */
    in.an[1] = (uint8_t)gx; in.an[2] = (uint8_t)gy;
    const Uint32 mb = SDL_GetMouseState(NULL, NULL);
    const int gun = ts_held(CS_GUN) || (mb & SDL_BUTTON(SDL_BUTTON_LEFT));
    const int fire2 = ts_held(CS_SHOOT2) || (mb & SDL_BUTTON(SDL_BUTTON_RIGHT));
    const int coin = ts_held(CS_COIN), start = ts_held(CS_START) || (mb & SDL_BUTTON(SDL_BUTTON_MIDDLE)), service = ts_held(CS_SERVICE);
    const int test = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F2];
    if (test && !test_prev) { test_on = !test_on; fprintf(stderr, "[INPUT] test switch %s\n", test_on ? "ON" : "OFF"); }
    test_prev = test;
    const int wup = wheel_up-- > 8, wdn = wheel_down-- > 8;    /* the wheel: held for the first 8 of its 16 frames */
    in.mcuh = (uint8_t)(0xFF & ~(gun ? 0x20 : 0) & ~(fire2 ? 0x08 : 0) & ~(ts_held(CS_SEL_UP) || wup ? 0x01 : 0) & ~(ts_held(CS_SEL_DOWN) || wdn ? 0x02 : 0));
    /* FREE PLAY WITH THE ATTRACT MODE: the game's own FREE PLAY = YES never leaves the title (it waits there whenever it has a credit),
     * so the package runs it at 1 coin 1 credit and START with no credit drops a coin first: 6 frames of coin, then START held from
     * frame 12. The credit count is master work RAM 0x10E022 (measured: 0, 1, 2 with coins, one less after START). */
    static int vfp, start_prev;
    if (start && !start_prev && !vfp && !test_on && (s21_bus_read(0, 0x10E022, 2) & 0xFFFF) == 0) vfp = 1;
    start_prev = start;
    int vcoin = 0, vstart = 0;
    if (vfp) { vcoin = vfp <= 6; vstart = vfp >= 12; if (++vfp > 20) vfp = 0; }
    in.mcuc = (uint8_t)(0xFF & ~(coin || vcoin ? 0x20 : 0) & ~(service ? 0x80 : 0));
    in.mcub = (uint8_t)(0xFF & ~((start && !(vfp && vfp < 12)) || vstart ? 0x80 : 0));
    in.dsw = (uint8_t)((cur.dsw & ~1) | (test_on ? 0 : 1));
    { static s21_inputs prev; static int log = -1; if (log < 0) log = getenv("CS_INLOG") != NULL;   /* CS_INLOG=1: every change of the ports */
      s21_debug_frame(&in);
      if (log && memcmp(&prev, &in, sizeof in)) { fprintf(stderr, "[INPUT] b %02X c %02X h %02X dsw %02X an %02X %02X %02X %02X\n", in.mcub, in.mcuc, in.mcuh, in.dsw, in.an[0], in.an[1], in.an[2], in.an[3]); prev = in; } }
    s21_io_set_inputs(&in);
}

void s21_input_set_test(int on) { if (test_on != !!on) fprintf(stderr, "[INPUT] test switch %s\n", on ? "ON" : "OFF"); test_on = !!on; }

void s21_input_neutral(void)
{
    s21_inputs in = cur; in.dsw = (uint8_t)((cur.dsw & ~1) | (test_on ? 0 : 1));
    s21_debug_frame(&in);
    for (int i = 0; i < 4; i++) axis[i] = 0x7F;
    s21_io_set_inputs(&in);
}

/* the Controls page: every control (twin_stick.c), the axis deadzone, the Test switch, the wheel and pedals, a reset */
enum { P_DEADZONE = CS_NCTL, P_TEST, P_RESET, P_N, P_WHEEL = P_N };   /* Starblade: no wheel rows */
static int nrows(void) { return P_N; }
static bool has_value(int r) { return r != P_RESET; }
static bool capture_cb(const SDL_Event *e, void *u) { (void)u; return ts_capture_event(e); }
static void text(int r, char *l, size_t ln, char *v, size_t vn)
{
    if (r < CS_NCTL) { snprintf(l, ln, "%s", ts_label(r)); ts_value_text(r, v, vn); return; }
    switch (r) {
    case P_DEADZONE: snprintf(l, ln, "Stick deadzone"); snprintf(v, vn, "%d%%", ts_deadzone()); break;
    case P_TEST: snprintf(l, ln, "Test switch"); snprintf(v, vn, "%s (F2)", test_on ? "ON" : "OFF"); break;
    case P_RESET: snprintf(l, ln, "Reset all controls"); snprintf(v, vn, "Enter: every row above back to its defaults"); break;
    default: {
        const int w = r - P_WHEEL;
        const rawbind *b = w == 0 ? &b_turn : w == 1 ? &b_gas : &b_brake;
        snprintf(l, ln, "%s", w == 0 ? "Racing wheel (optional): turn" : w == 1 ? "Racing wheel (optional): gas" : "Racing wheel (optional): brake");
        if (learn == w) snprintf(v, vn, "%s", w == 0 ? "turn the wheel RIGHT ..." : "press the pedal ...");
        else if (b->axis < 0) snprintf(v, vn, "not bound (Enter to bind)");
        else snprintf(v, vn, "axis %d%s (Enter to rebind, Left clears)", b->axis, b->invert ? " inverted" : "");
    } }
}
static void change(int r, int dir)
{
    if (r < CS_NCTL) {
        ts_change(r, dir);
        if (dir == 0) eng_ui_capture_input(capture_cb, NULL);
        return;
    }
    switch (r) {
    case P_DEADZONE: { int v = ts_deadzone() + (dir ? dir : 1) * 5; if (!dir && v > 50) v = 0; ts_set_deadzone(v); break; }   /* Enter cycles */
    case P_TEST: test_on = !test_on; fprintf(stderr, "[INPUT] test switch %s\n", test_on ? "ON" : "OFF"); break;
    case P_RESET: if (dir == 0) for (int c = 0; c < CS_NCTL; c++) ts_change(c, +1); break;
    default: {
        const int w = r - P_WHEEL;
        rawbind *b = w == 0 ? &b_turn : w == 1 ? &b_gas : &b_brake;
        if (dir < 0) { b->axis = w == 0 ? 0 : -1; b->guid[0] = 0; b->invert = b->dir = b->rest = 0; bind_save(w == 0 ? "joy_turn" : w == 1 ? "joy_gas" : "joy_brake", b); learn = -1; }
        else if (dir == 0) learn_begin(w);
    } }
}
static void notes(void (*line)(const char *fmt, ...))
{
    line("Enter on a row, then press a key or button, push a hat or move a stick (Esc cancels).");
    line("A key replaces the row's keys, a pad input its pad inputs; a joystick fills one of two joystick slots.");
    line("Left clears a row, Right puts its defaults back. Two joysticks of the same model are told apart (#2).");
}
static const eng_ui_page page = { "Controls", 640, 170, 20, nrows, has_value, NULL, text, change, notes };
const eng_ui_page *s21_input_page(void) { return &page; }
