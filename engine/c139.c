/* c139.c -- see c139.h. */
#include <stdio.h>
#include <string.h>
#include "c139.h"
#include "link_net.h"

#define RING_BASE 0x1000u                   /* the receive ring: words 0x1000..0x1FFF of the buffer */
#define RING_MASK 0x0FFFu
#define QMAX      58                        /* frames waiting for the game (the real ring buffer's 0x1000 words hold about 58 of 70);
                                             * beyond this the oldest go (a stalled PC catches up on fresh data) */
#define ACK_SLICES 3                        /* a frame the game has not acknowledged by then is overwritten by the next, as on the real chip */

enum { ST_RX = 0x0002 };                    /* status bit 1: a frame was received */
enum { CT_RX_MASK = 0x0002, CT_TX_MASK = 0x0004 };   /* control (register 1) */

static struct {
    uint8_t *buf;
    uint16_t reg[8];
    uint16_t status;
    bool     tx_busy;
    int      tx_len;
    uint16_t tx[LNK_MAX_WORDS];
    int      unacked;
    uint16_t q[QMAX][LNK_MAX_WORDS]; int qlen[QMAX]; int qhead, qn;
    c139_stats st;
} C;

static uint16_t wr_(unsigned w) { w &= 0x1FFF; return (uint16_t)(C.buf[2 * w] << 8 | C.buf[2 * w + 1]); }
static void ww_(unsigned w, uint16_t v) { w &= 0x1FFF; C.buf[2 * w] = (uint8_t)(v >> 8); C.buf[2 * w + 1] = (uint8_t)v; }

void c139_init(uint8_t *buf)
{
    memset(&C, 0, sizeof C);
    C.buf = buf;
    C.reg[6] = RING_BASE;
}

bool c139_on(void) { return lnk_on(); }
const c139_stats *c139_get_stats(void) { return &C.st; }

bool c139_irq(void)
{
    if (!lnk_on()) return false;
    const uint16_t ct = C.reg[1];
    return ((C.status & ST_RX) && !(ct & CT_RX_MASK)) || (!C.tx_busy && !(ct & CT_TX_MASK));
}

bool c139_eof_writes(void) { return lnk_on() && (C.reg[2] & 2); }

uint16_t c139_reg_r(unsigned unit)
{
    unit &= 7;
    if (!lnk_on()) return unit == 0 ? 0x0004 : 0;              /* MAME's namcos22_sci_r */
    switch (unit) {
    case 0: return (uint16_t)(0x0004 | C.status);
    case 5: return C.tx_busy ? C.reg[5] : 0;                   /* non-zero while the transmitter is busy */
    default: return C.reg[unit];
    }
}

/* a send: the words are taken now (the game has finished writing them); they leave at the next slice */
static void start_tx(void)
{
    unsigned n = C.reg[5] & 0x1FF, src = C.reg[7];
    if (!n) return;
    if (C.tx_busy) C.st.tx_restart++;
    if (n > LNK_MAX_WORDS) n = LNK_MAX_WORDS;
    for (unsigned i = 0; i < n; i++)
        C.tx[i] = (src & RING_BASE) ? wr_(RING_BASE | ((src + i) & RING_MASK)) : wr_(src + i);
    C.tx_len = (int)n;
    C.tx_busy = true;
}

void c139_reg_w(unsigned unit, uint16_t data, uint16_t mask)
{
    unit &= 7;
    if (!lnk_on()) return;                                     /* MAME: writes do nothing */
    uint16_t v = (uint16_t)((C.reg[unit] & ~mask) | (data & mask));
    switch (unit) {
    case 0: C.status = 0; C.unacked = 0; break;               /* the handler's acknowledge */
    case 5: C.reg[5] = v; start_tx(); break;
    case 6: C.reg[6] = (uint16_t)(RING_BASE | (v & RING_MASK)); break;
    default: C.reg[unit] = v; break;
    }
}

static void deliver(void)
{
    int n = C.qlen[C.qhead]; const uint16_t *w = C.q[C.qhead];
    C.qhead = (C.qhead + 1) % QMAX; C.qn--;
    unsigned p = C.reg[6] & RING_MASK;
    for (int i = 0; i < n; i++) ww_(RING_BASE | ((p + (unsigned)i) & RING_MASK), w[i]);
    C.reg[6] = (uint16_t)(RING_BASE | ((p + (unsigned)n) & RING_MASK));
    C.status |= ST_RX;
    C.unacked = 0;
    C.st.rx++;
}

bool c139_tick(bool new_slice)
{
    if (!lnk_on()) return false;
    if (C.tx_busy) { lnk_send(C.tx, C.tx_len); C.tx_busy = false; C.st.tx++; }
    if (new_slice && (C.status & ST_RX) && ++C.unacked > ACK_SLICES) { C.status &= (uint16_t)~ST_RX; C.st.rx_unacked++; }
    for (;;) {                                                 /* everything the network has, into the queue */
        int slot = (C.qhead + C.qn) % QMAX;
        if (C.qn == QMAX) { C.qhead = (C.qhead + 1) % QMAX; C.qn--; C.st.rx_dropped++; slot = (C.qhead + C.qn) % QMAX; }
        int n = lnk_recv(C.q[slot], LNK_MAX_WORDS);
        if (n <= 0) break;
        C.qlen[slot] = n; C.qn++;
    }
    if (!(C.status & ST_RX) && C.qn) deliver();
    return c139_irq();
}
