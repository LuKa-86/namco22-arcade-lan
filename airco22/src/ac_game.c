/*
 * ac_game.c -- Air Combat 22 (Namco 1995, Super System 22, MAME `airco22b`, Japan ACS1 Ver.B) as a table for the shared Super 22 host
 * (engine/ss22_run.c ...), made the same way as Alpine Surfer (alpines/) and Alpine Racer 2 (alpinr2/).
 *
 * The program is gen/ac_lifted_NN.c, made by tools/gen/lift68k.py from Ghidra's p-code (tools/import.sh, tools/regen.sh). The master
 * DSP and the sound MCU are interpreted (src/ac_interp.c, src/ac_snd_interp.c).
 *
 * MAME (namco/namcos22.cpp): machine `airco22b` = namcos22s + a "bodysonic" speaker (C352 channel 2 to a subwoofer behind the seat);
 * init_airco22: NAMCOS22_AIR_COMBAT22, NO KEYCUS (every read answers 0); S22-BIOS 1.20. Inputs `airco22`: coin 0x01, service 0x04,
 * test 0x08, missile 0x10, gun trigger 0x20, start (also the view change) 0x40; ADC 0 / 1 the stick (X / Y, 0x100..0x300), ADC 2 the
 * throttle stick (springs back to centre, 0x100..0x300). No default EEPROM in the set (the game sets a blank one up).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ss22_game.h"
#include "ac_lifted.h"
#include "lift_rt.h"

bool ac_c25_exec(c71_t *d, int pc);          /* src/ac_interp.c */

static const eng_rom_t k_roms[] = {
    {"acs1ccrh.1d", 0x80000}, {"acs1ccrl.3d", 0x200000},
    {"acs1cg0.8d", 0x200000}, {"acs1cg1.10d", 0x200000}, {"acs1cg2.12d", 0x200000}, {"acs1cg3.13d", 0x200000},
    {"acs1cg4.14d", 0x200000}, {"acs1cg5.16d", 0x200000}, {"acs1cg6.18d", 0x200000}, {"acs1cg7.19d", 0x200000},
    {"acs1data.8k", 0x80000},
    {"acs1ptl0.18k", 0x80000}, {"acs1ptl1.16k", 0x80000}, {"acs1ptl2.15k", 0x80000}, {"acs1ptl3.14k", 0x80000},
    {"acs1ptm0.18j", 0x80000}, {"acs1ptm1.16j", 0x80000}, {"acs1ptm2.15j", 0x80000}, {"acs1ptm3.14j", 0x80000},
    {"acs1ptu0.18f", 0x80000}, {"acs1ptu1.16f", 0x80000}, {"acs1ptu2.15f", 0x80000}, {"acs1ptu3.14f", 0x80000},
    {"acs1scg0.12f", 0x200000}, {"acs1scg1.10f", 0x200000},
    {"acs1wav0.1", 0x400000}, {"acs1wav1.2", 0x400000},
    {"acs1verb.1", 0x100000}, {"acs1verb.2", 0x100000}, {"acs1verb.3", 0x100000}, {"acs1verb.4", 0x100000},
};
#define NROMS ((int)(sizeof k_roms / sizeof k_roms[0]))

static bool ac_load_program(const char *dir)
{
    /* MAME ROM_LOAD32_BYTE: acs1verb.4 / .3 / .2 / .1 -> byte 0/1/2/3 of every long, 1 MB each */
    static const char *lane[4] = { "acs1verb.4", "acs1verb.3", "acs1verb.2", "acs1verb.1" };
    for (int l = 0; l < 4; l++) {
        char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, lane[l]);
        FILE *f = fopen(p, "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", p); return false; }
        for (uint32_t i = 0; i < 0x100000; i++) {
            int c = fgetc(f);
            if (c == EOF) { fclose(f); fprintf(stderr, "short %s\n", p); return false; }
            g_ss22.rom[i * 4 + (uint32_t)l] = (uint8_t)c;
        }
        fclose(f);
    }
    return true;
}

static bool ac_load_eeprom(const char *dir) { (void)dir; return false; }   /* not in MAME's set: the game sets a blank EEPROM up itself */

static const ss22_board_cfg ac_board = { "AC", 0, 0, ac_load_program, ac_load_eeprom, NULL, NULL, true };   /* no keycus: every read 0 */

#define IN_COIN    0x0001
#define IN_SERVICE 0x0004
#define IN_TEST    0x0008
#define IN_MISSILE 0x0010
#define IN_GUN     0x0020
#define IN_START   0x0040
/* the stick's Y and the throttle are read in ac_adc (below) from these keys / the pad; X is the engine's wheel axis */
static const ss22_action actions[] = {
    { "Insert coin",      "coin",      SDL_SCANCODE_5,      SDL_SCANCODE_6,        IN_COIN,    0,                   SS22_PAD(SDL_CONTROLLER_BUTTON_BACK) },
    { "Start / view",     "start",     SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, IN_START,   0,                   SS22_PAD(SDL_CONTROLLER_BUTTON_START) | SS22_PAD(SDL_CONTROLLER_BUTTON_Y) },
    { "Gun trigger",      "gun",       SDL_SCANCODE_SPACE,  SDL_SCANCODE_LCTRL,    IN_GUN,     0,                   SS22_PAD(SDL_CONTROLLER_BUTTON_A) },
    { "Missile",          "missile",   SDL_SCANCODE_LALT,   SDL_SCANCODE_X,        IN_MISSILE, 0,                   SS22_PAD(SDL_CONTROLLER_BUTTON_B) },
    { "Service",          "service",   SDL_SCANCODE_9,      SDL_SCANCODE_UNKNOWN,  IN_SERVICE, 0,                   0 },
    { "Test",             "test",      SDL_SCANCODE_F2,     SDL_SCANCODE_UNKNOWN,  IN_TEST,    0,                   0 },
    { "Stick left",       "left",      SDL_SCANCODE_LEFT,   SDL_SCANCODE_UNKNOWN,  0,          SS22_AX_WHEEL_LEFT,  0 },
    { "Stick right",      "right",     SDL_SCANCODE_RIGHT,  SDL_SCANCODE_UNKNOWN,  0,          SS22_AX_WHEEL_RIGHT, 0 },
};
static const ss22_input_game input = {
    actions, (int)(sizeof actions / sizeof *actions),
    0x100, 0x300, 0x100, 12,
    { 0x100, 0x100 }, 16,
    { "Enter, then press the new key (Esc cancels)", "Stick: arrows (up / down pitch); throttle: W / S (springs back)",
      "Pad: left stick flies, right stick up / down throttle, A gun, B missile" },
    ss22_snd_inputs,
    IN_TEST, IN_SERVICE,
};

static const ss22_press_name presses[] = { {"coin", IN_COIN}, {"start", IN_START}, {"service", IN_SERVICE}, {"test", IN_TEST},
                                           {"gun", IN_GUN}, {"missile", IN_MISSILE} };

static int auto_pitch, auto_throttle;           /* the test player's stick Y / throttle (centre 0) */
static void autoplay(long n, uint16_t *p, unsigned *wheel, unsigned *pedal1, unsigned *pedal2)
{
    (void)pedal1; (void)pedal2;
    /* AC_AUTO picks the test player: 0 flies about and fires (default), 4 never coins up (the attract loop), 6 goes into the test menu */
    static int v = -1; if (v < 0) { const char *e = getenv("AC_AUTO"); v = e ? atoi(e) : 0; }
    *wheel = 0x200; auto_pitch = auto_throttle = 0;
    if (v == 4) return;
    if (v == 6) {
        if (n > 300 && n < 9000) *p |= IN_TEST;
        if (n % 480 >= 300 && n % 480 < 306) *p |= IN_START;
        if (n % 1200 >= 700 && n % 1200 < 706) *p |= IN_SERVICE;
        return;
    }
    if (n % 600 >= 300 && n % 600 < 306) *p |= IN_COIN;
    if (n % 300 >= 150 && n % 300 < 156 && n > 400) *p |= IN_START;
    if (n % 40 < 20) *p |= IN_GUN;
    if (n % 400 < 10) *p |= IN_MISSILE;
    *wheel = ((n / 120) % 2 == 0) ? 0x180 : 0x280;
    auto_pitch = ((n / 200) % 2 == 0) ? 0x60 : -0x60;
    auto_throttle = 0x80;
}

uint8_t g_lift_cov[0x200000];                /* gen/: which instructions have run (LIFT_LAST_PC) */
uint32_t g_lift_pc;                          /* gen/: the instruction last started (LIFT_LAST_PC) */
static void ac_frame(long n)
{
    { static int on = -1; if (on < 0) on = getenv("AC_PCLOG") != NULL;      /* AC_PCLOG=1: where the 68K is at each frame's end */
      if (on && n % 20 == 0) fprintf(stderr, "[AC] f%ld pc %06X  D0 %08X A0 %08X SP %08X\n", n, (unsigned)g_lift_pc, (unsigned)RG4(0x00), (unsigned)RG4(0x20), (unsigned)RG4(0x3C)); }
    { static long c0 = -2, c1; if (c0 == -2) { c0 = -1; const char *e = getenv("AC_COV"); if (e) sscanf(e, "%ld:%ld", &c0, &c1); }   /* AC_COV=from:to -> ac_cov.txt */
      if (c0 >= 0 && n == c0) memset(g_lift_cov, 0, sizeof g_lift_cov);
      if (c0 >= 0 && n == c1) { FILE *f = fopen("ac_cov.txt", "w"); if (f) { for (unsigned a = 0; a < sizeof g_lift_cov; a++) if (g_lift_cov[a]) fprintf(f, "%X\n", a << 1); fclose(f); } } }
}

/* THE STICK'S Y (ADC 1) AND THE THROTTLE (ADC 2): up / down arrows pitch, W / S throttle (it springs back to the middle, as the
 * cabinet's); a pad's left stick Y and right stick Y. X (ADC 0) is the engine's wheel axis: left / right arrows, the pad's left stick. */
static SDL_GameController *ac_pad;
static int ac_axis(SDL_GameControllerAxis ax)
{
    if (!ac_pad || !SDL_GameControllerGetAttached(ac_pad)) {
        ac_pad = NULL;
        for (int i = 0; i < SDL_NumJoysticks() && !ac_pad; i++) if (SDL_IsGameController(i)) ac_pad = SDL_GameControllerOpen(i);
        if (!ac_pad) return 0;
    }
    const int v = SDL_GameControllerGetAxis(ac_pad, ax);
    return v > -6000 && v < 6000 ? 0 : v;
}
static uint16_t clamp_adc(int v) { return (uint16_t)(v < 0x100 ? 0x100 : v > 0x300 ? 0x300 : v); }
static void ac_adc(uint16_t *adc)
{
    int y = 0, t = 0;
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if (k[SDL_SCANCODE_UP]) y -= 0x100;
        if (k[SDL_SCANCODE_DOWN]) y += 0x100;
        if (k[SDL_SCANCODE_W]) t += 0x100;
        if (k[SDL_SCANCODE_S]) t -= 0x100;
        y += ac_axis(SDL_CONTROLLER_AXIS_LEFTY) / 128;
        t -= ac_axis(SDL_CONTROLLER_AXIS_RIGHTY) / 128;
    }
    y += auto_pitch; t += auto_throttle;
    adc[1] = clamp_adc(0x200 + y);
    adc[2] = clamp_adc(0x200 + t);
}

static const ss22_game game = {
    .name = "Air Combat 22", .tag = "AC", .lname = "ac", .logname = "airco22.log", .zip = "airco22b.zip",
    .board = &ac_board,
    .dsp = { { {"acs1ptl0.18k", "acs1ptl1.16k", "acs1ptl2.15k", "acs1ptl3.14k"},
               {"acs1ptm0.18j", "acs1ptm1.16j", "acs1ptm2.15j", "acs1ptm3.14j"},
               {"acs1ptu0.18f", "acs1ptu1.16f", "acs1ptu2.15f", "acs1ptu3.14f"} }, 4, ac_c25_exec },
    /* MAME's c352 region: acs1wav0.1 at 0x400000, acs1wav1.2 at 0x800000. ADC 1 and 2 are set in ac_adc (the pedals' slots unused) */
    .snd = { "acs1data.8k", { "acs1wav0.1", "acs1wav1.2" }, { 0x400000, 0x800000 }, 0x1000000, 0xFFFF, { 1, 2 } },
    .video = { { "acs1cg0.8d", "acs1cg1.10d", "acs1cg2.12d", "acs1cg3.13d", "acs1cg4.14d", "acs1cg5.16d", "acs1cg6.18d", "acs1cg7.19d" },
               "acs1ccrl.3d", "acs1ccrh.1d", { "acs1scg0.12f", "acs1scg1.10f" }, 2, 0x1000000, 0xFF,
               true },                          /* the VICS sprite count is in the list (MAME: "a workaround is needed for airco22b and dirtdash") */
    .input = &input,
    .roms = k_roms, .n_roms = NROMS,
    .entry = L_5000,
    .polls_per_frame = 66772,           /* Tokyo Wars' figure (same CPU, same clock): not measured for this game */
    .presses = presses, .n_presses = (int)(sizeof presses / sizeof *presses),
    .autoplay = autoplay,
    .pedal_full = { 0x100, 0x100 },
    .link_id_byte = -1,
    .frame = ac_frame,
};

int main(int argc, char **argv) { g_ss22_adc_hook = ac_adc; return ss22_main(argc, argv, &game); }
