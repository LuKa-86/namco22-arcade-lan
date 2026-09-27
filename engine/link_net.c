/* link_net.c -- see link_net.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#define sock_close closesocket
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define sock_close close
#endif
#include "link_net.h"

#define MAGIC     0x4E32324Cu               /* "N22L" */
#define PROTO     1
#define T_FRAME   1
#define T_HELLO   2                         /* + 1 byte: bit 0 = ready (the game is running its link) */
#define T_BYE     3                         /* this cabinet is closing: step over it at once */
#define HELLO_MS  25                        /* a hello to every cabinet this often ... */
#define ALIVE_MS  120                       /* ... and one not heard for this long is off: the game gives up after 8 frames (133 ms) */
#define HDR       16                        /* magic 4, proto 1, type 1, src 1, dst 1, seq 4, tag 4 */

static struct {
    bool on;
    lnk_cfg cfg;
    sock_t s;
    struct sockaddr_in addr[LNK_MAX_NODES];
    uint32_t tag;
    uint64_t heard[LNK_MAX_NODES];          /* SDL ticks of the last packet from each cabinet (0 = never) */
    bool     ready[LNK_MAX_NODES], ready_self;   /* its game forwards frames: only then is it part of the ring */
    uint32_t seq_to[LNK_MAX_NODES], last_seq[LNK_MAX_NODES];   /* numbered per destination: a gap = packets lost on that path */
    uint64_t next_hello;
    lnk_stats st;
    uint32_t rng;
    struct { uint64_t due; int dst, len; uint8_t b[HDR + 2 * LNK_MAX_WORDS]; } held[256];   /* test delay */
    int nheld;
    bool warned_tag;
} L;

static uint64_t now_ms(void) { return SDL_GetTicks64(); }
static uint32_t rnd(void) { L.rng ^= L.rng << 13; L.rng ^= L.rng >> 17; L.rng ^= L.rng << 5; return L.rng; }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static bool set_addr(int n, const char *host)
{
    memset(&L.addr[n], 0, sizeof L.addr[n]);
    L.addr[n].sin_family = AF_INET;
    L.addr[n].sin_port = htons((uint16_t)(L.cfg.base_port + n));
    if (inet_pton(AF_INET, host, &L.addr[n].sin_addr) != 1) { fprintf(stderr, "[LINK] cabinet %d: '%s' is not an IPv4 address\n", n, host); return false; }
    return true;
}

bool lnk_open(const lnk_cfg *cfg)
{
    L.cfg = *cfg;
    if (L.cfg.nodes < 2 || L.cfg.nodes > LNK_MAX_NODES || L.cfg.id < 0 || L.cfg.id >= L.cfg.nodes) {
        fprintf(stderr, "[LINK] cabinet %d of %d: out of range\n", L.cfg.id, L.cfg.nodes); return false;
    }
    L.tag = 0;
    for (int i = 0; i < 4 && cfg->tag[i]; i++) L.tag |= (uint32_t)(uint8_t)cfg->tag[i] << (24 - 8 * i);
#ifdef _WIN32
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd)) { fprintf(stderr, "[LINK] WSAStartup failed\n"); return false; }
#endif
    /* the addresses: --link-peers "a,b,c,d" (a missing or empty entry = 127.0.0.1) */
    char list[512]; snprintf(list, sizeof list, "%s", cfg->peers ? cfg->peers : "");
    char *p = list;
    for (int n = 0; n < L.cfg.nodes; n++) {
        char *comma = p ? strchr(p, ',') : NULL;
        if (comma) *comma = 0;
        if (!set_addr(n, (p && *p) ? p : "127.0.0.1")) return false;
        p = comma ? comma + 1 : NULL;
    }
    L.s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (L.s == SOCK_BAD) { fprintf(stderr, "[LINK] no socket\n"); return false; }
    struct sockaddr_in me; memset(&me, 0, sizeof me);
    me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_ANY); me.sin_port = htons((uint16_t)(L.cfg.base_port + L.cfg.id));
    if (bind(L.s, (struct sockaddr *)&me, sizeof me) != 0) {
        fprintf(stderr, "[LINK] cannot listen on UDP port %d (is another copy already cabinet %d?)\n", L.cfg.base_port + L.cfg.id, L.cfg.id + 1);
        sock_close(L.s); return false;
    }
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(L.s, FIONBIO, &nb);
    BOOL off = FALSE; DWORD ret = 0;                   /* a send to a closed port must not make the next receive fail */
    WSAIoctl(L.s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &ret, NULL, NULL);
#else
    fcntl(L.s, F_SETFL, fcntl(L.s, F_GETFL, 0) | O_NONBLOCK);
#endif
    L.rng = 0x9E3779B9u ^ (uint32_t)L.cfg.id * 7919u ^ (uint32_t)now_ms();
    L.heard[L.cfg.id] = UINT64_MAX;                   /* this cabinet is always on */
    L.on = true;
    fprintf(stderr, "[LINK] cabinet %d of up to %d, UDP port %d%s\n", L.cfg.id + 1, L.cfg.nodes, L.cfg.base_port + L.cfg.id,
            cfg->peers ? "" : ", all cabinets on this PC (127.0.0.1)");
    if (cfg->delay_ms || cfg->jitter_ms || cfg->loss_pct)
        fprintf(stderr, "[LINK] TEST network: +%d ms, jitter %d ms, %d%% lost\n", cfg->delay_ms, cfg->jitter_ms, cfg->loss_pct);
    return true;
}

bool lnk_on(void) { return L.on; }
const lnk_stats *lnk_get_stats(void) { return &L.st; }

static bool alive(int n, uint64_t t) { return L.heard[n] == UINT64_MAX || (L.ready[n] && L.heard[n] && t - L.heard[n] < ALIVE_MS); }

uint32_t lnk_alive_mask(void)
{
    uint64_t t = now_ms(); uint32_t m = 0;
    for (int n = 0; n < L.cfg.nodes; n++) if (alive(n, t)) m |= 1u << n;
    return m;
}

int lnk_next_hop(void)
{
    uint64_t t = now_ms();
    for (int k = 1; k < L.cfg.nodes; k++) { int n = (L.cfg.id + k) % L.cfg.nodes; if (alive(n, t)) return n; }
    return -1;
}

static void raw_send(int dst, const uint8_t *b, int len)
{
    sendto(L.s, (const char *)b, len, 0, (struct sockaddr *)&L.addr[dst], sizeof L.addr[dst]);
}

/* the test impairments apply to frames only (hellos go straight out) */
static void send_frame_packet(int dst, const uint8_t *b, int len)
{
    if (L.cfg.loss_pct && (int)(rnd() % 100) < L.cfg.loss_pct) { L.st.dropped_test++; return; }
    int d = L.cfg.delay_ms + (L.cfg.jitter_ms ? (int)(rnd() % (unsigned)(L.cfg.jitter_ms + 1)) : 0);
    if (d <= 0 || L.nheld >= 256) { raw_send(dst, b, len); return; }
    L.held[L.nheld].due = now_ms() + (uint64_t)d; L.held[L.nheld].dst = dst; L.held[L.nheld].len = len;
    memcpy(L.held[L.nheld].b, b, (size_t)len); L.nheld++; L.st.delayed++;
}

static void release_held(void)
{
    uint64_t t = now_ms();
    for (int i = 0; i < L.nheld; ) {
        if (L.held[i].due <= t) {
            raw_send(L.held[i].dst, L.held[i].b, L.held[i].len);
            L.held[i] = L.held[--L.nheld];             /* jitter may reorder frames, as a bad network would */
        } else i++;
    }
}

static int header(uint8_t *b, int type, int dst)
{
    put32(b, MAGIC); b[4] = PROTO; b[5] = (uint8_t)type; b[6] = (uint8_t)L.cfg.id; b[7] = (uint8_t)dst;
    put32(b + 8, ++L.seq_to[dst]); put32(b + 12, L.tag);
    return HDR;
}

void lnk_send(const uint16_t *w, int n)
{
    if (!L.on) return;
    int dst = lnk_next_hop();
    if (dst < 0) { L.st.no_peer++; return; }
    if (n > LNK_MAX_WORDS) n = LNK_MAX_WORDS;
    uint8_t b[HDR + 2 * LNK_MAX_WORDS];
    int len = header(b, T_FRAME, dst);
    for (int i = 0; i < n; i++) { b[len++] = (uint8_t)(w[i] >> 8); b[len++] = (uint8_t)w[i]; }
    send_frame_packet(dst, b, len);
    L.st.sent++;
}

static void hellos(void)
{
    uint64_t t = now_ms();
    if (t < L.next_hello) return;
    L.next_hello = t + HELLO_MS;
    uint8_t b[HDR + 1];
    for (int n = 0; n < L.cfg.nodes; n++) if (n != L.cfg.id) { header(b, T_HELLO, n); b[HDR] = L.ready_self; raw_send(n, b, HDR + 1); }
}

int lnk_recv(uint16_t *w, int max)
{
    if (!L.on) return 0;
    hellos();
    release_held();
    for (;;) {
        uint8_t b[HDR + 2 * LNK_MAX_WORDS + 16];
        struct sockaddr_in from; socklen_t fl = sizeof from;
        int len = (int)recvfrom(L.s, (char *)b, sizeof b, 0, (struct sockaddr *)&from, &fl);
        if (len < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAECONNRESET) continue;
#endif
            return 0;                                          /* nothing waiting */
        }
        if (len < HDR || get32(b) != MAGIC || b[4] != PROTO || b[6] >= L.cfg.nodes || b[6] == L.cfg.id) { L.st.bad++; continue; }
        if (get32(b + 12) != L.tag) {
            if (!L.warned_tag) { fprintf(stderr, "[LINK] cabinet %d is running another game: ignored\n", b[6] + 1); L.warned_tag = true; }
            continue;
        }
        int src = b[6];
        L.heard[src] = now_ms();
        uint32_t seq = get32(b + 8);
        /* lost = gaps in the numbering; a late (reordered) packet fills its gap back in */
        if (L.last_seq[src] && seq > L.last_seq[src] + 1 && seq - L.last_seq[src] < 1000) L.st.lost_seq += seq - L.last_seq[src] - 1;
        else if (seq < L.last_seq[src] && L.last_seq[src] - seq < 1000 && L.st.lost_seq) L.st.lost_seq--;
        if (seq > L.last_seq[src] || L.last_seq[src] - seq > 100000) L.last_seq[src] = seq;   /* (a restarted cabinet counts from 1 again) */
        if (b[5] == T_HELLO) { if (len > HDR) L.ready[src] = b[HDR] & 1; continue; }
        if (b[5] == T_BYE) { L.heard[src] = 0; L.ready[src] = false; continue; }
        if (b[5] != T_FRAME) continue;
        int n = (len - HDR) / 2;
        if (n > max) n = max;
        for (int i = 0; i < n; i++) w[i] = (uint16_t)(b[HDR + 2 * i] << 8 | b[HDR + 2 * i + 1]);
        L.st.recv++;
        return n;
    }
}

void lnk_set_ready(bool r) { L.ready_self = r; }

void lnk_close(void)
{
    if (!L.on) return;
    uint8_t b[HDR];
    for (int k = 0; k < 3; k++)                               /* a goodbye (three times: it is UDP) */
        for (int n = 0; n < L.cfg.nodes; n++) if (n != L.cfg.id) { header(b, T_BYE, n); raw_send(n, b, HDR); }
    sock_close(L.s); L.on = false;
}
