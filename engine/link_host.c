/* link_host.c -- see link_host.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link_host.h"
#include "link_net.h"
#include "c139.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

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
static char **argv0;                                                   /* the command line, for a restart */
static char dup_msg[128], setup_msg_shown[160];                        /* the title's warnings: two cabinets with one game ID; set-ups that differ */
static void set_title(void);
static void setup_msg(char *m, size_t n);

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

/* "Hears 1 (192.168.1.163), 3 (192.168.1.164) | Can't hear 4 | Not heard by 3" -- who this one hears, from where, who hears it */
static void hearing(char *buf, size_t n, bool addresses)
{
    const uint32_t h = lnk_hear_mask(), by = lnk_heard_by_mask();
    size_t k = 0; char not_h[40] = "", not_by[40] = ""; int kn = 0, kb = 0;
    k += (size_t)snprintf(buf + k, n - k, "Hears");
    bool any = false;
    for (int c = 0; c < cfg.nodes; c++) {
        if (c == cfg.id) continue;
        char a[16]; int ago; uint32_t pk;
        if (h >> c & 1) {
            if (addresses && lnk_peer_info(c, a, &ago, &pk)) k += (size_t)snprintf(buf + k, n - k, "%s %d (%s)", any ? "," : "", c + 1, a);
            else k += (size_t)snprintf(buf + k, n - k, "%s %d", any ? "," : "", c + 1);
            any = true;
            if (!(by >> c & 1)) kb += snprintf(not_by + kb, sizeof not_by - (size_t)kb, "%s%d", kb ? ", " : "", c + 1);
        } else kn += snprintf(not_h + kn, sizeof not_h - (size_t)kn, "%s%d", kn ? ", " : "", c + 1);
        if (k >= n) return;
    }
    if (!any) k += (size_t)snprintf(buf + k, n - k, " nobody");
    if (*not_h && k < n) k += (size_t)snprintf(buf + k, n - k, " | Can't hear %s", not_h);
    if (*not_by && k < n) snprintf(buf + k, n - k, " | Not heard by %s", not_by);
}

static char auto_mine[16];                                             /* automatic numbering: this PC's address, when it is not in the list */
static bool auto_fail;

void link_host_gather(const char *title, SDL_Window *w, void (*hint)(const char *text, int frames))
{
    if (auto_fail) {                                                   /* This cabinet = Auto, and this PC is not in the list: not linked */
        char t[256];
        snprintf(t, sizeof t, "%s - NOT LINKED: this PC (%s) is not in the address list", title, *auto_mine ? auto_mine : "no address");
        fprintf(stderr, "[LINK] NOT linked: \"This cabinet\" is Auto, but this PC's address (%s) is not in the Network page's address list\n",
                *auto_mine ? auto_mine : "none found");
        if (w) SDL_SetWindowTitle(w, t);
        if (hint) hint("NOT LINKED: THIS PC IS NOT IN THE ADDRESS LIST", 900);
        return;
    }
    if (!on) return;
    if (w) place_window(w);
    uint32_t shown = 0xFFFFFFFFu, shown_deaf = 0; int shown_s = -1;
    while (lnk_gather() == LNK_GATHERING) {
        SDL_Event e;
        while (w && SDL_PollEvent(&e)) if (e.type == SDL_QUIT) { lnk_close(); exit(0); }
        uint32_t here = lnk_present_mask(); int left = (lnk_gather_left_ms() + 999) / 1000;
        const uint32_t deaf = lnk_hear_mask() & ~lnk_heard_by_mask();   /* cabinets this one hears that cannot hear it */
        char sm[160]; setup_msg(sm, sizeof sm);
        if (strcmp(sm, setup_msg_shown)) { if (*sm) fprintf(stderr, "[LINK] SET-UP DIFFERS: %s (Esc > Network on each cabinet: the same set size and address list)\n", sm);
                                           snprintf(setup_msg_shown, sizeof setup_msg_shown, "%s", sm); shown_s = -1; }
        if (here != shown || left != shown_s || deaf != shown_deaf) {
            char miss[40] = "", t[256], d[64] = ""; int k = 0, kd = 0;
            for (int n = 0; n < cfg.nodes; n++) if (!(here >> n & 1)) k += snprintf(miss + k, sizeof miss - (size_t)k, "%s%d", k ? ", " : "", n + 1);
            for (int n = 0; n < cfg.nodes; n++) if (deaf >> n & 1) kd += snprintf(d + kd, sizeof d - (size_t)kd, "%s%d", kd ? ", " : "", n + 1);
            if (*miss) snprintf(t, sizeof t, "%s - Waiting for Cabinet%s %s (%d s)", title, strchr(miss, ',') ? "s" : "", miss, left);
            else snprintf(t, sizeof t, "%s - All cabinets found: starting", title);
            if (*d) { size_t tl = strlen(t); snprintf(t + tl, sizeof t - tl, " - Cabinet%s %s can't hear this PC", strchr(d, ',') ? "s" : "", d); }
            if (*sm) { size_t tl = strlen(t); snprintf(t + tl, sizeof t - tl, " - SET-UP DIFFERS: %s", sm); }
            if (w) SDL_SetWindowTitle(w, t);
            if (here != shown || deaf != shown_deaf) {
                char hb[200]; hearing(hb, sizeof hb, true);
                fprintf(stderr, "[LINK] %s  [%s]\n", t + strlen(title) + 3, hb);
            }
            shown = here; shown_s = left; shown_deaf = deaf;
        }
        SDL_Delay(5);
    }
    char t[256];
    if (lnk_state() == LNK_ALONE) {
        snprintf(t, sizeof t, "%s - NOT LINKED (started late): restart all cabinets to link", title);
        if (hint) hint("NOT LINKED: STARTED AFTER THE OTHERS", 600);          /* (full screen: the title bar is not seen) */
    } else if (lnk_members() == 1u << cfg.id) {
        snprintf(t, sizeof t, "%s - No other cabinets found: playing solo", title);
        if (hint) hint("NO OTHER CABINETS FOUND: PLAYING SOLO", 600);
    } else {
        char who[40] = ""; int k = 0;
        for (int n = 0; n < cfg.nodes; n++) if (lnk_members() >> n & 1) k += snprintf(who + k, sizeof who - (size_t)k, "%s%d", k ? "+" : "", n + 1);
        snprintf(t, sizeof t, "%s - Linked: %s", title, who);
    }
    win = w; snprintf(base_title, sizeof base_title, "%s", t);
    set_title();
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
    if (clash) {
        char who[40] = ""; int k = 0;
        for (int n = 0; n < LNK_MAX_NODES; n++) if (clash >> n & 1) k += snprintf(who + k, sizeof who - (size_t)k, "%s%d", k ? ", " : "", n + 1);
        snprintf(dup_msg, sizeof dup_msg, "Cabinets %s have the same game ID (change it in the test menu)", who);
        fprintf(stderr, "[LINK] WARNING: cabinets %s have the same game ID (test menu): the game cannot tell them apart\n", who);
    } else {
        dup_msg[0] = 0;
        fprintf(stderr, "[LINK] every cabinet's game ID is different now\n");
    }
    set_title();
}

/* SET-UPS THAT DIFFER: every cabinet of a set needs the same "Cabinets in the set" and the same address list (each its own number).
 * A cabinet with another size waits for cabinets that are not there, or sends to the wrong ones; so it is said plainly. */
static void setup_msg(char *m, size_t n)
{
    m[0] = 0;
    for (int c = 0; c < cfg.nodes; c++) {
        bool same = true; const int nodes = lnk_peer_setup(c, &same);
        if (!nodes) continue;
        if (nodes != cfg.nodes) { snprintf(m, n, "Cabinet %d is set for %d cabinets, this PC for %d", c + 1, nodes, cfg.nodes); return; }
        if (!same) { snprintf(m, n, "Cabinet %d has a different address list", c + 1); return; }
    }
}
static void check_setup(void)
{
    char m[160]; setup_msg(m, sizeof m);
    if (!strcmp(m, setup_msg_shown)) return;
    if (*m) fprintf(stderr, "[LINK] SET-UP DIFFERS: %s (Esc > Network on each cabinet: the same set size and address list)\n", m);
    else if (*setup_msg_shown) fprintf(stderr, "[LINK] every cabinet's set-up matches now\n");
    snprintf(setup_msg_shown, sizeof setup_msg_shown, "%s", m);
    set_title();
}
static void set_title(void)
{
    if (!win) return;
    char t[640]; size_t k = (size_t)snprintf(t, sizeof t, "%s", base_title);
    if (*setup_msg_shown && k < sizeof t) k += (size_t)snprintf(t + k, sizeof t - k, " - SET-UP DIFFERS: %s", setup_msg_shown);
    if (*dup_msg && k < sizeof t) snprintf(t + k, sizeof t - k, " - WARNING: %s", dup_msg);
    SDL_SetWindowTitle(win, t);
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

/* THE SAFETY NET: a linked game that has lost its link for good restarts, as if its cabinet were switched off and on, so it is not left
 * with a battle nobody else is in (enemies that stand still: the cabinet that ran them has gone). When: after the link had worked,
 * every other cabinet gone for 5 s, or the game itself without its link for 10 s. First "LINK LOST - RESTARTING IN 5" on the screen;
 * if the link comes back meanwhile, nothing happens. After the restart it looks for the others as at power-on: any still playing
 * leave it out (it plays on its own); if they were all restarted too, it links with them. Network page: "If the link is lost". */
static bool lost_carry_on;                                             /* the page's choice: carry on instead (set from link.cfg) */
static void safety_net(uint32_t frame, bool headless)
{
    static uint32_t alone_for, gdown_for; static int count = -1; static bool told;
    if (lnk_state() != LNK_LINKED || !was_up || lnk_members() == 1u << cfg.id) { alone_for = gdown_for = 0; return; }   /* (a set of one: nothing to lose) */
    alone_for = (lnk_alive_mask() & ~(1u << cfg.id)) ? 0 : alone_for + 1;
    gdown_for = (G->good && !G->good()) ? gdown_for + 1 : 0;
    const char *why = alone_for >= 300 ? "every other cabinet has gone" : gdown_for >= 600 ? "the game has had no link for 10 seconds" : NULL;
    if (!why) {
        if (count >= 0) { fprintf(stderr, "[LINK] frame %u: SAFETY NET: the link is back: not restarting\n", frame); if (G->hint) G->hint("", 0); }
        count = -1; told = false;
        return;
    }
    if (lost_carry_on) {
        if (!told) fprintf(stderr, "[LINK] frame %u: SAFETY NET: %s; carrying on (Network page: If the link is lost = Carry on)\n", frame, why);
        told = true; count = -1;
        return;
    }
    if (count < 0) { count = 300; fprintf(stderr, "[LINK] frame %u: SAFETY NET: %s: restarting this cabinet in 5 s\n", frame, why); }
    if (count % 60 == 0 && count > 0 && G->hint) { char t[64]; snprintf(t, sizeof t, "LINK LOST - RESTARTING IN %d", count / 60); G->hint(t, 75); }
    if (count-- > 0) return;
    if (headless) {                                                    /* the tests: no restart, the log says it would */
        fprintf(stderr, "[LINK] frame %u: SAFETY NET: (a test run: it would restart now)\n", frame);
        alone_for = gdown_for = 0; count = -1; was_up = false;
        return;
    }
    fprintf(stderr, "[LINK] frame %u: SAFETY NET: restarting now\n", frame);
    if (G->before_restart) G->before_restart();
    link_host_relaunch();
}

/* start this program again with the same command line (the log continues), then end this copy */
void link_host_relaunch(void)
{
    lnk_close();                                                       /* its port free for the new copy; the others hear it go */
    fflush(NULL);
#ifdef _WIN32
    SetEnvironmentVariableA("N22_LOG_APPEND", "1");
    static wchar_t exe[32768], cl[32768];
    const DWORD n = GetModuleFileNameW(NULL, exe, 32768);
    wcsncpy(cl, GetCommandLineW(), 32767);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (n > 0 && n < 32768 && CreateProcessW(exe, cl, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    else fprintf(stderr, "[LINK] the restart failed (error %lu)\n", (unsigned long)GetLastError());
#else
    setenv("N22_LOG_APPEND", "1", 1);
    if (argv0) execv("/proc/self/exe", argv0);
    perror("[LINK] the restart failed");
#endif
    exit(0);
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
    if (frame % 30 == 0) check_setup();
    safety_net(frame, headless);
    if (frame % 300 == 0) {
        report(frame);
        char hb[240]; hearing(hb, sizeof hb, true);
        static char last[240];
        if (strcmp(hb, last) || frame % 3600 == 0) { fprintf(stderr, "[LINK] frame %u  %s\n", frame, hb); snprintf(last, sizeof last, "%s", hb); }
    }
}

/* ---- THE SAVED SETTINGS (link.cfg beside the program) and the menu's Network page -----------------------------------------------
 * The file is what the Network page edits:  link=on|off  cabinet=N  cabinets=M  address1=...  address8=...  wait=S  port=P
 * Used when the command line has no --link (a cabinet started by double-clicking the game); --link and friends override it. A change
 * takes effect the next time the game starts: the boards only look for each other when they are switched on. */
#define CFG_FILE "link.cfg"
/* cabinet_auto: "This cabinet" = Auto, the number is where this PC's own address is in the list (last_auto: the one found last time;
 * when it changes the game's own ID follows). lights: 0 off, else where (LINK_LIGHTS_*). */
static struct { bool on, set_id, carry_on, cab_auto; int cabinet, cabinets, wait, port, lights, last_auto; char addr[LNK_MAX_NODES][16]; }
    F = { false, false, false, true, 1, 2, 60, 22220, 0, 0, { "" } };
static const char *const light_key[7] = { "off", "bottom-left", "bottom-centre", "bottom-right", "top-left", "top-centre", "top-right" };
static const char *const light_name[7] = { "Off", "Bottom left", "Bottom centre", "Bottom right", "Top left", "Top centre", "Top right" };
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
        else if (!strcmp(k, "cabinet")) { F.cab_auto = !strcmp(v, "auto"); if (!F.cab_auto) F.cabinet = atoi(v); }
        else if (!strcmp(k, "last_auto")) F.last_auto = atoi(v);
        else if (!strcmp(k, "cabinets")) F.cabinets = atoi(v);
        else if (!strcmp(k, "wait")) F.wait = atoi(v);
        else if (!strcmp(k, "port")) F.port = atoi(v);
        else if (!strcmp(k, "set_game_id")) F.set_id = atoi(v) != 0;
        else if (!strcmp(k, "lights")) { F.lights = !strcmp(v, "on") ? 1 : 0; for (int i = 1; i < 7; i++) if (!strcmp(v, light_key[i])) F.lights = i; }
        else if (!strcmp(k, "if_lost")) F.carry_on = !strcmp(v, "carry_on");
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
    if (F.cab_auto) fprintf(f, "link=%s\ncabinet=auto\n", F.on ? "on" : "off");
    else fprintf(f, "link=%s\ncabinet=%d\n", F.on ? "on" : "off", F.cabinet);
    fprintf(f, "cabinets=%d\nwait=%d\nport=%d\n", F.cabinets, F.wait, F.port);
    if (F.last_auto) fprintf(f, "last_auto=%d\n", F.last_auto);
    for (int n = 0; n < LNK_MAX_NODES; n++) if (F.addr[n][0]) fprintf(f, "address%d=%s\n", n + 1, F.addr[n]);
    if (F.set_id) fprintf(f, "# the game's own ID (test menu) is set to this cabinet's number when the game next starts\nset_game_id=1\n");
    fprintf(f, "lights=%s\nif_lost=%s\n", light_key[F.lights], F.carry_on ? "carry_on" : "restart");
    fclose(f);
#ifdef _WIN32
    remove(CFG_FILE);
#endif
    rename(CFG_FILE ".tmp", CFG_FILE);
}

/* automatic numbering: this PC's cabinet = the position of one of its own addresses in the list (0 = none). At start-up it waits up to
 * 15 s for one to appear: a PC switched on with the cabinet may start the game before its network is up. */
static int own_cabinet(bool wait)
{
    static uint32_t t_last; static int last;                           /* (the menu asks every frame: once a second is enough) */
    if (!wait && t_last && SDL_GetTicks() - t_last < 1000) return last;
    if (!wait) t_last = SDL_GetTicks();
    last = 0;
    for (int tries = 0; tries < (wait ? 30 : 1); tries++) {
        char a[8][16]; const int n = lnk_local_addresses(a, 8);
        for (int c = 0; c < F.cabinets; c++) for (int i = 0; i < n; i++) if (F.addr[c][0] && !strcmp(F.addr[c], a[i])) return last = c + 1;
        snprintf(auto_mine, sizeof auto_mine, "%s", n ? a[0] : "");
        if (wait) SDL_Delay(500);
    }
    return 0;
}

int link_host_prescan(int argc, char **argv)
{
    argv0 = argv;
    cfg_load();
    lost_carry_on = F.carry_on;
    for (int i = 1; i < argc; i++) if (!strncmp(argv[i], "--link", 6)) {           /* the command line decides */
        from_cmdline = true;
        for (int j = 1; j + 1 < argc; j++) if (!strcmp(argv[j], "--link")) return atoi(argv[j + 1]);
        return 0;
    }
    if (!F.on) return 0;
    int cab = F.cabinet;
    if (F.cab_auto) {                                                  /* where this PC's own address is in the list */
        cab = own_cabinet(true);
        if (cab <= 0) { auto_fail = true; return 0; }
        if (F.last_auto && cab != F.last_auto) F.set_id = true;         /* a new number: the game's own ID follows it */
        if (cab != F.last_auto) { F.last_auto = cab; cfg_save(); }
    }
    cfg.id = cab - 1; cfg.nodes = F.cabinets; cfg.gather_ms = F.wait * 1000; cfg.base_port = F.port;
    int k = 0;
    for (int n = 0; n < F.cabinets; n++) k += snprintf(peers_buf + k, sizeof peers_buf - (size_t)k, "%s%s", n ? "," : "", F.addr[n]);
    cfg.peers = peers_buf;
    return cab;
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
int  link_page_rows(void) { return 3 + page_nodes() + 3; }
bool link_page_has_value(int row) { return row < 3 || row >= 3 + page_nodes(); }
static bool extra_text(int row, char *label, size_t ln, char *value, size_t vn)   /* the rows after the wait */
{
    const int r = row - (3 + page_nodes() + 1);
    if (r == 0) { snprintf(label, ln, "Link lights"); snprintf(value, vn, "%s", light_name[F.lights]); return true; }
    if (r == 1) { snprintf(label, ln, "If link is lost"); snprintf(value, vn, "%s", F.carry_on ? "Carry on" : "Restart"); return true; }
    return false;
}
static void live_text(int row, char *label, size_t ln, char *value, size_t vn)
{
    if (row == 0) { snprintf(label, ln, "Link play"); snprintf(value, vn, "On"); }
    else if (row == 1) { snprintf(label, ln, "This cabinet"); snprintf(value, vn, "%d", cfg.id + 1); }
    else if (row == 2) { snprintf(label, ln, "Cabinets in set"); snprintf(value, vn, "%d", cfg.nodes); }
    else if (row < 3 + cfg.nodes) {
        const int n = row - 3;
        snprintf(label, ln, n == cfg.id ? "Cabinet %d (this PC)" : "Cabinet %d", n + 1);
        const char *p = cfg.peers;
        for (int k = 0; p && k < n; k++) { p = strchr(p, ','); if (p) p++; }
        if (p) { size_t e = strcspn(p, ","); snprintf(value, vn, "%.*s", (int)(e < 15 ? e : 15), p); }
        else snprintf(value, vn, "127.0.0.1 (this PC)");
    } else { snprintf(label, ln, "Start-up wait"); snprintf(value, vn, "%d s", cfg.gather_ms / 1000); }
}
void link_page_text(int row, char *label, size_t ln, char *value, size_t vn)
{
    if (extra_text(row, label, ln, value, vn)) return;
    if (from_cmdline) { live_text(row, label, ln, value, vn); return; }
    if (row == 0) { snprintf(label, ln, "Link play"); snprintf(value, vn, "%s", F.on ? "On" : "Off"); }
    else if (row == 1) {
        snprintf(label, ln, "This cabinet");
        const int c = F.cab_auto ? own_cabinet(false) : F.cabinet;
        if (!F.cab_auto) snprintf(value, vn, "%d", F.cabinet);
        else if (c > 0) snprintf(value, vn, "Auto (%d)", c);
        else snprintf(value, vn, "Auto (not in list)");
    }
    else if (row == 2) { snprintf(label, ln, "Cabinets in set"); snprintf(value, vn, "%d", F.cabinets); }
    else if (row < 3 + F.cabinets) {
        const int n = row - 3, me = F.cab_auto ? own_cabinet(false) : F.cabinet;
        snprintf(label, ln, n + 1 == me ? "Cabinet %d (this PC)" : "Cabinet %d", n + 1);
        if (edit_row == row) snprintf(value, vn, "%s_", edit_buf);
        else snprintf(value, vn, "%s", F.addr[n][0] ? F.addr[n] : "(Enter to type)");
    } else { snprintf(label, ln, "Start-up wait"); snprintf(value, vn, "%d s", F.wait); }
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void link_page_change(int row, int dir)
{
    const int r = row - (3 + page_nodes() + 1);
    if (r == 0) { F.lights = (F.lights + (dir < 0 ? 6 : 1)) % 7; cfg_save(); return; }
    if (r == 1) { F.carry_on = !F.carry_on; lost_carry_on = F.carry_on; cfg_save(); return; }
    if (from_cmdline) return;                                          /* the launcher's settings: nothing here to change */
    if (row == 0) F.on = !F.on;
    else if (row == 1) {                                               /* Auto, 1 .. N, round */
        int i = F.cab_auto ? 0 : F.cabinet;
        i = (i + (dir < 0 ? F.cabinets : 1)) % (F.cabinets + 1);
        F.cab_auto = i == 0; if (i) F.cabinet = i;
        F.set_id = true;
    }
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
    line("This PC: %s", n ? s : "no network address found");
    if (from_cmdline) {
        line("Set by the test launcher (Cabinet %d of %d).", cfg.id + 1, cfg.nodes);
        line("For a real cabinet, use 'play as a cabinet'.");
    } else {
        if (edit_row >= 0) line("Type the address, then Enter (Esc cancels).");
        line("Same list on every cabinet. Used at next start.");
        if (F.cab_auto) line("Auto: the number is this PC's place in the list.");
        if (F.set_id) line("The game's own ID will follow at next start.");
    }
    if (on) {
        char who[40] = ""; int j = 0;
        for (int m = 0; m < cfg.nodes; m++) if (lnk_members() >> m & 1) j += snprintf(who + j, sizeof who - (size_t)j, "%s%d", j ? "+" : "", m + 1);
        if (lnk_state() == LNK_ALONE) line("Status: Cabinet %d, NOT linked (started late)", cfg.id + 1);
        else if (lnk_members() == 1u << cfg.id) line("Status: Cabinet %d, playing solo", cfg.id + 1);
        else line("Status: Cabinet %d, linked with %s", cfg.id + 1, who);
        char hb[160]; hearing(hb, sizeof hb, false);
        line("%s", hb);
        if (*setup_msg_shown) line("SET-UP DIFFERS: %s", setup_msg_shown);
        line("The game keeps running while this menu is open.");
    } else if (auto_fail) line("Status: NOT linked (this PC is not in the list)");
    else line("Status: link off");
    if (F.lights) line("Lights: green OK, amber one-way, red silent, grey out");
}

/* THE LINK LIGHTS (Network page > Link lights): one light per cabinet of the set, at the bottom left of the screen. This cabinet is
 * blue and flickers as it sends; another is green (both ways) or amber (one way: it is heard here but does not hear this one, or its
 * set-up differs) and flickers as its messages arrive, red when nothing comes from it, grey when it is not in this set. */
bool link_lights_on(void) { return on && F.lights && lnk_state() != LNK_GATHERING; }
int  link_lights_pos(void) { return link_lights_on() ? F.lights : 0; }
int link_lights(link_light *out)
{
    if (!link_lights_on()) return 0;
    const uint32_t h = lnk_hear_mask(), by = lnk_heard_by_mask(), mem = lnk_state() == LNK_LINKED ? lnk_members() : 0;
    int k = 0;
    for (int c = 0; c < cfg.nodes && k < LINK_LIGHTS_MAX; c++, k++) {
        link_light *l = &out[k];
        l->cabinet = c + 1; l->lit = true;
        if (c == cfg.id) { l->kind = LINK_LIGHT_SELF; l->lit = (lnk_get_stats()->sent / 8) & 1; continue; }
        char a[16]; int ago; uint32_t pk = 0;
        lnk_peer_info(c, a, &ago, &pk);
        bool same = true; const int nodes = lnk_peer_setup(c, &same);
        if (!(mem >> c & 1)) l->kind = LINK_LIGHT_OUT;
        else if (!(h >> c & 1)) l->kind = LINK_LIGHT_SILENT;
        else { l->kind = (by >> c & 1) && (!nodes || (nodes == cfg.nodes && same)) ? LINK_LIGHT_GOOD : LINK_LIGHT_ONEWAY; l->lit = (pk / 12) & 1; }
    }
    return k;
}
