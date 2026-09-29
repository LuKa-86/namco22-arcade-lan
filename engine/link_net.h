/*
 * link_net.h -- the cable between cabinets: UDP on a LAN (or 127.0.0.1 for several copies on one PC). See docs/LINK_NOTES.md.
 *
 * Every cabinet has a number 0..nodes-1 and listens on UDP port base_port + number. The original machines were a ring of optical
 * fibre (cabinet n sends to n+1, the last back to 0), and the games look for the other cabinets when they are switched on: a board
 * that was not there at power-on is left out until the whole set is switched off and on again. This keeps that rule:
 *
 *   GATHER   at start-up, before the game runs, every cabinet says hello to the others. The lowest-numbered cabinet present decides:
 *            once every cabinet is there (or the wait has run out -- 60 s), it opens a SESSION: an id and the fixed list of members.
 *            The members start their games together, as boards switched on together do.
 *   SESSION  frames go only between members of this session; each frame goes to the next member that is still on (a member not
 *            heard for 120 ms is stepped over, so one crashed PC does not stop the others). Nobody is ever added.
 *   ALONE    a cabinet that starts while a session is already running (started late, or restarted after a crash) is not let in:
 *            it plays on its own until every cabinet is restarted.
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
    int      gather_ms;                     /* how long to wait at start-up for missing cabinets */
    int      delay_ms, jitter_ms, loss_pct; /* test only: a worse network than the real one */
} lnk_cfg;

typedef struct {
    uint32_t sent, recv, bad, lost_seq, no_peer, delayed, dropped_test, foreign;
} lnk_stats;

enum { LNK_GATHERING, LNK_LINKED, LNK_ALONE };

bool     lnk_open(const lnk_cfg *cfg);
void     lnk_close(void);                             /* also tells the others this cabinet is going */
bool     lnk_on(void);
int      lnk_gather(void);                            /* start-up: call until it is not LNK_GATHERING (every few ms) */
int      lnk_state(void);
uint32_t lnk_present_mask(void);                      /* GATHER: who is here so far (bit n = cabinet n+1) */
uint32_t lnk_members(void);                           /* SESSION: the cabinets that started together */
int      lnk_gather_left_ms(void);                    /* GATHER: how long until the set starts without the missing ones */
void     lnk_send(const uint16_t *w, int n);          /* one chip frame to the next member that is on */
int      lnk_recv(uint16_t *w, int max);              /* the next frame received (its word count), 0 = none; also handles the hellos */
uint32_t lnk_alive_mask(void);                        /* bit n = member n is on now (this one included) */
int      lnk_next_hop(void);                          /* where a frame goes now, -1 = nobody else is on */
void     lnk_set_game_id(int id);                     /* the game's own cabinet ID (its test menu's), told to the others in the hellos */
int      lnk_peer_game_id(int n);                     /* cabinet n's game ID, -1 = not known */
const lnk_stats *lnk_get_stats(void);
int      lnk_local_addresses(char out[][16], int max);   /* this PC's IPv4 addresses (not 127.x), for the menu: how many */
#endif
