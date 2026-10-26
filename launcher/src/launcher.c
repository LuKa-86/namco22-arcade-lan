/*
 * launcher.c -- "Namco 22 Arcade LAN.exe": the package's front end (Windows).
 *
 * Full screen, like a cabinet's own menu: the games down the left, the chosen one's picture and how to play it on the right.
 * Keyboard (arrows, Enter, Esc), a game controller (D-pad / stick, A, B) or the mouse. The package beside it:
 *
 *   Namco 22 Arcade LAN.exe       this program, and its settings: launcher.cfg
 *   games\<Game>\                  each game's program (<Name>-ArcadeLAN.exe), its own roms\ folder and settings (link.cfg)
 *   tools\                         the helpers (the Rave Racer 4-cabinet check)
 *   advanced\                      one .bat per way to play (written by --write-advanced): the same as this menu's buttons
 *
 * SET UP AND FORGET: Settings > "Start a game when Windows starts" picks a game and a way to play it. Windows then starts this
 * program with --auto at sign-in (HKCU ...\Run), it counts down (any key or button gives the menu instead) and starts the
 * game; if the game ever closes, it is started again after a few seconds (Settings > "If the game closes"). A game that
 * restarts itself (the link's safety net) is noticed by its program path, so it is never started twice.
 *
 *   --auto                 the start-up countdown, as Windows runs it
 *   --window               in a window instead of full screen (also Settings > Launcher screen)
 *   --write-advanced DIR   write the .bat launchers into DIR and exit
 *   --list                 print every game and way to play, and exit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_GL2_IMPLEMENTATION
#include "../../third_party/nuklear.h"
#include "../../third_party/nuklear_sdl_gl2.h"

#define APP_NAME   "Namco 22 Arcade LAN"
#define RUN_VALUE  L"Namco 22 Arcade LAN"         /* the HKCU Run value's name */

/* ---- THE GAMES AND THE WAYS TO PLAY THEM ------------------------------------------------------------------------------
 * A way to play starts one or more copies of the game (copies on one PC: "try" and party modes), in the game's folder.
 * Per copy: the command line after the program, and environment settings ("NAME=value;NAME=value"). */
#define MAXCOPY 8
typedef struct { const char *args, *env; } copy_t;
typedef struct {
    const char *label, *hint;
    bool auto_ok;                       /* offered for the automatic start (a cabinet's normal way to run) */
    int n; copy_t c[MAXCOPY];
} way_t;
typedef struct {
    const char *id, *name, *year, *folder, *exe, *line1, *line2, *cfg;   /* cfg: its link settings file (NULL = none) */
    int nmodes; way_t m[12];
} game_t;

#define NET_MODES(roms) \
    { "Cabinet on the network - FULL SCREEN", "This PC is one cabinet of a linked set (recommended)", true, 1, { { roms " --fullscreen", NULL } } }, \
    { "Cabinet on the network - window", "The same in a window: fine for two cabinets", false, 1, { { roms " --window", NULL } } }, \
    { "Try 2 linked cabinets on this PC", "Two windows; click one to play it", false, 2, { { roms " --window 1 --link 1 --link-cabinets 2", NULL }, \
        { roms " --window 1 --link 2 --link-cabinets 2 --mute", NULL } } }, \
    { "Try 4 linked cabinets on this PC", "Four windows; only cabinet 1 has sound", false, 4, { { roms " --window 1 --link 1", NULL }, \
        { roms " --window 1 --link 2 --mute", NULL }, { roms " --window 1 --link 3 --mute", NULL }, { roms " --window 1 --link 4 --mute", NULL } } }, \
    { "PARTY: 2 players - keyboard + 1 controller", "Split screen on this PC; plug the controller in first", false, 2, { \
        { roms " --window 1 --link 2 --link-cabinets 2 --party --pad 1 --mute", NULL }, { roms " --window 1 --link 1 --link-cabinets 2 --party --pad 0", NULL } } }, \
    { "PARTY: 2 players - 2 controllers", "Split screen on this PC; plug the controllers in first", false, 2, { \
        { roms " --window 1 --link 2 --link-cabinets 2 --party --pad 2 --mute", NULL }, { roms " --window 1 --link 1 --link-cabinets 2 --party --pad 1", NULL } } }, \
    { "PARTY: 4 players - keyboard + 3 controllers", "Split screen on this PC; plug the controllers in first", false, 4, { \
        { roms " --window 1 --link 2 --link-cabinets 4 --party --pad 1 --mute", NULL }, { roms " --window 1 --link 3 --link-cabinets 4 --party --pad 2 --mute", NULL }, \
        { roms " --window 1 --link 4 --link-cabinets 4 --party --pad 3 --mute", NULL }, { roms " --window 1 --link 1 --link-cabinets 4 --party --pad 0", NULL } } }, \
    { "PARTY: 4 players - 4 controllers", "Split screen on this PC; plug the controllers in first", false, 4, { \
        { roms " --window 1 --link 2 --link-cabinets 4 --party --pad 2 --mute", NULL }, { roms " --window 1 --link 3 --link-cabinets 4 --party --pad 3 --mute", NULL }, \
        { roms " --window 1 --link 4 --link-cabinets 4 --party --pad 4 --mute", NULL }, { roms " --window 1 --link 1 --link-cabinets 4 --party --pad 1", NULL } } }

static const game_t games[] = {
    { "tokyowar", "Tokyo Wars", "1996", "Tokyo Wars", "TokyoWars-ArcadeLAN.exe",
      "Tank battles, two teams, up to four linked cabinets.", "Keys: 5 coin, 1 start, arrows drive, Ctrl fire, Esc settings.", "link.cfg",
      8, { NET_MODES("roms") } },
    { "raverace", "Rave Racer", "1995", "Rave Racer", "RaveRacer-ArcadeLAN.exe",
      "Racing for up to four linked cabinets.", "Keys: 5 coin, 1 start, arrows steer, A / Z gas and brake, Esc settings.", "link.cfg",
      8, { NET_MODES("roms") } },
    { "acedrive", "Ace Driver", "1994", "Ace Driver", "AceDriver-ArcadeLAN.exe",
      "Formula racing for up to eight linked cabinets.", "Keys: 5 coin (two a credit), arrows steer, A / Z gas and brake, Esc settings.", "link.cfg",
      9, { NET_MODES("roms"),
        { "Try 8 linked cabinets on this PC", "Eight small windows in a grid; only cabinet 1 has sound (needs a strong PC)", false, 8, {
            { "roms --window 1 --link 1 --link-cabinets 8", NULL }, { "roms --window 1 --link 2 --link-cabinets 8 --mute", NULL },
            { "roms --window 1 --link 3 --link-cabinets 8 --mute", NULL }, { "roms --window 1 --link 4 --link-cabinets 8 --mute", NULL },
            { "roms --window 1 --link 5 --link-cabinets 8 --mute", NULL }, { "roms --window 1 --link 6 --link-cabinets 8 --mute", NULL },
            { "roms --window 1 --link 7 --link-cabinets 8 --mute", NULL }, { "roms --window 1 --link 8 --link-cabinets 8 --mute", NULL } } } } },
    { "timecrs2", "Time Crisis II", "1997", "Time Crisis II", "TimeCrisis2-ArcadeLAN.exe",
      "The twin light-gun cabinet: the left and right sides link for two players.", "Mouse or light gun to shoot, the pedal to take cover; Esc settings.",
      "tc2_link.cfg",
      6, {
        { "LEFT cabinet (red player) - FULL SCREEN", "This PC is the left side of the twin cabinet (recommended)", true, 1, { { "--window --fullscreen", "TC2_SIDE=left" } } },
        { "RIGHT cabinet (blue player) - FULL SCREEN", "This PC is the right side of the twin cabinet (recommended)", true, 1, { { "--window --fullscreen", "TC2_SIDE=right" } } },
        { "BOTH screens on ONE PC (2 monitors, 2 guns)", "One PC runs the whole twin cabinet; the first gun to fire is the left", true, 2, {
            { "--window", "TC2_OWN_GUN=1;TC2_LINK=L:23021:127.0.0.1:23022;TC2_CABINET_DIR=screen-left;TC2_MONITOR=1" },
            { "--window", "TC2_OWN_GUN=1;TC2_LINK=R:23022:127.0.0.1:23021;TC2_CABINET_DIR=screen-right;TC2_MONITOR=2" } } },
        { "LEFT cabinet (red player) - window", "The left side, in a window", false, 1, { { "--window", "TC2_SIDE=left" } } },
        { "RIGHT cabinet (blue player) - window", "The right side, in a window", false, 1, { { "--window", "TC2_SIDE=right" } } },
        { "Try both cabinets on this PC", "Two linked windows, left and right", false, 2, {
            { "--window", "TC2_LINK=L:23011:127.0.0.1:23012;TC2_CABINET_DIR=try-left;TC2_WINPOS=left" },
            { "--window", "TC2_LINK=R:23012:127.0.0.1:23011;TC2_CABINET_DIR=try-right;TC2_WINPOS=right" } } } } },
    { "cybrcycc", "Cyber Cycles", "1995", "Cyber Cycles", "CyberCycles-ArcadeLAN.exe",
      "Motorbike racing for up to four linked cabinets. New in this package.", "Keys: 5 coin, Enter start / view, arrows bank, Up throttle, Down brake.", "link.cfg",
      8, { NET_MODES("roms") } },
    { "cybrcomm", "Cyber Commando", "1994", "Cyber Commando", "CyberCommando-ArcadeLAN.exe",
      "Robot battles: two linked cabinets fight each other (the challenge mode). New in this package.",
      "Keys: 5 coin (two a credit), arrows move, Z fire, X missile, C view, Esc settings.", "link.cfg",
      5, {
        { "Cabinet on the network - FULL SCREEN", "This PC is one of the two linked cabinets (recommended)", true, 1, { { "roms --fullscreen", NULL } } },
        { "Cabinet on the network - window", "The same in a window", false, 1, { { "roms --window", NULL } } },
        { "Try 2 linked cabinets on this PC", "Two windows; click one to play it", false, 2, { { "roms --window 1 --link 1 --link-cabinets 2", NULL },
            { "roms --window 1 --link 2 --link-cabinets 2 --mute", NULL } } },
        { "PARTY: 2 players - keyboard + 1 controller", "Split screen on this PC; plug the controller in first", false, 2, {
            { "roms --window 1 --link 2 --link-cabinets 2 --party --pad 1 --mute", NULL }, { "roms --window 1 --link 1 --link-cabinets 2 --party --pad 0", NULL } } },
        { "PARTY: 2 players - 2 controllers", "Split screen on this PC; plug the controllers in first", false, 2, {
            { "roms --window 1 --link 2 --link-cabinets 2 --party --pad 2 --mute", NULL }, { "roms --window 1 --link 1 --link-cabinets 2 --party --pad 1", NULL } } } } },
    { "adillor", "Armadillo Racing", "1997", "Armadillo Racing", "ArmadilloRacing-ArcadeLAN.exe",
      "Trackball racing for up to four linked cabinets (red, blue, yellow, green). New in this package.",
      "Keys: 5 coin, 1 start; the mouse / a trackball rolls (or the arrows); Esc settings.", "link.cfg",
      8, { NET_MODES("roms") } },
    { "alpiner", "Alpine Racer", "1995", "Alpine Racer", "AlpineRacer-ArcadeLAN.exe",
      "Downhill skiing, one cabinet. New in this package.", "Keys: 5 coin, Enter decide / view, Z and X select, arrows ski.", NULL,
      2, {
        { "FULL SCREEN", "The cabinet's normal way to run (recommended)", true, 1, { { "roms --fullscreen", NULL } } },
        { "Window", "In a window", false, 1, { { "roms --window 2", NULL } } } } },
    { "cybsled", "Cyber Sled", "1993", "Cyber Sled", "CyberSled-ArcadeLAN.exe",
      "Twin-stick tank battles. The original author's build: its two players play ONLINE (internet), not over the cabinet LAN.",
      "Keys: 5 coin, 1 start, arrows drive (Shift strafe), Ctrl fire, Esc settings.", NULL,
      2, {
        { "FULL SCREEN", "The cabinet's normal way to run (recommended)", true, 1, { { "extracted --fullscreen", NULL } } },
        { "Window", "In a window", false, 1, { { "extracted --window", NULL } } } } },
};
#define NGAMES ((int)(sizeof games / sizeof *games))

/* ---- PATHS AND THE SETTINGS FILE ------------------------------------------------------------------------------------ */
static char base[MAX_PATH];                     /* the package folder (this program's), with a trailing backslash */
static FILE *log_file;
static void logm(const char *fmt, ...)
{
    if (!log_file) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(log_file, "%04d-%02d-%02d %02d:%02d:%02d  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(log_file, fmt, a); va_end(a);
    fputc('\n', log_file); fflush(log_file);
}
static void game_dir(const game_t *g, char *out, size_t n) { snprintf(out, n, "%sgames\\%s", base, g->folder); }
static void game_exe(const game_t *g, char *out, size_t n) { snprintf(out, n, "%sgames\\%s\\%s", base, g->folder, g->exe); }

static struct { int auto_game, auto_mode, delay, restart, windowed; } S = { -1, 0, 10, 1, 0 };
static const int delays[] = { 5, 10, 20, 30, 60 };

static void cfg_load(void)
{
    char p[MAX_PATH]; snprintf(p, sizeof p, "%slauncher.cfg", base);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char k[64], v[128];
        if (line[0] == '#' || sscanf(line, " %63[^= ] = %127[^\r\n]", k, v) != 2) continue;
        if (!strcmp(k, "auto_start")) {
            S.auto_game = -1;
            char gid[64]; int m = 0;
            if (sscanf(v, "%63[^:]:%d", gid, &m) == 2)
                for (int i = 0; i < NGAMES; i++) if (!strcmp(games[i].id, gid) && m >= 0 && m < games[i].nmodes) { S.auto_game = i; S.auto_mode = m; }
        }
        else if (!strcmp(k, "auto_delay")) S.delay = atoi(v);
        else if (!strcmp(k, "if_closed")) S.restart = strcmp(v, "menu") != 0;
        else if (!strcmp(k, "screen")) S.windowed = !strcmp(v, "window");
    }
    fclose(f);
    if (S.delay < 1) S.delay = 10;
}
static void cfg_save(void)
{
    char p[MAX_PATH]; snprintf(p, sizeof p, "%slauncher.cfg", base);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "# " APP_NAME " launcher settings (its Settings page edits this file)\n");
    fprintf(f, "# auto_start: the game and way to play started when Windows starts (<game>:<way>, see --list), or off\n");
    if (S.auto_game >= 0) fprintf(f, "auto_start=%s:%d\n", games[S.auto_game].id, S.auto_mode); else fprintf(f, "auto_start=off\n");
    fprintf(f, "# auto_delay: seconds of countdown first (a key or button gives the menu instead)\nauto_delay=%d\n", S.delay);
    fprintf(f, "# if_closed: restart = start the game again when it closes (automatic start), menu = come back to this menu\nif_closed=%s\n",
            S.restart ? "restart" : "menu");
    fprintf(f, "screen=%s\n", S.windowed ? "window" : "fullscreen");
    fclose(f);
}

/* Windows starts the launcher at sign-in when auto start is on: HKCU\Software\Microsoft\Windows\CurrentVersion\Run */
static bool run_key_set(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    LONG r;
    if (on) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        swprintf(cmd, MAX_PATH + 32, L"\"%ls\" --auto", exe);
        r = RegSetValueExW(k, RUN_VALUE, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, RUN_VALUE);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    logm("Windows start-up entry %s: %s", on ? "set" : "removed", r == ERROR_SUCCESS ? "ok" : "FAILED");
    return r == ERROR_SUCCESS;
}

/* ---- THE GAMES' OWN SETTINGS FILES: one key across every game that has it (link lights, session recordings) ---------- */
static bool cfg_get(const char *path, const char *key, char *val, size_t n)
{
    FILE *f = fopen(path, "r"); if (!f) return false;
    char line[256]; bool found = false;
    while (fgets(line, sizeof line, f)) {
        char k[64], v[128];
        if (line[0] != '#' && sscanf(line, " %63[^= ] = %127s", k, v) == 2 && !strcmp(k, key)) { snprintf(val, n, "%s", v); found = true; }
    }
    fclose(f);
    return found;
}
static void cfg_set(const char *path, const char *key, const char *val)
{
    char buf[16384]; size_t len = 0; buf[0] = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char k[64];
            if (line[0] != '#' && sscanf(line, " %63[^= ]", k) == 1 && !strcmp(k, key)) continue;   /* the old line goes */
            size_t l = strlen(line);
            if (len + l + 2 < sizeof buf) { memcpy(buf + len, line, l); len += l; if (line[l - 1] != '\n') buf[len++] = '\n'; buf[len] = 0; }
        }
        fclose(f);
    }
    f = fopen(path, "w"); if (!f) return;
    fputs(buf, f); fprintf(f, "%s=%s\n", key, val); fclose(f);
}
static void cfg_path(const game_t *g, char *out, size_t n) { snprintf(out, n, "%sgames\\%s\\%s", base, g->folder, g->cfg); }
/* lights: "off" or a corner (default bottom-left); lights_was remembers the corner while off. record: on (default) / off */
static bool all_on(const char *key, const char *dflt)
{
    for (int i = 0; i < NGAMES; i++) {
        if (!games[i].cfg) continue;
        char p[MAX_PATH], v[64]; cfg_path(&games[i], p, sizeof p);
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) continue;
        if (!cfg_get(p, key, v, sizeof v)) snprintf(v, sizeof v, "%s", dflt);
        if (strcmp(v, "off")) return true;
    }
    return false;
}
static void all_set(const char *key, bool on)
{
    for (int i = 0; i < NGAMES; i++) {
        if (!games[i].cfg) continue;
        char p[MAX_PATH]; cfg_path(&games[i], p, sizeof p);
        if (!strcmp(key, "lights")) {
            char was[64] = "bottom-left", now[64] = "bottom-left";
            cfg_get(p, "lights", now, sizeof now); cfg_get(p, "lights_was", was, sizeof was);
            if (on) { cfg_set(p, "lights", strcmp(was, "off") ? was : "bottom-left"); }
            else { if (strcmp(now, "off")) cfg_set(p, "lights_was", now); cfg_set(p, "lights", "off"); }
        } else cfg_set(p, key, on ? "on" : "off");
    }
    logm("%s %s for every game", key, on ? "on" : "off");
}

/* ---- STARTING A GAME, AND WATCHING IT -------------------------------------------------------------------------------- */
static HANDLE procs[MAXCOPY]; static int nprocs;
static int run_game = -1, run_mode;
static bool launch(int gi, int mi)
{
    const game_t *g = &games[gi]; const way_t *m = &g->m[mi];
    char dir[MAX_PATH], exe[MAX_PATH];
    game_dir(g, dir, sizeof dir); game_exe(g, exe, sizeof exe);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) { logm("cannot start %s: %s is missing", g->name, exe); return false; }
    nprocs = 0;
    for (int c = 0; c < m->n; c++) {
        char cmd[1024]; snprintf(cmd, sizeof cmd, "\"%s\" %s", exe, m->c[c].args);
        char envs[512] = "";                       /* the copy's own settings: set for the child, cleared again after */
        if (m->c[c].env) snprintf(envs, sizeof envs, "%s", m->c[c].env);
        for (char *t = strtok(envs, ";"); t; t = strtok(NULL, ";")) { char *eq = strchr(t, '='); if (eq) { *eq = 0; SetEnvironmentVariableA(t, eq + 1); *eq = '='; } }
        STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi;
        BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi);
        if (m->c[c].env) { snprintf(envs, sizeof envs, "%s", m->c[c].env);
            for (char *t = strtok(envs, ";"); t; t = strtok(NULL, ";")) { char *eq = strchr(t, '='); if (eq) { *eq = 0; SetEnvironmentVariableA(t, NULL); } } }
        if (!ok) { logm("cannot start %s (error %lu)", cmd, GetLastError()); continue; }
        CloseHandle(pi.hThread); procs[nprocs++] = pi.hProcess;
        logm("started %s - %s: %s", g->name, m->label, cmd);
        if (c + 1 < m->n) Sleep(300);
    }
    if (!nprocs) return false;
    run_game = gi; run_mode = mi;
    return true;
}
/* is any copy of this game's program running (ours, or one the game started itself: the link's safety net restarts it)? */
static bool game_running(int gi)
{
    char want[MAX_PATH]; game_exe(&games[gi], want, sizeof want);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe = { sizeof pe }; bool found = false;
    for (BOOL ok = Process32First(snap, &pe); ok && !found; ok = Process32Next(snap, &pe)) {
        if (_stricmp(pe.szExeFile, games[gi].exe)) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        char path[MAX_PATH]; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameA(h, 0, path, &n) && !_stricmp(path, want)) found = true;
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}
static bool ours_running(void)
{
    for (int i = 0; i < nprocs; i++) if (WaitForSingleObject(procs[i], 0) == WAIT_TIMEOUT) return true;
    return false;
}
static void forget_procs(void) { for (int i = 0; i < nprocs; i++) CloseHandle(procs[i]); nprocs = 0; }

/* ---- WINDOWS: write the .bat launchers, this PC's addresses ---------------------------------------------------------- */
static void write_advanced(const char *dir)
{
    CreateDirectoryA(dir, NULL);
    for (int gi = 0; gi < NGAMES; gi++) {
        const game_t *g = &games[gi];
        for (int mi = 0; mi < g->nmodes; mi++) {
            const way_t *m = &g->m[mi];
            char lab[128]; int n = 0;                     /* a file name: "PARTY: 2 players" -> "PARTY - 2 players" */
            for (const char *c = m->label; *c && n < (int)sizeof lab - 3; c++) {
                if (*c == ':') { lab[n++] = ' '; lab[n++] = '-'; }
                else if (!strchr("\\/*?\"<>|", *c)) lab[n++] = *c;
            }
            lab[n] = 0;
            char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\%s - %s.bat", dir, g->name, lab);
            FILE *f = fopen(p, "wb"); if (!f) continue;
            fprintf(f, "@echo off\r\nrem %s - %s: %s.\r\nrem (written by \"" APP_NAME ".exe\" --write-advanced: the same as its menu's button)\r\n",
                    g->name, m->label, m->hint);
            fprintf(f, "setlocal\r\ncd /d \"%%~dp0..\\games\\%s\"\r\n", g->folder);
            for (int c = 0; c < m->n; c++) {
                if (m->c[c].env) { char e[512]; snprintf(e, sizeof e, "%s", m->c[c].env);
                    for (char *t = strtok(e, ";"); t; t = strtok(NULL, ";")) fprintf(f, "set \"%s\"\r\n", t); }
                fprintf(f, "start \"\" \"%s\" %s\r\n", g->exe, m->c[c].args);
                if (c + 1 < m->n) fprintf(f, "ping -n 1 -w 300 127.0.0.1 >nul\r\n");
            }
            fclose(f);
        }
    }
}
static int my_addresses(char out[][48], int max)
{
    ULONG sz = 16384; IP_ADAPTER_ADDRESSES *aa = malloc(sz); int n = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &sz) == ERROR_BUFFER_OVERFLOW) {
        free(aa); aa = malloc(sz);
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &sz) != NO_ERROR) { free(aa); return 0; }
    }
    for (IP_ADAPTER_ADDRESSES *a = aa; a && n < max; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u && n < max; u = u->Next) {
            const unsigned char *b = (const unsigned char *)&((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr;
            if (b[0] == 169 && b[1] == 254) continue;                    /* Windows' own address for an adapter with no network: not a LAN */
            char name[32]; WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof name, NULL, NULL);
            snprintf(out[n++], 48, "%u.%u.%u.%u  (%.24s)", b[0], b[1], b[2], b[3], name);
        }
    }
    free(aa);
    return n;
}

/* ---- DRAWING ----------------------------------------------------------------------------------------------------------- */
static struct nk_context *ctx;
static struct nk_font *f_title, *f_big, *f_mid, *f_small;
static GLuint pics[NGAMES]; static int pic_w[NGAMES], pic_h[NGAMES];
static const struct nk_color C_BG = { 14, 14, 18, 255 }, C_PANEL = { 26, 26, 32, 255 }, C_TEXT = { 235, 235, 235, 255 },
                             C_DIM = { 150, 150, 160, 255 }, C_AMBER = { 255, 176, 0, 255 }, C_DARK = { 12, 12, 14, 255 },
                             C_SEL = { 58, 46, 14, 255 };

static void load_pictures(void)
{
    for (int i = 0; i < NGAMES; i++) {
        char p[MAX_PATH]; snprintf(p, sizeof p, "%sgames\\%s\\picture.bmp", base, games[i].folder);
        SDL_Surface *s = SDL_LoadBMP(p); if (!s) continue;
        SDL_Surface *r = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0); SDL_FreeSurface(s); if (!r) continue;
        glGenTextures(1, &pics[i]); glBindTexture(GL_TEXTURE_2D, pics[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, r->pitch / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r->w, r->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, r->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        pic_w[i] = r->w; pic_h[i] = r->h; SDL_FreeSurface(r);
    }
}
static void fonts(float h)
{
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    const char *reg = "C:\\Windows\\Fonts\\segoeui.ttf", *bold = "C:\\Windows\\Fonts\\segoeuib.ttf";
    bool have = GetFileAttributesA(reg) != INVALID_FILE_ATTRIBUTES && GetFileAttributesA(bold) != INVALID_FILE_ATTRIBUTES;
    const float k = h / 1080.0f;
    if (have) {
        f_title = nk_font_atlas_add_from_file(atlas, bold, 54 * k, NULL);
        f_big = nk_font_atlas_add_from_file(atlas, bold, 34 * k, NULL);
        f_mid = nk_font_atlas_add_from_file(atlas, reg, 26 * k, NULL);
        f_small = nk_font_atlas_add_from_file(atlas, reg, 21 * k, NULL);
    } else {
        f_title = nk_font_atlas_add_default(atlas, 54 * k, NULL); f_big = nk_font_atlas_add_default(atlas, 34 * k, NULL);
        f_mid = nk_font_atlas_add_default(atlas, 26 * k, NULL); f_small = nk_font_atlas_add_default(atlas, 21 * k, NULL);
    }
    nk_sdl_font_stash_end();
    nk_style_set_font(ctx, &f_mid->handle);
}
static float tw(struct nk_font *f, const char *s) { return f->handle.width(f->handle.userdata, f->handle.height, s, (int)strlen(s)); }
static void text(struct nk_command_buffer *cb, struct nk_font *f, float x, float y, const char *s, struct nk_color c)
{
    nk_draw_text(cb, nk_rect(x, y, tw(f, s) + 4, f->handle.height + 4), s, (int)strlen(s), &f->handle, nk_rgba(0, 0, 0, 0), c);
}
static void text_c(struct nk_command_buffer *cb, struct nk_font *f, float cx, float y, const char *s, struct nk_color c) { text(cb, f, cx - tw(f, s) / 2, y, s, c); }

/* ---- THE SCREENS ------------------------------------------------------------------------------------------------------ */
enum { SC_MENU, SC_SETTINGS, SC_COUNTDOWN, SC_RUNNING, SC_RESTART, SC_QUIT };
static int screen = SC_MENU, sel_game, sel_mode, focus;     /* focus 0: the games, 1: the ways to play */
static int sel_set; static Uint64 count_until; static char message[160]; static Uint64 message_until;
static bool auto_session;                                   /* started by Windows (--auto): the watchdog's restart applies */
static bool want_quit;
enum { SET_AUTO, SET_DELAY, SET_CLOSED, SET_SCREEN, SET_LIGHTS, SET_RECORD, SET_RRCHECK, SET_ADDR, SET_BACK, SET_N };

static void say(const char *m) { snprintf(message, sizeof message, "%s", m); message_until = SDL_GetTicks64() + 4000; }

/* the auto start choices: every way to play marked auto_ok, in order */
static int auto_next(int dir)
{
    int list[64][2], n = 0, cur = -1;
    list[n][0] = -1; list[n][1] = 0; n++;
    for (int g = 0; g < NGAMES; g++) for (int m = 0; m < games[g].nmodes; m++) if (games[g].m[m].auto_ok && n < 64) { list[n][0] = g; list[n][1] = m; n++; }
    for (int i = 0; i < n; i++) if (list[i][0] == S.auto_game && (S.auto_game < 0 || list[i][1] == S.auto_mode)) cur = i;
    if (cur < 0) cur = 0;
    cur = (cur + dir + n) % n;
    S.auto_game = list[cur][0]; S.auto_mode = list[cur][1];
    return cur;
}

static void start_mode(int g, int m, bool from_auto)
{
    if (!launch(g, m)) { char b[160]; snprintf(b, sizeof b, "%s could not start: is games\\%s\\%s there?", games[g].name, games[g].folder, games[g].exe); say(b); screen = SC_MENU; return; }
    auto_session = from_auto;
    screen = SC_RUNNING;
}

static void act(int key, SDL_Window *win)        /* key: SDLK_UP/DOWN/LEFT/RIGHT/RETURN/ESCAPE (pads are mapped to these) */
{
    if (screen == SC_COUNTDOWN || screen == SC_RESTART) { screen = SC_MENU; logm("countdown stopped: the menu"); return; }
    if (screen == SC_MENU) {
        const game_t *g = &games[sel_game];
        if (focus == 0) {
            if (key == SDLK_UP) { sel_game = (sel_game + NGAMES - 1) % NGAMES; sel_mode = 0; }
            else if (key == SDLK_DOWN) { sel_game = (sel_game + 1) % NGAMES; sel_mode = 0; }
            else if (key == SDLK_RIGHT || key == SDLK_RETURN) { focus = 1; sel_mode = 0; }
            else if (key == SDLK_ESCAPE) screen = SC_QUIT;
        } else {
            if (key == SDLK_UP) sel_mode = (sel_mode + g->nmodes - 1) % g->nmodes;
            else if (key == SDLK_DOWN) sel_mode = (sel_mode + 1) % g->nmodes;
            else if (key == SDLK_LEFT || key == SDLK_ESCAPE) focus = 0;
            else if (key == SDLK_RETURN) start_mode(sel_game, sel_mode, false);
        }
    } else if (screen == SC_SETTINGS) {
        const int d = key == SDLK_LEFT ? -1 : 1;
        if (key == SDLK_UP) sel_set = (sel_set + SET_N - 1) % SET_N;
        else if (key == SDLK_DOWN) sel_set = (sel_set + 1) % SET_N;
        else if (key == SDLK_ESCAPE) screen = SC_MENU;
        else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_RETURN) {
            switch (sel_set) {
            case SET_AUTO: auto_next(d); cfg_save(); run_key_set(S.auto_game >= 0); break;
            case SET_DELAY: { int i = 0; while (i < 4 && delays[i] != S.delay) i++; i = (i + d + 5) % 5; S.delay = delays[i]; cfg_save(); break; }
            case SET_CLOSED: S.restart = !S.restart; cfg_save(); break;
            case SET_SCREEN: S.windowed = !S.windowed; cfg_save();
                SDL_SetWindowFullscreen(win, S.windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); break;
            case SET_LIGHTS: all_set("lights", !all_on("lights", "bottom-left")); break;
            case SET_RECORD: all_set("record", !all_on("record", "on")); break;
            case SET_RRCHECK: if (key == SDLK_RETURN) {
                char cmd[MAX_PATH * 2]; snprintf(cmd, sizeof cmd, "powershell -NoProfile -ExecutionPolicy Bypass -File \"%stools\\rr-4-cabinet-check.ps1\"", base);
                STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi;
                if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, base, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); say("The Rave Racer check runs in its own window (about 2 minutes)."); }
                } break;
            case SET_BACK: if (key == SDLK_RETURN) screen = SC_MENU; break;
            }
        }
    }
}

/* one frame of the current screen; mouse clicks act directly */
static void draw(int W, int H, SDL_Window *win)
{
    const float k = H / 1080.0f, pad = 40 * k;
    if (!nk_begin(ctx, "launcher", nk_rect(0, 0, (float)W, (float)H), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) { nk_end(ctx); return; }
    struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
    const struct nk_input *in = &ctx->input;
    nk_fill_rect(cb, nk_rect(0, 0, (float)W, (float)H), 0, C_BG);
    /* the title bar */
    nk_fill_rect(cb, nk_rect(0, 0, (float)W, 120 * k), 0, C_DARK);
    text(cb, f_title, pad, 26 * k, "NAMCO 22  ARCADE LAN", C_TEXT);
    text(cb, f_small, pad + tw(f_title, "NAMCO 22  ARCADE LAN") + 30 * k, 58 * k, "Real cabinets, a PC in each, linked over a wired network", C_AMBER);
    const float top = 150 * k, bottom = H - 90 * k;

    if (screen == SC_MENU) {
        /* the games */
        const float lw = W * 0.30f, rowh = 92 * k;
        for (int i = 0; i < NGAMES; i++) {
            struct nk_rect r = nk_rect(pad, top + i * (rowh + 10 * k), lw - pad, rowh);
            if (nk_input_is_mouse_hovering_rect(in, r) && in->mouse.delta.x + in->mouse.delta.y != 0) { if (sel_game != i) sel_mode = 0; sel_game = i; focus = 0; }
            if (nk_input_mouse_clicked(in, NK_BUTTON_LEFT, r)) { sel_game = i; focus = 1; sel_mode = 0; }
            const bool s = i == sel_game;
            nk_fill_rect(cb, r, 8 * k, s ? (focus == 0 ? C_SEL : C_PANEL) : C_PANEL);
            if (s) nk_fill_rect(cb, nk_rect(r.x, r.y, 8 * k, r.h), 0, C_AMBER);
            text(cb, f_big, r.x + 28 * k, r.y + 12 * k, games[i].name, s ? C_AMBER : C_TEXT);
            char sub[64]; snprintf(sub, sizeof sub, "%s   %s", games[i].year, games[i].cfg ? (!strcmp(games[i].id, "timecrs2") ? "twin cabinet link" : !strcmp(games[i].id, "acedrive") ? "up to 8 linked cabinets" : !strcmp(games[i].id, "cybrcomm") ? "2 linked cabinets" : "up to 4 linked cabinets") : "one cabinet");
            text(cb, f_small, r.x + 28 * k, r.y + 54 * k, sub, C_DIM);
        }
        /* the chosen game: picture, lines, the ways to play */
        const game_t *g = &games[sel_game];
        const float rx = lw + pad, rw = W - rx - pad;
        float ph = rw * 9 / 16; if (ph > (bottom - top) * 0.48f) ph = (bottom - top) * 0.48f;
        const float pw = ph * 16 / 9, px = rx + (rw - pw) / 2;
        nk_fill_rect(cb, nk_rect(px, top, pw, ph), 6 * k, C_DARK);
        if (pics[sel_game]) {
            float iw = pw, ih = pw * pic_h[sel_game] / pic_w[sel_game];
            if (ih > ph) { ih = ph; iw = ph * pic_w[sel_game] / pic_h[sel_game]; }
            struct nk_image im = nk_image_id((int)pics[sel_game]);
            nk_draw_image(cb, nk_rect(px + (pw - iw) / 2, top + (ph - ih) / 2, iw, ih), &im, nk_rgb(255, 255, 255));
        } else text_c(cb, f_mid, px + pw / 2, top + ph / 2 - 14 * k, "(no picture)", C_DIM);
        float y = top + ph + 18 * k;
        text(cb, f_mid, rx, y, g->line1, C_TEXT); y += 34 * k;
        text(cb, f_small, rx, y, g->line2, C_DIM); y += 44 * k;
        const float mh = 46 * k; int cols = g->nmodes > 4 ? 2 : 1; const float mw = cols == 2 ? (rw - 16 * k) / 2 : rw;
        for (int i = 0; i < g->nmodes; i++) {
            const int col = cols == 2 ? i / ((g->nmodes + 1) / 2) : 0, row = cols == 2 ? i % ((g->nmodes + 1) / 2) : i;
            struct nk_rect r = nk_rect(rx + col * (mw + 16 * k), y + row * (mh + 8 * k), mw, mh);
            if (r.y + r.h > bottom) break;
            if (nk_input_is_mouse_hovering_rect(in, r) && in->mouse.delta.x + in->mouse.delta.y != 0) { focus = 1; sel_mode = i; }
            const bool s = focus == 1 && i == sel_mode;
            nk_fill_rect(cb, r, 6 * k, s ? C_AMBER : C_PANEL);
            text(cb, f_mid, r.x + 18 * k, r.y + 8 * k, g->m[i].label, s ? C_DARK : C_TEXT);
            if (nk_input_mouse_clicked(in, NK_BUTTON_LEFT, r)) { sel_mode = i; focus = 1; start_mode(sel_game, i, false); }
        }
        if (focus == 1) text(cb, f_small, rx, bottom - 6 * k, g->m[sel_mode].hint, C_AMBER);
    } else if (screen == SC_SETTINGS) {
        static const char *names[SET_N] = { "Start a game when Windows starts", "Countdown before it starts", "If the game closes",
                                             "This menu's screen", "Link lights (all games)", "Session recordings (all games)",
                                             "Rave Racer 4-cabinet check", "This PC's network address", "Back" };
        text(cb, f_big, pad, top, "SETTINGS", C_AMBER);
        float y = top + 70 * k; const float rowh = 62 * k, w = W - 2 * pad;
        static char addrs[4][48]; static int na; static Uint64 addr_at;      /* this PC's addresses, read every 2 s */
        if (SDL_GetTicks64() >= addr_at) { na = my_addresses(addrs, 4); addr_at = SDL_GetTicks64() + 2000; }
        for (int i = 0; i < SET_N; i++) {
            struct nk_rect r = nk_rect(pad, y + i * (rowh + 8 * k), w, rowh);
            if (nk_input_is_mouse_hovering_rect(in, r) && in->mouse.delta.x + in->mouse.delta.y != 0) sel_set = i;
            const bool s = i == sel_set;
            nk_fill_rect(cb, r, 6 * k, s ? C_SEL : C_PANEL);
            if (s) nk_fill_rect(cb, nk_rect(r.x, r.y, 8 * k, r.h), 0, C_AMBER);
            text(cb, f_mid, r.x + 28 * k, r.y + 14 * k, names[i], s ? C_AMBER : C_TEXT);
            char v[200] = "";
            switch (i) {
            case SET_AUTO: if (S.auto_game < 0) snprintf(v, sizeof v, "Off"); else snprintf(v, sizeof v, "%s: %s", games[S.auto_game].name, games[S.auto_game].m[S.auto_mode].label); break;
            case SET_DELAY: snprintf(v, sizeof v, "%d seconds", S.delay); break;
            case SET_CLOSED: snprintf(v, sizeof v, "%s", S.restart ? "Start it again (automatic start)" : "Come back to this menu"); break;
            case SET_SCREEN: snprintf(v, sizeof v, "%s", S.windowed ? "Window" : "Full screen"); break;
            case SET_LIGHTS: snprintf(v, sizeof v, "%s", all_on("lights", "bottom-left") ? "On" : "Off"); break;
            case SET_RECORD: snprintf(v, sizeof v, "%s", all_on("record", "on") ? "On" : "Off"); break;
            case SET_RRCHECK: snprintf(v, sizeof v, "Run (Enter)"); break;
            case SET_ADDR: if (na) snprintf(v, sizeof v, "%s%s%s", addrs[0], na > 1 ? "   " : "", na > 1 ? addrs[1] : ""); else snprintf(v, sizeof v, "(no network)"); break;
            }
            text(cb, f_mid, r.x + w - tw(f_mid, v) - 28 * k, r.y + 14 * k, v, s ? C_TEXT : C_DIM);
            if (nk_input_mouse_clicked(in, NK_BUTTON_LEFT, r)) { sel_set = i; act(SDLK_RETURN, win); }
        }
        if (sel_set == SET_AUTO) text(cb, f_small, pad, bottom - 6 * k,
            "Windows then starts this menu when it signs in, counts down and starts the game; set Windows to sign in by itself for a cabinet.", C_AMBER);
    } else if (screen == SC_COUNTDOWN || screen == SC_RESTART) {
        const game_t *g = &games[S.auto_game >= 0 ? S.auto_game : 0];
        const int left = (int)((count_until > SDL_GetTicks64() ? count_until - SDL_GetTicks64() : 0) + 999) / 1000;
        char b[200]; snprintf(b, sizeof b, "%s %s in %d", screen == SC_RESTART ? "Starting again:" : "Starting", g->name, left);
        text_c(cb, f_title, W / 2.0f, H * 0.38f, b, C_TEXT);
        text_c(cb, f_mid, W / 2.0f, H * 0.38f + 80 * k, g->m[S.auto_mode].label, C_AMBER);
        text_c(cb, f_mid, W / 2.0f, H * 0.62f, "Press any key or button for the menu", C_DIM);
    } else if (screen == SC_QUIT) {
        text_c(cb, f_big, W / 2.0f, H * 0.40f, "Close the launcher?", C_TEXT);
        text_c(cb, f_mid, W / 2.0f, H * 0.40f + 60 * k, "Enter: close      Esc: back", C_DIM);
    }
    /* the bottom bar */
    nk_fill_rect(cb, nk_rect(0, H - 60 * k, (float)W, 60 * k), 0, C_DARK);
    if (screen == SC_MENU || screen == SC_SETTINGS) {
        text(cb, f_small, pad, H - 44 * k, screen == SC_MENU ? "Arrows / D-pad: choose     Enter / A: start     Esc / B: back     S / Y: settings"
                                                                : "Up / Down: choose     Left / Right / Enter: change     Esc / B: back", C_DIM);
        const char *bt[2] = { screen == SC_MENU ? "Settings" : "Games", "Quit" };
        float bx = W - pad;
        for (int i = 1; i >= 0; i--) {
            const float bw = tw(f_mid, bt[i]) + 40 * k; bx -= bw;
            struct nk_rect r = nk_rect(bx, H - 52 * k, bw, 44 * k);
            const bool h = nk_input_is_mouse_hovering_rect(in, r);
            nk_fill_rect(cb, r, 6 * k, h ? C_AMBER : C_PANEL);
            text(cb, f_mid, r.x + 20 * k, r.y + 6 * k, bt[i], h ? C_DARK : C_TEXT);
            if (nk_input_mouse_clicked(in, NK_BUTTON_LEFT, r)) { if (i == 1) screen = SC_QUIT; else screen = screen == SC_MENU ? SC_SETTINGS : SC_MENU; }
            bx -= 14 * k;
        }
    }
    if (message[0] && SDL_GetTicks64() < message_until) {
        const float mw = tw(f_mid, message) + 60 * k;
        nk_fill_rect(cb, nk_rect((W - mw) / 2, H - 140 * k, mw, 50 * k), 8 * k, C_AMBER);
        text_c(cb, f_mid, W / 2.0f, H - 132 * k, message, C_DARK);
    }
    nk_end(ctx);
}

/* ---- MAIN --------------------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    GetModuleFileNameA(NULL, base, MAX_PATH);
    { char *s = strrchr(base, '\\'); if (s) s[1] = 0; }
    bool want_auto = false;
    const char *shot = NULL; int shot_screen = SC_MENU, shot_game = 0, shot_focus = 0;   /* --shot FILE.bmp [menu|ways|settings] [game]: a picture of one screen */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') { i++; shot_screen = !strcmp(argv[i], "settings") ? SC_SETTINGS : SC_MENU; shot_focus = !strcmp(argv[i], "ways"); }
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') shot_game = atoi(argv[++i]);
            S.windowed = 2;
        }
        else if (!strcmp(argv[i], "--auto")) want_auto = true;
        else if (!strcmp(argv[i], "--window")) S.windowed = 2;
        else if (!strcmp(argv[i], "--write-advanced") && i + 1 < argc) { write_advanced(argv[++i]); return 0; }
        else if (!strcmp(argv[i], "--list")) {
            for (int g = 0; g < NGAMES; g++) for (int m = 0; m < games[g].nmodes; m++) printf("%s:%d  %s - %s\n", games[g].id, m, games[g].name, games[g].m[m].label);
            return 0;
        }
    }
    { char p[MAX_PATH]; snprintf(p, sizeof p, "%slauncher.log", base); log_file = fopen(p, "a"); }
    const int forced_window = S.windowed == 2;
    cfg_load();
    if (forced_window) S.windowed = 1;
    logm("---- " APP_NAME " started%s", want_auto ? " by Windows (--auto)" : "");

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { MessageBoxA(NULL, SDL_GetError(), APP_NAME, MB_ICONERROR); return 1; }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_DisplayMode dm; SDL_GetDesktopDisplayMode(0, &dm);
    SDL_Window *win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, shot ? 1600 : dm.w * 3 / 4, shot ? 900 : dm.h * 3 / 4,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (S.windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP));
    if (!win) { MessageBoxA(NULL, SDL_GetError(), APP_NAME, MB_ICONERROR); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win, gl); SDL_GL_SetSwapInterval(1);
    ctx = nk_sdl_init(win);
    ctx->style.window.border = 0; ctx->style.window.padding = nk_vec2(0, 0);      /* the screen is drawn by hand: no window frame */
    ctx->style.window.fixed_background = nk_style_item_color(C_BG);
    int W, H; SDL_GetWindowSize(win, &W, &H); int fonts_h = H;
    fonts((float)H);
    load_pictures();
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

    if (want_auto && S.auto_game >= 0) { screen = SC_COUNTDOWN; count_until = SDL_GetTicks64() + (Uint64)S.delay * 1000; sel_game = S.auto_game; }
    else if (want_auto) logm("--auto, but no automatic start is set: the menu");
    if (shot) { screen = shot_screen; sel_game = shot_game % NGAMES; focus = shot_focus; }
    Uint64 next_check = 0, gone_since = 0; int frames = 0;
    while (!want_quit) {
        SDL_Event e;
        nk_input_begin(ctx);
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { if (screen != SC_RUNNING) want_quit = true; continue; }
            if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
            if (screen == SC_RUNNING) continue;                          /* the game has the screen */
            int key = 0;
            if (e.type == SDL_KEYDOWN) {
                key = e.key.keysym.sym;
                if (key == SDLK_KP_ENTER || key == SDLK_SPACE) key = SDLK_RETURN;
                if (key == SDLK_BACKSPACE) key = SDLK_ESCAPE;
                if ((key == SDLK_s || key == SDLK_F1) && (screen == SC_MENU || screen == SC_SETTINGS)) { screen = screen == SC_MENU ? SC_SETTINGS : SC_MENU; key = 0; }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: key = SDLK_UP; break;        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key = SDLK_DOWN; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key = SDLK_LEFT; break;    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = SDLK_RIGHT; break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: key = SDLK_RETURN; break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: key = SDLK_ESCAPE; break;
                case SDL_CONTROLLER_BUTTON_Y: if (screen == SC_MENU || screen == SC_SETTINGS) screen = screen == SC_MENU ? SC_SETTINGS : SC_MENU; break;
                }
            } else if (e.type == SDL_CONTROLLERAXISMOTION && (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)) {
                static int held[2];                                      /* the stick as a D-pad: once per push */
                const int a = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY, v = e.caxis.value;
                const int d = v > 20000 ? 1 : v < -20000 ? -1 : 0;
                if (d && d != held[a]) key = a ? (d > 0 ? SDLK_DOWN : SDLK_UP) : (d > 0 ? SDLK_RIGHT : SDLK_LEFT);
                held[a] = d;
            } else if ((e.type == SDL_MOUSEBUTTONDOWN) && (screen == SC_COUNTDOWN || screen == SC_RESTART)) key = SDLK_ESCAPE;
            if (screen == SC_QUIT && key) { if (key == SDLK_RETURN) want_quit = true; else if (key == SDLK_ESCAPE) screen = SC_MENU; continue; }
            if (key) act(key, win);
            if (!shot) nk_sdl_handle_event(&e);                               /* (--shot: no mouse, the picture shows the chosen row) */
        }
        nk_input_end(ctx);
        const Uint64 now = SDL_GetTicks64();
        if ((screen == SC_COUNTDOWN || screen == SC_RESTART) && now >= count_until) {
            logm("countdown over: starting %s - %s", games[S.auto_game].name, games[S.auto_game].m[S.auto_mode].label);
            start_mode(S.auto_game, S.auto_mode, true);
            if (screen == SC_RUNNING) SDL_HideWindow(win);
        } else if (screen == SC_RUNNING && SDL_GetWindowFlags(win) & SDL_WINDOW_SHOWN) {
            SDL_HideWindow(win);
        }
        if (screen == SC_RUNNING) {                                      /* the watchdog: every second */
            if (now >= next_check) {
                next_check = now + 1000;
                if (ours_running() || game_running(run_game)) gone_since = 0;
                else if (!gone_since) gone_since = now;
                else if (now - gone_since >= 3000) {                     /* gone for 3 s (a restart by the game itself is quicker) */
                    forget_procs(); gone_since = 0;
                    logm("%s has closed", games[run_game].name);
                    SDL_ShowWindow(win); SDL_RaiseWindow(win);
                    if (auto_session && S.restart && S.auto_game >= 0) { screen = SC_RESTART; count_until = now + 5000; }
                    else screen = SC_MENU;
                }
            }
            SDL_Delay(100);
            continue;
        }
        SDL_GetWindowSize(win, &W, &H);
        if (H != fonts_h && H > 100) { fonts((float)H); fonts_h = H; }
        draw(W, H, win);
        int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh); glClearColor(C_BG.r / 255.0f, C_BG.g / 255.0f, C_BG.b / 255.0f, 1); glClear(GL_COLOR_BUFFER_BIT);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        if (shot && ++frames == 5) {                                     /* --shot: the picture of this screen, then close */
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 24, SDL_PIXELFORMAT_RGB24);
            unsigned char *row = malloc((size_t)dw * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            for (int y = 0; y < dh; y++) { glReadPixels(0, dh - 1 - y, dw, 1, GL_RGB, GL_UNSIGNED_BYTE, row); memcpy((unsigned char *)s->pixels + y * s->pitch, row, (size_t)dw * 3); }
            SDL_SaveBMP(s, shot); SDL_FreeSurface(s); free(row);
            want_quit = true;
        }
        SDL_GL_SwapWindow(win);
    }
    logm("closed");
    nk_sdl_shutdown(); SDL_GL_DeleteContext(gl); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
