/*
 * s21_cabinet.c -- Starblade's cabinet effects on a PC, each one a setting (the menu's "Cabinet" page, saved in sb21_controls.cfg).
 *
 * The game drives the cabinet's lamps and seat vibration through ONE byte it writes to the I/O MCU's shared RAM, 0xA00068
 * (dpram[0x34]); found with the game's own EFFECT TEST: 0x02 START lamp, 0x04 HIT lamp, 0x18 FLASH lamps, 0x20 VIBRATION,
 * 0x40 REAR lamp, 0x80 "2" lamp. On a PC:
 *   Lamp flashes  -> the picture flashes white (fading out); the hit lamp, a red glow round the picture's edges
 *   Vibration     -> every open game pad rumbles
 *   Mouse pointer -> hidden over the game: the game's own sight is the aim
 * and the look of the screen, any mix of:
 *   Screen shape  -> flat, curved out (a tube's bulge: corners in) or concave (the picture set back in a bowl: edges bow in)
 *   Screen filter -> CRT (scanlines + an RGB mask), plastic mirror (the cabinet's mirror: soft, a faint ghost), or both
 *   Glass glare   -> a soft reflection of a light on the glass
 * All drawn over the finished picture (ss22_host_game.overlay) with the fixed pipeline, like the rest of the host.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "eng_cfg.h"
#include "eng_ui.h"
#include "s21_board.h"
#include "s21_cabinet.h"

enum { R_POINTER, R_LAMPS, R_RUMBLE, R_SHAPE, R_STRENGTH, R_FILTER, R_GLARE, NROWS };
static const char *const keys[NROWS] = { "sb_hide_pointer", "sb_lamp_flashes", "sb_vibration", "sb_screen_shape", "sb_shape_strength",
                                         "sb_screen_filter", "sb_glass_glare" };
static const char *const labels[NROWS] = { "Mouse pointer", "Lamp flashes", "Vibration", "Screen shape", "Shape strength",
                                           "Screen filter", "Glass glare" };
static const int lo[NROWS] = { 0, 0, 0, 0, 1, 0, 0 }, hi[NROWS] = { 1, 1, 1, 2, 5, 3, 1 };
static int val[NROWS] = { 1, 1, 1, 0, 3, 0, 0 };   /* defaults: pointer hidden, lamps and vibration on, a flat clean screen */
static const char *const shapes[] = { "Flat", "Curved (bulges out)", "Concave (depth)" };
static const char *const filters[] = { "None", "CRT", "Plastic mirror", "CRT + mirror" };

void s21_cabinet_init(void)
{
    if (eng_cfg_int("sb_curved_screen", 0)) val[R_SHAPE] = 1;    /* the first version's on/off */
    for (int i = 0; i < NROWS; i++) { int v = eng_cfg_int(keys[i], val[i]); val[i] = v < lo[i] ? lo[i] : v > hi[i] ? hi[i] : v; }
}
bool s21_cabinet_hide_pointer(void) { return val[R_POINTER]; }

static int nrows(void) { return NROWS; }
static bool enabled(int row) { return row != R_STRENGTH || val[R_SHAPE]; }
static void text(int row, char *label, size_t ln, char *value, size_t vn)
{
    snprintf(label, ln, "%s", labels[row]);
    switch (row) {
    case R_POINTER:  snprintf(value, vn, "%s", val[row] ? "Hidden (the game's sight)" : "Shown"); break;
    case R_SHAPE:    snprintf(value, vn, "%s", shapes[val[row]]); break;
    case R_STRENGTH: snprintf(value, vn, "%d", val[row]); break;
    case R_FILTER:   snprintf(value, vn, "%s", filters[val[row]]); break;
    default:         snprintf(value, vn, "%s", val[row] ? "On" : "Off");
    }
}
static void change(int row, int dir)
{
    int v = val[row] + (dir < 0 ? -1 : 1);                       /* Enter / click steps forward, round */
    if (v > hi[row]) v = dir ? hi[row] : lo[row];
    if (v < lo[row]) v = lo[row];
    val[row] = v; eng_cfg_set_int(keys[row], v);
}
static void notes(void (*line)(const char *fmt, ...))
{
    line("Lamps and seat shaker as the game drives them: a white flash, a red glow when hit, pad rumble.");
    line("Shape, filter and glare mix freely: concave sets the picture back like the cabinet's mirror.");
}
static const eng_ui_page page = { "Cabinet", 640, 170, 26, nrows, NULL, enabled, text, change, notes, 0 };
const eng_ui_page *s21_cabinet_page(void) { return &page; }

static void rumble(bool go)
{
    static bool was;
    if (!go && !was) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController *c = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (c) SDL_GameControllerRumble(c, go ? 0x7000 : 0, go ? 0xC000 : 0, go ? 120 : 0);
    }
    was = go;
}

/* THE MESH: the picture's rectangle as a grid, bent by the shape; every layer (the picture, its ghost, the CRT mask) is drawn on it
 * so they all bend together. Texture coordinates: the picture's (u + du, v + dv, flipped: GL rows go up) or the mask's (u * us, v * vs). */
static int cx, cy, cw, ch;
static void mesh(bool pic, float du, float dv, float us, float vs, float r, float g, float b, float a, bool vignette)
{
    enum { GX = 32, GY = 24 };
    const int shape = val[R_SHAPE]; const float st = (float)val[R_STRENGTH];
    const float as = (float)cw / ch;
    for (int j = 0; j < GY; j++) {
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= GX; i++)
            for (int jj = j + 1; jj >= j; jj--) {
                const float u = (float)i / GX, v = (float)jj / GY, px = 2 * u - 1, py = 2 * v - 1;
                const float r2 = (px * px * as * as + py * py) / (as * as + 1);   /* 0 centre .. 1 corner */
                float s = 1;
                if (shape == 1) { const float k = 0.035f * st; s = (1 + k) / (1 + 2 * k * r2); }   /* bulge: corners pulled in */
                if (shape == 2) { const float k = 0.06f * st; s = 1 - k * (1 - r2); }             /* concave: edges' middles in, corners stay */
                const float sx = cx + cw * (0.5f + 0.5f * px * s), sy = cy + ch * (0.5f + 0.5f * py * s);
                float sh = 1;
                if (vignette && shape == 1) sh = 1.0f - 0.08f * st * r2 * r2;                       /* a tube: its edges darker */
                if (vignette && shape == 2) sh = 0.80f + 0.20f * (1 - 0.6f * r2) - 0.04f * st * (1 - r2);   /* a bowl: the far middle dimmer */
                glColor4f(r * sh, g * sh, b * sh, a);
                if (pic) glTexCoord2f(u + du, 1 - v - dv); else glTexCoord2f(u * us, v * vs);
                glVertex2f(sx, sy);
            }
        glEnd();
    }
}

static void quad(float x, float y, float w, float h) { glBegin(GL_QUADS); glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h); glEnd(); }

static void glow(float lv)
{
    const float b = (cw < ch ? cw : ch) * 0.14f, x = (float)cx, y = (float)cy, w = (float)cw, h = (float)ch;
    glBegin(GL_QUADS);
    #define V(px, py, al) glColor4f(1, 0.08f, 0.05f, (al)); glVertex2f((float)(px), (float)(py))
    V(x, y, lv); V(x + w, y, lv); V(x + w - b, y + b, 0); V(x + b, y + b, 0);                         /* top */
    V(x + b, y + h - b, 0); V(x + w - b, y + h - b, 0); V(x + w, y + h, lv); V(x, y + h, lv);         /* bottom */
    V(x, y, lv); V(x + b, y + b, 0); V(x + b, y + h - b, 0); V(x, y + h, lv);                         /* left */
    V(x + w - b, y + b, 0); V(x + w, y, lv); V(x + w, y + h, lv); V(x + w - b, y + h - b, 0);         /* right */
    #undef V
    glEnd();
}

static void glare(void)
{
    const float gx = cx + cw * 0.27f, gy = cy + ch * 0.20f, rx = cw * 0.42f, ry = ch * 0.30f;
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1, 1, 1, 0.10f); glVertex2f(gx, gy);
    glColor4f(1, 1, 1, 0);
    for (int i = 0; i <= 32; i++) { const float t = i * 6.2831853f / 32; glVertex2f(gx + rx * cosf(t), gy + ry * sinf(t)); }
    glEnd();
}

static GLuint pic_tex(int dh)                  /* the finished picture, copied out of the window */
{
    static GLuint tex; static int tw, th;
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (tw != cw || th != ch) { glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cw, ch, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL); tw = cw; th = ch; }   /* (engine/gl_dyn.c has no glCopyTexImage2D) */
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cx, dh - (cy + ch), cw, ch);
    return tex;
}

static GLuint mask_tex(void)                   /* the CRT: one RGB triad across, one scanline down (lit middle, dark gap) */
{
    static GLuint tex;
    if (tex) return tex;
    static const float prof[4] = { 0.55f, 0.92f, 1.0f, 0.92f };
    uint8_t px[4][3][4];
    for (int yy = 0; yy < 4; yy++)
        for (int xx = 0; xx < 3; xx++)
            for (int c = 0; c < 3; c++) { px[yy][xx][c] = (uint8_t)(255 * prof[yy] * (c == xx ? 1.0f : 0.72f)); px[yy][xx][3] = 255; }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

static float flash, hit;
void s21_cabinet_overlay(int x, int y, int w, int h)
{
    static int fxt = -1; if (fxt < 0) { const char *e = getenv("SB_FXTEST"); fxt = e ? (int)strtol(e, NULL, 16) : 0; }   /* tests: those bits forced on */
    const uint8_t fx = g_s21.dpram[0x34] | (uint8_t)fxt;
    flash = (val[R_LAMPS] && (fx & 0x18)) ? 1.0f : flash * 0.75f;
    hit = (val[R_LAMPS] && (fx & 0x04)) ? 1.0f : hit * 0.85f;
    rumble(val[R_RUMBLE] && (fx & 0x20) && !eng_ui_is_open());
    const bool look = val[R_SHAPE] || val[R_FILTER];
    if (!look && !val[R_GLARE] && flash < 0.02f && hit < 0.02f) return;
    cx = x; cy = y; cw = w; ch = h;
    int dw, dh; SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &dw, &dh);
    glViewport(0, 0, dw, dh);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, dw, dh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (look) {
        const GLuint pt = pic_tex(dh);
        const bool crt = val[R_FILTER] & 1, mirror = val[R_FILTER] & 2;
        glColor4f(0, 0, 0, 1); quad((float)x, (float)y, (float)w, (float)h);   /* the flat picture away */
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, pt);
        mesh(true, 0, 0, 0, 0, 1, 1, 1, 1, true);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (mirror) {                          /* the plastic mirror: soft (the picture again a pixel or two off), a faint ghost lower right */
            const float px = 1.5f / w, py = 1.5f / h;
            mesh(true, px, py, 0, 0, 1, 1, 1, 0.30f, true);
            mesh(true, -px, -py, 0, 0, 1, 1, 1, 0.25f, true);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            mesh(true, -6.0f / w, -7.0f / h, 0, 0, 0.75f, 0.85f, 1.0f, 0.14f, true);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        if (crt) {                             /* the mask multiplied in, then some of the picture added back: the tube's glow */
            glBindTexture(GL_TEXTURE_2D, mask_tex());
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            mesh(false, 0, 0, w / 3.0f, 240.0f, 1, 1, 1, 1, false);
            glBindTexture(GL_TEXTURE_2D, pt);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            mesh(true, 0, 0, 0, 0, 1, 1, 1, 0.22f, true);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        glDisable(GL_TEXTURE_2D);
    }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (flash >= 0.02f) { glColor4f(1, 1, 1, 0.30f * flash); quad((float)x, (float)y, (float)w, (float)h); }
    if (hit >= 0.02f) glow(0.55f * hit);
    if (val[R_GLARE]) glare();
    glDisable(GL_BLEND); glColor4f(1, 1, 1, 1);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}
