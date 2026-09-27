/*
 * link_net.h -- the cable between cabinets: UDP on a LAN (or 127.0.0.1 for several copies on one PC). See docs/LINK_NOTES.md.
 *
 * Every cabinet has a number 0..nodes-1 and listens on UDP port base_port + number. The original machines were a ring of optical
 * fibre (cabinet n sends to n+1, the last back to 0); here each frame goes to the NEXT CABINET THAT IS SWITCHED ON, so a cabinet
 * that is off or has crashed is stepped over instead of breaking the ring. Who is on is learned from a small "hello" every cabinet
 * sends to every other one four times a second; a cabinet not heard for a second is taken as off.
 *
 * Nothing here needs the internet: the addresses are given (--link-peers) or all 127.0.0.1.
 */
#ifndef ENG_LINK_NET_H
#define ENG_LINK_NET_H
#include <stdint.h>
#include <stdbool.h>

#define LNK_MAX_NODES 8
#define LNK_MAX_WORDS 512                   /* the longest frame the chip may send */

typedef struct {
    int      id, nodes, base_port;
    const char *peers;                      /* "ip0,ip1,..." (index = cabinet number); NULL = all 127.0.0.1 */
    const char *tag;                        /* the game ("TW"): cabinets of another game are ignored */
    int      delay_ms, jitter_ms, loss_pct; /* test only: a worse network than the real one */
} lnk_cfg;

typedef struct {
    uint32_t sent, recv, bad, lost_seq, no_peer, delayed, dropped_test;
} lnk_stats;

bool     lnk_open(const lnk_cfg *cfg);
void     lnk_close(void);
bool     lnk_on(void);
void     lnk_send(const uint16_t *w, int n);          /* one chip frame to the next cabinet that is on */
int      lnk_recv(uint16_t *w, int max);              /* the next frame received (its word count), 0 = none; also handles the hellos */
uint32_t lnk_alive_mask(void);                        /* bit n = cabinet n is on (this one included) */
int      lnk_next_hop(void);                          /* where a frame goes now, -1 = nobody else is on */
const lnk_stats *lnk_get_stats(void);
#endif
