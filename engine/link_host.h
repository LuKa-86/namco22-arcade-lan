/*
 * link_host.h -- LINK PLAY in a game's host (the Super 22 host engine/ss22_run.c, Rave Racer's raverace/src/rr_main.c): the command
 * line, the start-up gathering, the link chip's slice and the log. The chip is engine/c139.c, the network engine/link_net.c; see
 * docs/LINK_NOTES.md.
 *
 *   --link N                 this is cabinet N (1..8) of a linked set
 *   --link-cabinets M        how many cabinets the set can have (default: the --link-peers list's length, else 4)
 *   --link-peers A,B,C,D     the cabinets' IPv4 addresses, in cabinet order (default: all on this PC, 127.0.0.1)
 *   --link-port P            UDP port of cabinet 1 (cabinet N uses P + N - 1; default 22220)
 *   --link-wait S            at start-up, wait up to S seconds for the other cabinets (default 60), then start without the missing ones
 *   --link-test D:J:L[:S]    TEST: every frame sent D ms late, plus up to J ms of jitter, L% lost; S = this copy's clock S ppm fast (headless)
 *   --mute                   no sound (the other copies when several run on one PC)
 */
#ifndef ENG_LINK_HOST_H
#define ENG_LINK_HOST_H
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

typedef struct {
    const char *tag;                         /* "TW", "RR": the log's and the network's name for the game */
    void (*raise_irq)(void);                 /* the chip wants its interrupt: raise the board's SCI line (if the game enabled it) */
    void (*deliver_irqs)(void);              /* take the pending interrupts now */
    void (*report)(char *buf, int n);        /* the game's own view of the link, for the log (NULL = none) */
    bool (*good)(void);                      /* the game thinks its link works (NULL = unknown) */
} link_host_game;

extern bool g_link_mute;

int  link_host_prescan(int argc, char **argv);                  /* the cabinet number (1..) if --link is given, else 0: before the log opens */
bool link_host_arg(int argc, char **argv, int *i);             /* one of the options above (consumed; *i advanced) */
bool link_host_open(const link_host_game *g);                  /* after the options: the network; false = it could not start */
bool link_host_on(void);
int  link_host_id(void);                                       /* 0-based cabinet number */
void link_host_gather(const char *title, SDL_Window *win);     /* start-up: wait for the others, the window's title saying who is missing */
void link_host_slice(void);                                    /* every scheduler slice */
void link_host_frame(uint32_t frame, bool headless);           /* every video frame: real-time pacing (headless), the log */
#endif
