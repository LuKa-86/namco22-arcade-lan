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

bool link_host_on(void) { return on; }
int  link_host_id(void) { return cfg.id; }

int link_host_prescan(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--link")) return atoi(argv[i + 1]);
    return 0;
}

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

void link_host_gather(const char *title, SDL_Window *w)
{
    if (!on) return;
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
    fprintf(stderr, "[LINK] frame %u  down %u (now %u)  cabinets on %02X (next %d)  chip tx %u rx %u queue-drop %u unacked %u  net sent %u recv %u lost %u bad %u  | %s\n",
            frame, down_frames, down_now, lnk_alive_mask(), lnk_next_hop() + 1, c->tx, c->rx, c->rx_dropped, c->rx_unacked, n->sent, n->recv, n->lost_seq, n->bad, g);
}

void link_host_frame(uint32_t frame, bool headless)
{
    if (!on) return;
    if (headless) pace();
    if (G->good) {
        if (G->good()) { if (++up_run >= 120) was_up = true; down_now = 0; }
        else if ((up_run = 0), was_up) { down_frames++; if (++down_now == 1) fprintf(stderr, "[LINK] frame %u: the game lost the link\n", frame); }
    }
    if (frame % 300 == 0) report(frame);
}
