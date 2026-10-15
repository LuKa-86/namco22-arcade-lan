/*
 * tc2_c422.c -- THE TWIN-CABINET LINK: Namco's C422 ("RS422 networking (twin cabinet) communication controller"), our own model, and its
 * cable carried over UDP to the other cabinet's PC. Our fork (the LAN link project): neither MAME nor the original port model the data path
 * -- MAME knows only the interrupt -- so this is read off the game's own driver (ROM 0x8000BB6C send, 0x8000BD24 receive, 0x8000BEAC the
 * interrupt handler, 0x80119700 the power-on interrupt check, 0x8008DCE8 the test menu's register display).
 *
 * THE CHIP as the driver uses it (16-bit registers at 0x06400000, 16 KB of RAM at 0x06200000 holding one byte in the low half of each word):
 *   +0  STATUS   bit 1 = a message has arrived, bit 2 = the send is done. The driver clears bit 1 by writing it 0.
 *   +2  MASK     interrupt mask, a set bit masks: the driver unmasks bits 1 / 2 to wait for them (0xFFFB in the power-on check, 0x000F
 *                masks everything). The interrupt (IP5) is up while (STATUS & ~MASK & 6) != 0.
 *   +6  CONTROL  3 while the driver sets a send up, 2 to let it go (1 then 0 in the power-on check): a send starts when bit 0 drops.
 *   +A  COUNT    bytes to send (the driver sends a message in pieces of up to 255); reads what is left, 0 = idle.
 *   +C  RX       the receive ring's write position (12 bits): the next word of RAM+0x2000 the chip fills.
 *   +E  TX       the send position (12 bits, a word of RAM+0x0000), advanced as the bytes go.
 *   +4, +8       stored and read back (the test menu shows +8).
 * A received message ends with the word's bit 8 set on its last byte (the driver searches up to 8 words back from RX for it, then reads
 * the length from the two bytes before it and expects 0x5A at the start). The chip cannot know where a message ends except that the line
 * goes quiet, so here: the sender marks the end of a burst -- a send that finishes with no new one started within two byte times.
 *
 * THE CABLE: every piece of a burst goes to the other PC as a UDP datagram ("C422", a sequence number, the end-of-burst flag, the bytes);
 * the receiver drops duplicates and old ones and writes the rest into its ring as they come (polled a few times a frame).
 *
 *   TC2_LINK=<side>:<this PC's port>:<other PC>:<its port>     side L (left / red) or R (right / blue) -- the I/O board's Link ID switch
 *   e.g. TC2_LINK=L:23001:127.0.0.1:23002 and TC2_LINK=R:23002:127.0.0.1:23001 (two copies on one PC)
 *   TC2_C422LOG=1   every register access and every burst, to the log
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define closesocket close
#endif
#include "tc2_host.h"
#include "tc2_game.h"

uint8_t *tc2_vram_ptr(uint32_t p);
uint64_t tc2_cycles_now(void);
extern uint32_t rr_frame;

#define RAM_TX  0x06200000u
#define RAM_RX  0x06202000u
#define BYTE_CYCLES 1694u                 /* one byte on the line: 1 Mbit/s, 10 bits a byte, at the 169.344 MHz count -- a guess, unmeasured */
#define POLL_CYCLES 200000u               /* the socket is read about 14 times a frame */

static uint16_t reg[8];
static int logon = -1;
static int side_right;                    /* the Link ID switch (src/tc2_h8.c reads it) */
int tc2_c422_link_right(void) { return side_right; }

/* the send in progress */
static int tx_busy;                       /* bytes are going out */
static uint64_t tx_next;                  /* the cycle the next byte has gone */
static uint64_t idle_at;                  /* a burst's end: no new send by this cycle */
static int burst_open;                    /* bytes have gone since the last end-of-burst */

/* the cable */
static sock_t sk = BAD_SOCK;
static struct sockaddr_in peer;
static uint32_t tx_seq, rx_seq_last = 0xFFFFFFFFu;
static uint8_t pend[1500]; static int npend;   /* this burst's bytes not yet sent */
static uint64_t poll_at;
static unsigned long n_bursts_out, n_bursts_in, n_bytes_out, n_bytes_in, n_drops;

static uint16_t ram_rd(uint32_t a) { const uint8_t *m = tc2_vram_ptr(a); return m ? (uint16_t)(m[0] << 8 | m[1]) : 0; }
static void ram_wr(uint32_t a, uint16_t v) { uint8_t *m = tc2_vram_ptr(a); if (m) { m[0] = (uint8_t)(v >> 8); m[1] = (uint8_t)v; } }

static void irq_update(void) { tc2_irq_set(TC2_SRC_C422, (reg[0] & ~reg[1] & 6) != 0); }

static void open_link(void)
{
    static int done; if (done) return; done = 1;
    logon = getenv("TC2_C422LOG") != NULL;
    const char *e = getenv("TC2_LINK"); if (!e || !*e) return;
    char side = 'L', host[256] = ""; unsigned lport = 0, pport = 0;
    if (sscanf(e, "%c:%u:%255[^:]:%u", &side, &lport, host, &pport) != 4) { fprintf(stderr, "[C422] TC2_LINK=%s: expected side:port:host:port\n", e); return; }
    side_right = side == 'R' || side == 'r';
#ifdef _WIN32
    { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
#endif
    sk = socket(AF_INET, SOCK_DGRAM, 0);
    if (sk == BAD_SOCK) { fprintf(stderr, "[C422] no socket\n"); return; }
    struct sockaddr_in me; memset(&me, 0, sizeof me);
    const int loop = !strcmp(host, "127.0.0.1") || !strcmp(host, "localhost");   /* both copies on this PC: this PC only (no firewall question) */
    me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(loop ? INADDR_LOOPBACK : INADDR_ANY); me.sin_port = htons((uint16_t)lport);
    if (bind(sk, (struct sockaddr *)&me, sizeof me) != 0) { fprintf(stderr, "[C422] cannot use port %u\n", lport); closesocket(sk); sk = BAD_SOCK; return; }
#ifdef _WIN32
    { u_long nb = 1; ioctlsocket(sk, FIONBIO, &nb); }
#else
    fcntl(sk, F_SETFL, fcntl(sk, F_GETFL) | O_NONBLOCK);
#endif
    struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) { fprintf(stderr, "[C422] cannot find %s\n", host); closesocket(sk); sk = BAD_SOCK; return; }
    peer = *(struct sockaddr_in *)res->ai_addr; peer.sin_port = htons((uint16_t)pport); freeaddrinfo(res);
    fprintf(stderr, "[C422] link cable: this cabinet is %s (Link ID), port %u -> %s:%u\n", side_right ? "RIGHT / blue" : "LEFT / red", lport, host, pport);
}

/* a piece of a burst onto the cable (end = the line goes quiet after it) */
static void flush(int end)
{
    if (sk == BAD_SOCK) { npend = 0; return; }
    if (!npend && !end) return;
    uint8_t b[12 + sizeof pend];
    memcpy(b, "C422", 4);
    b[4] = (uint8_t)(tx_seq >> 24); b[5] = (uint8_t)(tx_seq >> 16); b[6] = (uint8_t)(tx_seq >> 8); b[7] = (uint8_t)tx_seq; tx_seq++;
    b[8] = (uint8_t)end; b[9] = 0; b[10] = (uint8_t)(npend >> 8); b[11] = (uint8_t)npend;
    memcpy(b + 12, pend, (size_t)npend);
    sendto(sk, (const char *)b, 12 + npend, 0, (struct sockaddr *)&peer, sizeof peer);
    if (end) n_bursts_out++;
    n_bytes_out += (unsigned long)npend;
    npend = 0;
}

/* bytes from the cable into the receive ring */
static void receive(const uint8_t *d, int n, int end)
{
    unsigned rx = reg[6] & 0xFFF;
    for (int i = 0; i < n; i++) {
        const int last = end && i == n - 1;
        ram_wr(RAM_RX + 2 * rx, (uint16_t)(d[i] | (last ? 0x100 : 0)));
        rx = (rx + 1) & 0xFFF;
    }
    if (end && n == 0) {                                     /* an end with no bytes: mark the last byte written */
        const unsigned l = (rx - 1) & 0xFFF; ram_wr(RAM_RX + 2 * l, (uint16_t)(ram_rd(RAM_RX + 2 * l) | 0x100));
    }
    reg[6] = (uint16_t)((reg[6] & ~0xFFFu) | rx);
    n_bytes_in += (unsigned long)n;
    if (end) { n_bursts_in++; reg[0] |= 2; irq_update(); }
}

static void poll_cable(void)
{
    if (sk == BAD_SOCK) return;
    for (int k = 0; k < 64; k++) {
        uint8_t b[2048]; struct sockaddr_in from; socklen_t fl = sizeof from;
        const int r = (int)recvfrom(sk, (char *)b, sizeof b, 0, (struct sockaddr *)&from, &fl);
        if (r < 12) return;
        if (memcmp(b, "C422", 4)) continue;
        const uint32_t seq = (uint32_t)b[4] << 24 | (uint32_t)b[5] << 16 | (uint32_t)b[6] << 8 | b[7];
        const int end = b[8] & 1, n = b[10] << 8 | b[11];
        if (12 + n > r) continue;
        if (rx_seq_last != 0xFFFFFFFFu && (int32_t)(seq - rx_seq_last) <= 0) continue;     /* a duplicate or a late one */
        if (rx_seq_last != 0xFFFFFFFFu && seq != rx_seq_last + 1) n_drops += seq - rx_seq_last - 1;
        rx_seq_last = seq;
        receive(b + 12, n, end);
        if (logon) fprintf(stderr, "[C422] f%u in  %3d bytes%s (rx at %03X)\n", rr_frame, n, end ? " END" : "", reg[6] & 0xFFF);
    }
}

static void tx_start(uint64_t now)
{
    if ((reg[5] & 0xFF) == 0) return;
    tx_busy = 1; tx_next = now + BYTE_CYCLES; idle_at = 0;
    if (logon) fprintf(stderr, "[C422] f%u send %u bytes from %03X\n", rr_frame, reg[5] & 0xFF, reg[7] & 0xFFF);
}

/* time passed (src/tc2_main.c tc2_host_step): bytes go out, a quiet line ends the burst, the cable is read */
void tc2_c422_tick(void)
{
    open_link();
    const uint64_t now = tc2_cycles_now();
    while (tx_busy && now >= tx_next) {
        const unsigned tx = reg[7] & 0xFFF;
        if (npend < (int)sizeof pend) pend[npend++] = (uint8_t)ram_rd(RAM_TX + 2 * tx);
        reg[7] = (uint16_t)((reg[7] & ~0xFFFu) | ((tx + 1) & 0xFFF));
        burst_open = 1;
        reg[5] = (uint16_t)((reg[5] & 0xFF00u) | ((reg[5] - 1) & 0xFF));
        if ((reg[5] & 0xFF) == 0) {
            tx_busy = 0; reg[0] |= 4; idle_at = tx_next + 2 * BYTE_CYCLES;
            flush(0);                                        /* this piece now; the end mark follows when the line stays quiet */
            irq_update();
        } else tx_next += BYTE_CYCLES;
    }
    if (!tx_busy && burst_open && idle_at && now >= idle_at) {
        burst_open = 0; idle_at = 0; flush(1);
        if (logon) fprintf(stderr, "[C422] f%u burst end\n", rr_frame);
    }
    if (now >= poll_at) { poll_at = now + POLL_CYCLES; poll_cable(); }
}

int tc2_c422_read(uint32_t p, uint32_t *v)
{
    open_link();
    const unsigned k = (p - 0x06400000u) >> 1 & 7;
    *v = reg[k];
    if (logon && k != 0) fprintf(stderr, "[C422] f%u read  +%X = %04X\n", rr_frame, k * 2, reg[k]);
    return 1;
}

int tc2_c422_write(uint32_t p, uint32_t v)
{
    open_link();
    const unsigned k = (p - 0x06400000u) >> 1 & 7;
    const uint16_t old = reg[k];
    if (logon) fprintf(stderr, "[C422] f%u write +%X = %04X\n", rr_frame, k * 2, (unsigned)(uint16_t)v);
    switch (k) {
    case 0: reg[0] = (uint16_t)((reg[0] & v & 6u) | (v & ~6u)); break;     /* bits 1 / 2: written 0 clears them, never sets */
    case 3: reg[3] = (uint16_t)v; if ((old & 1) && !(v & 1)) tx_start(tc2_cycles_now()); break;
    case 5: reg[5] = (uint16_t)v; reg[0] &= (uint16_t)~4u;                   /* a new count: the last send's "done" is old news */
            if (!(reg[3] & 1)) tx_start(tc2_cycles_now()); break;
    default: reg[k] = (uint16_t)v; break;
    }
    irq_update();
    return 1;
}

void tc2_c422_load_reg(unsigned k, uint16_t v) { if (k < 8) reg[k] = v; }

void tc2_c422_report(void)
{
    if (sk == BAD_SOCK && !n_bytes_out) return;
    fprintf(stderr, "[C422] link: %lu bursts / %lu bytes out, %lu bursts / %lu bytes in, %lu datagrams lost\n",
            n_bursts_out, n_bytes_out, n_bursts_in, n_bytes_in, n_drops);
}
