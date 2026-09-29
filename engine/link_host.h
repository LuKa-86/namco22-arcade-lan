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
 * Without any --link option, the settings saved by the menu's Network page (link.cfg beside the program) are used.
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
    int  (*game_id)(void);                   /* the game's own cabinet ID (its test menu's, 0-based; NULL = unknown): two linked cabinets
                                              * with the same one cannot work, so it is compared across the set and warned about */
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

/* THE NETWORK PAGE of the Esc menu (both menus draw it from these): its rows are saved in link.cfg beside the program, which a game
 * started without --link uses. An address row is typed: Enter starts, then link_page_type() takes each key until it returns false. */
int  link_page_rows(void);
bool link_page_has_value(int row);
void link_page_text(int row, char *label, size_t ln, char *value, size_t vn);
void link_page_change(int row, int dir);                       /* dir 0 = Enter, -1 / +1 = Left / Right */
bool link_page_editing(void);
bool link_page_type(SDL_Scancode sc);
void link_page_notes(void (*line)(const char *fmt, ...));
#endif
