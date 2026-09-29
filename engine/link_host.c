/* link_host.c -- see link_host.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link_host.h"
#include "link_net.h"
#include "c139.h"

bool g_link_mute;

static lnk_cfg cfg = { .id = -1, .nodes = 0, .base_port = 22220, .gather_ms = 60000 };
static const link_host_game *G;
static bool on;
static int  skew_ppm;                                                  /* TEST (--link-test D:J:L:S): this copy's clock S ppm fast (negative: slow) */
static uint32_t down_frames, down_now, up_run;                         /* frames the GAME saw its link down (counted after 2 s of a good link) */
static bool was_up;
static SDL_Window *win;                                                /* the window, for the title's warnings */
static char base_title[256];                                           /* the title after the start-up gathering */
static uint32_t dup_shown;                                             /* the clash last warned about (bit per cabinet) */
static bool from_cmdline;                                              /* --link on the command line (a one-PC test launcher): link.cfg is not used */

bool link_host_on(void) { return on; }
int  link_host_id(void) { return cfg.id; }


bool link_host_arg(int argc, char **argv, int *i)
{
    const char *a = argv[*i];
    bool v = *i + 1 < argc;
    if (!strcmp(a, "--link") && v) cfg.id = atoi(argv[++*i]) - 1;
    else if (!strcmp(a, "--link-cabinets") && v) cfg.nodes = atoi(argv[++*i]);
    else if (!strcmp(a, "--link-peers") && v) cfg.peers = argv[++*i];
    else if (!strcmp(a, "--link-port") && v) cfg.base_port = atoi(argv[++*i]);
    else if (!strcmp(a, "--link-wait") && v) cfg.gather_ms = atoi(argv[++*i]) * 1000;
    else if (!strcmp(a, "--link-test") && v) sscanf(argv[++*i], "%d:%d:%d:%d", &cfg.delay_ms, &cfg.jitter_ms, &cfg.loss_pct, &skew_ppm);
    else if (!strcmp(a, "--mute")) g_link_mute = true;
    else return false;
    return true;
}

bool link_host_open(const link_host_game *g)
{
    if (cfg.id < 0) return true;                                       /* not linked */
    G = g;
    cfg.tag = g->tag;
    if (!cfg.nodes) {                                                  /* the set's size: --link-cabinets, else the addresses given, else 4 */
        cfg.nodes = 4;
        if (cfg.peers) { cfg.nodes = 1; for (const char *c = cfg.peers; *c; c++) cfg.nodes += *c == ','; }
    }
    if (!lnk_open(&cfg)) return false;
    atexit(lnk_close);                                                 /* however the game ends, the others hear it go */
    on = true;
    return true;
}

/* the copies of a set on one PC (no addresses given): their windows in a grid by cabinet number, so none has to be dragged apart --
 * while a window is dragged Windows holds its game still, and a cabinet that stops for more than a tenth of a second drops out */
static void place_window(SDL_Window *w)
{
    SDL_Rect b; int ww, wh, top = 0, l = 0, bo = 0, r = 0;
    if (cfg.peers || SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(w), &b) != 0) return;
    SDL_GetWindowSize(w, &ww, &wh);
    SDL_GetWindowBordersSize(w, &top, &l, &bo, &r);
    const int cols = cfg.nodes <= 1 ? 1 : 2, rows = (cfg.nodes + cols - 1) / cols;
    const int fw = ww + l + r, fh = wh + top + bo;                        /* the window with its frame */
    const int sx = cols > 1 ? (b.w - fw < fw ? (b.w - fw) / (cols - 1) : fw) : 0;       /* overlap them when the screen is too small */
    const int sy = rows > 1 ? (b.h - fh < fh * (rows - 1) ? (b.h - fh) / (rows - 1) : fh) : 0;
    const int c = cfg.id % cols, rw = cfg.id / cols;
    SDL_SetWindowPosition(w, b.x + l + c * (sx > 0 ? sx : 0), b.y + top + rw * (sy > 0 ? sy : 0));
}

void link_host_gather(const char *title, SDL_Window *w)
{
    if (!on) return;
    if (w) place_window(w);
    uint32_t shown = 0xFFFFFFFFu; int shown_s = -1;
    while (lnk_gather() == LNK_GATHERING) {
        SDL_Event e;
        while (w && SDL_PollEvent(&e)) if (e.type == SDL_QUIT) { lnk_close(); exit(0); }
        uint32_t here = lnk_present_mask(); int left = (lnk_gather_left_ms() + 999) / 1000;
        if (here != shown || left != shown_s) {
            char miss[40] = "", t[256]; int k = 0;
            for (int n = 0; n < cfg.nodes; n++) if (!(here >> n & 1)) k += snprintf(miss + k, sizeof miss - (size_t)k, "%s%d", k ? ", " : "", n + 1);
            if (*miss) snprintf(t, sizeof t, "%s - waiting for cabinet%s %s (%d s)", title, strchr(miss, ',') ? "s" : "", miss, left);
            else snprintf(t, sizeof t, "%s - every cabinet is here: starting", title);
            if (w) SDL_SetWindowTitle(w, t);
            if (here != shown) fprintf(stderr, "[LINK] %s\n", t + strlen(title) + 3);
            shown = here; shown_s = left;
        }
        SDL_Delay(5);
    }
    char t[256];
    if (lnk_state() == LNK_ALONE) snprintf(t, sizeof t, "%s - NOT LINKED (it started after the others): restart every cabinet", title);
    else {
        char who[40] = ""; int k = 0;
        for (int n = 0; n < cfg.nodes; n++) if (lnk_members() >> n & 1) k += snprintf(who + k, sizeof who - (size_t)k, "%s%d", k ? "+" : "", n + 1);
        snprintf(t, sizeof t, "%s - linked: cabinets %s", title, who);
    }
    if (w) SDL_SetWindowTitle(w, t);
    win = w; snprintf(base_title, sizeof base_title, "%s", t);
}

/* two members whose games use the same ID: the game cannot tell them apart (the operator sets each one's ID in its test menu) */
static void check_ids(void)
{
    if (!G->game_id || lnk_state() != LNK_LINKED) return;
    lnk_set_game_id(G->game_id());
    uint32_t clash = 0; const uint32_t on = lnk_alive_mask();
    for (int n = 0; n < LNK_MAX_NODES; n++) if ((on >> n & 1) && lnk_peer_game_id(n) < 0) return;   /* wait until every member's ID is known */
    for (int a = 0; a < LNK_MAX_NODES; a++) for (int b = a + 1; b < LNK_MAX_NODES; b++)
        if ((on >> a & 1) && (on >> b & 1) && lnk_peer_game_id(a) >= 0 && lnk_peer_game_id(a) == lnk_peer_game_id(b)) clash |= 1u << a | 1u << b;
    if (clash == dup_shown) return;
    dup_shown = clash;
    char t[512];
    if (clash) {
        char who[40] = ""; int k = 0;
        for (int n = 0; n < LNK_MAX_NODES; n++) if (clash >> n & 1) k += snprintf(who + k, sizeof who - (size_t)k, "%s%d", k ? ", " : "", n + 1);
        snprintf(t, sizeof t, "%s - WARNING: cabinets %s have the same game ID: give each its own in the test menu", base_title, who);
        fprintf(stderr, "[LINK] WARNING: cabinets %s have the same game ID (test menu): the game cannot tell them apart\n", who);
    } else {
        snprintf(t, sizeof t, "%s", base_title);
        fprintf(stderr, "[LINK] every cabinet's game ID is different now\n");
    }
    if (win) SDL_SetWindowTitle(win, t);
}

void link_host_slice(void)
{
    if (!on) return;
    for (int k = 0; k < 8 && c139_tick(k == 0); k++) {                /* a frame is taken in and forwarded within the slice */
        G->raise_irq();
        G->deliver_irqs();
    }
}

static void pace(void)                                                 /* headless linked copies: real time, like a window */
{
    static uint64_t next; const uint64_t f = SDL_GetPerformanceFrequency(), step = (uint64_t)((double)f / (59.906 * (1.0 + skew_ppm / 1e6)));
    uint64_t t = SDL_GetPerformanceCounter();
    if (!next || t > next + 10 * step) next = t;
    next += step;
    while ((t = SDL_GetPerformanceCounter()) < next) { uint64_t left_ms = (next - t) * 1000 / f; if (left_ms > 2) SDL_Delay((Uint32)(left_ms - 1)); }
}

static void report(uint32_t frame)
{
    const lnk_stats *n = lnk_get_stats(); const c139_stats *c = c139_get_stats();
    char g[200] = "";
    if (G->report) G->report(g, sizeof g);
    char ids[64] = ""; int k = 0;
    for (int n = 0; n < LNK_MAX_NODES; n++) { int id = lnk_peer_game_id(n); if (id >= 0) k += snprintf(ids + k, sizeof ids - (size_t)k, " %d:%d", n + 1, id + 1); }
    fprintf(stderr, "[LINK] frame %u  down %u (now %u)  cabinets on %02X (next %d)  game IDs%s  chip tx %u rx %u queue-drop %u unacked %u  net sent %u recv %u lost %u bad %u  | %s\n",
            frame, down_frames, down_now, lnk_alive_mask(), lnk_next_hop() + 1, ids, c->tx, c->rx, c->rx_dropped, c->rx_unacked, n->sent, n->recv, n->lost_seq, n->bad, g);
}

void link_host_frame(uint32_t frame, bool headless)
{
    if (!on) return;
    if (headless) pace();
    if (G->good) {
        if (G->good()) { if (++up_run >= 120) was_up = true; down_now = 0; }
        else if ((up_run = 0), was_up) { down_frames++; if (++down_now == 1) fprintf(stderr, "[LINK] frame %u: the game lost the link\n", frame); }
    }
    if (frame % 30 == 0 && frame >= 300) check_ids();              /* the games take a moment to read their own ID from their settings: 0 until then */
    if (frame % 300 == 0) report(frame);
}

/* ---- THE SAVED SETTINGS (link.cfg beside the program) and the menu's Network page -----------------------------------------------
 * The file is what the Network page edits:  link=on|off  cabinet=N  cabinets=M  address1=...  address8=...  wait=S  port=P
 * Used when the command line has no --link (a cabinet started by double-clicking the game); --link and friends override it. A change
 * takes effect the next time the game starts: the boards only look for each other when they are switched on. */
#define CFG_FILE "link.cfg"
static struct { bool on, set_id; int cabinet, cabinets, wait, port; char addr[LNK_MAX_NODES][16]; } F = { false, false, 1, 2, 60, 22220, { "" } };
static char peers_buf[LNK_MAX_NODES * 17];
static int  edit_row = -1;                                             /* the address row being typed, -1 = none */
static char edit_buf[16];

static void cfg_load(void)
{
    FILE *f = fopen(CFG_FILE, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char k[32], v[64]; int n;
        if (sscanf(line, " %31[^= ] = %63s", k, v) != 2) continue;
        if (!strcmp(k, "link")) F.on = !strcmp(v, "on");
        else if (!strcmp(k, "cabinet")) F.cabinet = atoi(v);
        else if (!strcmp(k, "cabinets")) F.cabinets = atoi(v);
        else if (!strcmp(k, "wait")) F.wait = atoi(v);
        else if (!strcmp(k, "port")) F.port = atoi(v);
        else if (!strcmp(k, "set_game_id")) F.set_id = atoi(v) != 0;
        else if (sscanf(k, "address%d", &n) == 1 && n >= 1 && n <= LNK_MAX_NODES) snprintf(F.addr[n - 1], sizeof F.addr[0], "%.15s", v);
    }
    fclose(f);
    if (F.cabinets < 2) F.cabinets = 2;
    if (F.cabinets > LNK_MAX_NODES) F.cabinets = LNK_MAX_NODES;
    if (F.cabinet < 1) F.cabinet = 1;
    if (F.cabinet > F.cabinets) F.cabinet = F.cabinets;
    if (F.wait < 10) F.wait = 10;
}

static void cfg_save(void)
{
    FILE *f = fopen(CFG_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "# Link play settings (the game's Esc menu > Network edits this file). Used when the game is started without --link.\n");
    fprintf(f, "link=%s\ncabinet=%d\ncabinets=%d\nwait=%d\nport=%d\n", F.on ? "on" : "off", F.cabinet, F.cabinets, F.wait, F.port);
    for (int n = 0; n < LNK_MAX_NODES; n++) if (F.addr[n][0]) fprintf(f, "address%d=%s\n", n + 1, F.addr[n]);
    if (F.set_id) fprintf(f, "# the game's own ID (test menu) is set to this cabinet's number when the game next starts\nset_game_id=1\n");
    fclose(f);
#ifdef _WIN32
    remove(CFG_FILE);
#endif
    rename(CFG_FILE ".tmp", CFG_FILE);
}

int link_host_prescan(int argc, char **argv)
{
    cfg_load();
    for (int i = 1; i < argc; i++) if (!strncmp(argv[i], "--link", 6)) {           /* the command line decides */
        from_cmdline = true;
        for (int j = 1; j + 1 < argc; j++) if (!strcmp(argv[j], "--link")) return atoi(argv[j + 1]);
        return 0;
    }
    if (!F.on) return 0;
    cfg.id = F.cabinet - 1; cfg.nodes = F.cabinets; cfg.gather_ms = F.wait * 1000; cfg.base_port = F.port;
    int k = 0;
    for (int n = 0; n < F.cabinets; n++) k += snprintf(peers_buf + k, sizeof peers_buf - (size_t)k, "%s%s", n ? "," : "", F.addr[n]);
    cfg.peers = peers_buf;
    return F.cabinet;
}

/* the Network page changed "This cabinet": at the next start the game's own ID follows it (once; the test menu can change it after) */
bool link_host_take_id_reset(void)
{
    if (from_cmdline || !on || !F.set_id) return false;
    F.set_id = false;
    cfg_save();
    return true;
}

/* the page: 0 Link play, 1 This cabinet, 2 Cabinets in the set, 3.. the addresses, then the wait. A copy started by a one-PC test
 * launcher (--link ...) shows what the launcher gave it, read-only: its number comes from the launcher every time it starts. */
static int page_nodes(void) { return from_cmdline ? cfg.nodes : F.cabinets; }
int  link_page_rows(void) { return 3 + page_nodes() + 1; }
bool link_page_has_value(int row) { return row < 3 || row == 3 + page_nodes(); }
static void live_text(int row, char *label, size_t ln, char *value, size_t vn)
{
    if (row == 0) { snprintf(label, ln, "Link play"); snprintf(value, vn, "On"); }
    else if (row == 1) { snprintf(label, ln, "This cabinet"); snprintf(value, vn, "%d", cfg.id + 1); }
    else if (row == 2) { snprintf(label, ln, "Cabinets in the set"); snprintf(value, vn, "%d", cfg.nodes); }
    else if (row < 3 + cfg.nodes) {
        const int n = row - 3;
        if (n == cfg.id) snprintf(label, ln, "Cabinet %d (this PC)", n + 1); else snprintf(label, ln, "Cabinet %d address", n + 1);
        const char *p = cfg.peers;
        for (int k = 0; p && k < n; k++) { p = strchr(p, ','); if (p) p++; }
        if (p) { size_t e = strcspn(p, ","); snprintf(value, vn, "%.*s", (int)(e < 15 ? e : 15), p); }
        else snprintf(value, vn, "127.0.0.1 (this PC)");
    } else { snprintf(label, ln, "Wait at start-up"); snprintf(value, vn, "%d s", cfg.gather_ms / 1000); }
}
void link_page_text(int row, char *label, size_t ln, char *value, size_t vn)
{
    if (from_cmdline) { live_text(row, label, ln, value, vn); return; }
    if (row == 0) { snprintf(label, ln, "Link play"); snprintf(value, vn, "%s", F.on ? "On" : "Off"); }
    else if (row == 1) { snprintf(label, ln, "This cabinet"); snprintf(value, vn, "%d", F.cabinet); }
    else if (row == 2) { snprintf(label, ln, "Cabinets in the set"); snprintf(value, vn, "%d", F.cabinets); }
    else if (row < 3 + F.cabinets) {
        const int n = row - 3;
        if (n + 1 == F.cabinet) snprintf(label, ln, "Cabinet %d (this PC)", n + 1); else snprintf(label, ln, "Cabinet %d address", n + 1);
        if (edit_row == row) snprintf(value, vn, "%s_", edit_buf);
        else snprintf(value, vn, "%s", F.addr[n][0] ? F.addr[n] : "(Enter to type)");
    } else { snprintf(label, ln, "Wait at start-up"); snprintf(value, vn, "%d s", F.wait); }
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void link_page_change(int row, int dir)
{
    if (from_cmdline) return;                                          /* the launcher's settings: nothing here to change */
    if (row == 0) F.on = !F.on;
    else if (row == 1) { F.cabinet = clampi(F.cabinet + (dir ? dir : 1), 1, F.cabinets); F.set_id = true; }
    else if (row == 2) { const int c = F.cabinet; F.cabinets = clampi(F.cabinets + (dir ? dir : 1), 2, LNK_MAX_NODES); F.cabinet = clampi(F.cabinet, 1, F.cabinets); F.set_id |= F.cabinet != c; }
    else if (row < 3 + F.cabinets) {
        if (dir) return;
        edit_row = row; snprintf(edit_buf, sizeof edit_buf, "%s", F.addr[row - 3]);                /* Enter: type the address */
        return;
    } else F.wait = clampi(F.wait + (dir ? dir : 1) * 10, 10, 300);
    cfg_save();
}
bool link_page_editing(void) { return edit_row >= 0; }
/* one key while an address is typed: digits and dots; Backspace deletes; Enter keeps it; Esc drops it. false = typing is over */
bool link_page_type(SDL_Scancode sc)
{
    if (edit_row < 0) return false;
    size_t n = strlen(edit_buf);
    char c = 0;
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) c = (char)('1' + (sc - SDL_SCANCODE_1));
    else if (sc == SDL_SCANCODE_0) c = '0';
    else if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) c = (char)('1' + (sc - SDL_SCANCODE_KP_1));
    else if (sc == SDL_SCANCODE_KP_0) c = '0';
    else if (sc == SDL_SCANCODE_PERIOD || sc == SDL_SCANCODE_KP_PERIOD) c = '.';
    if (c) { if (n + 1 < sizeof edit_buf) { edit_buf[n] = c; edit_buf[n + 1] = 0; } return true; }
    if (sc == SDL_SCANCODE_BACKSPACE) { if (n) edit_buf[n - 1] = 0; return true; }
    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) { snprintf(F.addr[edit_row - 3], sizeof F.addr[0], "%s", edit_buf); cfg_save(); }
    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_UNKNOWN) { edit_row = -1; return false; }
    return true;                                                       /* any other key: ignored, still typing */
}
void link_page_notes(void (*line)(const char *fmt, ...))
{
    char a[4][16]; int n = lnk_local_addresses(a, 4);
    char s[80] = ""; int k = 0;
    for (int i = 0; i < n; i++) k += snprintf(s + k, sizeof s - (size_t)k, "%s%s", i ? ", " : "", a[i]);
    line("This PC's network address: %s", n ? s : "(none found)");
    if (from_cmdline) {
        line("Set by the test launcher (cabinet %d of %d),", cfg.id + 1, cfg.nodes);
        line("so not changed here. On a cabinet PC use");
        line("the 'play as a cabinet' launcher, then set it here.");
    } else {
        if (edit_row >= 0) line("Type the address, then Enter (Esc cancels).");
        line("Same list on every cabinet; used from the next start.");
        if (F.set_id) line("The game's own ID will then be set to %d too.", F.cabinet);
    }
    if (on) line("Linked: the game runs on behind this menu.");
    if (on) {
        char who[40] = ""; int j = 0;
        for (int m = 0; m < cfg.nodes; m++) if (lnk_members() >> m & 1) j += snprintf(who + j, sizeof who - (size_t)j, "%s%d", j ? "+" : "", m + 1);
        if (lnk_state() == LNK_ALONE) line("Now: cabinet %d, NOT linked (it started after the others)", cfg.id + 1);
        else line("Now: cabinet %d, linked with cabinets %s", cfg.id + 1, who);
    } else line("Now: not linked");
}
