/* s23_game.h -- ONE GAME = ONE TABLE. The System 23 host in tc2/src is shared by every System 23 / Evolution 2 game (HARD RULE 3: settings and
 * small hooks, never copies). Time Crisis 2 fills it in src/tc2_game.c, Crisis Zone in ../crszone/src/csz_game.c; each binary links one.
 * Everything a game differs in that the host must know is here: the ROM files, the board (System 23 or Evolution 2), the clocks, which
 * CPU interrupt pin each device drives. */
#ifndef S23_GAME_H
#define S23_GAME_H
#include <stdint.h>
#include <stddef.h>
#include "eng/romzip.h"

/* the board's interrupt SOURCES (MAME namcos23.cpp MAIN_*_IRQ); each game's table says which Cause bit (IP2..IP7) a source drives */
enum { S23_SRC_VBLANK, S23_SRC_C361, S23_SRC_SUB, S23_SRC_C435, S23_SRC_C422, S23_SRC_C450, S23_SRC_C451, S23_SRC_RS232, S23_SRC_TIMER, S23_SRC_N };
#define S23_IP(n) (0x400u << (n))                 /* the CPU's IRQn pin = Cause.IP(n+2); the CP0 timer is IP7 = S23_IP(5) */

typedef struct {
    const char *title;                            /* "Time Crisis 2": windows, messages */
    const char *tag;                              /* "tc2": the cfg / backup / log file names */
    const char *window_title;                     /* "Time Crisis II" */
    const char *mame_set;                         /* "timecrs2": the set name the output protocol announces (recoil hooks key on it) */
    const char *zip_help;                         /* what the player must supply, for the missing-ROM message */
    const char *const *zips; int nzips;           /* the MAME zips searched (a split set keeps a device ROM in its own zip) */
    const eng_rom_t *chips; int nchips;           /* every ROM file: name, size, CRC (src/eng/romzip.c) */

    int twin;                                     /* 1 = a TWIN-CABINET game (Time Crisis 2: Left/Red + Right/Blue on the C422 RS-422 link): the menu's
                                                   * Cabinet choice (the I/O board's Link ID) and online play (src/s23_link.c) */
    int evo2;                                     /* 1 = System 23 EVOLUTION 2: the chips at +0x10000000, 29-bit decode, C450 DMA, IRQ status regs */
    uint32_t clock;                               /* the main CPU, Hz (cycles as MAME charges them) */
    uint32_t px_num, px_den;                      /* pixel clocks (25.6 MHz) per main cycle, exactly */
    uint32_t sub_div;                             /* main cycles per sub-CPU (H8/3002) state */
    uint32_t io_num, io_den;                      /* I/O board (H8/3334) states = main cycles * io_num / io_den */
    uint32_t c352_div;                            /* main cycles per C352 sample (88200 Hz) */
    uint32_t ip[S23_SRC_N];                       /* the Cause bit each source drives (0 = not wired) */
    int irq_prompt;                               /* 1 = an interrupt a device raises mid-slice is polled at once (MAME takes it at the next instruction);
                                                   * 0 = at the end of the slice, as Time Crisis 2's gates were measured with. Crisis Zone waits on ~550
                                                   * C450 DMA interrupts a frame: a slice's delay each (up to S23_SLICE instructions) halved its frame rate */

    /* the program images, made from the ROM files at start-up (tools/make_prog.py writes the same for the translators) */
    uint32_t boot_size;                           /* the boot ROM as the CPU sees it, ending at 0x1FFFFFFF (4 MB System 23, 8 MB Evolution 2) */
    int (*rom_boot)(const char *dir, uint8_t *dst);
    int (*rom_sub)(const char *dir, uint8_t *dst, size_t n);
    int (*rom_io)(const char *dir, uint8_t *dst, size_t n);
    uint32_t io_rom_size;                         /* how much of the I/O board's program its H8 maps */
    int no_direct_buf;                            /* 1 = the C435 direct buffer (S23 0x0CC00000) is not decoded: Crisis Zone (MAME's crszone map leaves
                                                   * 0x1CC00000 unmapped: a full-screen black quad written there covers the namco / GASHIN logos) */
    int (*zbias_scale)(int model);                /* a model's polygons' z-bias word multiplied by this when sorting (NULL / 0 = 1, as MAME). Gunmen Wars:
                                                   * the blue suit-up's cheek plate 0x0AD7 (bias -0x320) sits on the same surface as the dark head layer;
                                                   * at x1 the two near-tie and the plate blinks out (black), x4 holds it as in the arcade footage. NOT global:
                                                   * x4 everywhere pulled the chest doors' frames (bias -5000) over the closed chest plate */
    int depth_default;                            /* Display > Depth when tc2.cfg has no choice yet: 1 = "exact" (Time Crisis 2), 0 = the arcade's order.
                                                   * Crisis Zone 0: with "exact" its hotel scene (frame 2280) loses the sky (the background colour shows where
                                                   * coverage proves the sky polygons drawn) and gains black bands top and bottom -- cause not found yet (NOT
                                                   * transparent texels: System 23 has none, only the stencil, which the pre-pass honours) */
    uint16_t c417_status;                         /* the C417 status read's fixed bits (MAME: System 23 0x008E; crszone_state::c417_status_r 0) | test mode << 15 */
    uint16_t gun_x0, gun_xw, gun_y0, gun_yh;      /* the light gun's port range (MAME JVS_SCREEN_POSITION_INPUT_X1/Y1 PORT_MINMAX): first value + width */
    uint8_t io_6001_held;                         /* bits of the I/O board's 0x6001 that read as PRESSED always (MAME namco_tss_io_device::in_r: bit 3 =
                                                   * BUTTON5, which crszone's port declares active-low and unused -> pressed; "Motor test shows NG if this is not pressed") */
    uint8_t sub_p7;                               /* what the sub-CPU reads on port 7 (MAME: timecrs2 0, crszone 0x80) */
    const char *data_rom[2][2];                   /* the data ROMs: [half][high, low] byte-interleaved */
    int npt; const char *pt_rom[4][2];            /* the point ROM chip pairs [high, low] */
    const char *ccrl, *ccrh, *cg[4];              /* texture tile maps and tiles */
    const char *wave[2];                          /* the C352's samples: low, high */
    int recoil_hold;                              /* 1 = 0x6002 bit 0 stays on WHILE the gun fires (Crisis Zone's machine gun: measured on a player's
                                                   * session, on for the whole burst), 0 = a 3-frame pulse per shot (Time Crisis 2) */
    int io_analog_out;                            /* 1 = the I/O board has MAME's namco_csz1 analog output at 0x6003 (Crisis Zone): published as analog0 */
    /* the SPEECH, on its own gain (Audio > Voice volume, GitHub #46): a C352 voice in speech_voices, or a key-on from a sample bank in
     * speech_bank_lo..speech_bank_hi (hi 0 = no range), is mixed as speech. Measured per game with C352_KEYLOG on a recorded session */
    uint32_t speech_voices;
    uint8_t speech_bank_lo, speech_bank_hi;
    /* the game's OWN hardware beside the System 23 chips (Gunmen Wars: the GMEN board): asked first for an address no System 23 chip
     * decodes (the 28-bit physical address); 1 = handled. NULL = none */
    int no_smooth;                                /* 1 = motion smoothing (in-between frames) off: Gunmen Wars crashed Intel's GL driver with it (cause not found yet) */
    int io_asca;                                  /* 1 = the I/O board is an ASCA (Gunmen Wars' ASCA-5): switches at 0x6000-3, controls on its A/D */
    int (*mem_read)(uint32_t p, int size, uint32_t *v);
    int (*mem_write)(uint32_t p, int size, uint32_t v);
    void (*list_hook)(void *list);
    int (*hide_crosshair)(void);                  /* optional: 1 = the crosshair is not drawn now (Gunmen Wars: the FPS layout, where the mouse turns
                                                   * and the crosshair's spot means nothing -- Options page "Crosshair (FPS)") */
    int (*hud_wide_ok)(const void *list);        /* the widescreen HUD shift applies to this list? (NULL = always; Gunmen Wars: only with the in-game HUD up) */
    void (*menu_pages)(void);                     /* optional: add the game's own pages to the Esc menu (eng_ui_add_page), after Controls */
    const void *input;                            /* optional: the game's own controls (an ss22_input_game: rows, keys, the Controls page notes);
                                                   * NULL = the light-gun cabinet's (src/s23_input.c: trigger, pedal, reload) */
    int mounted_gun;                              /* 1 = a mounted gun, not a light gun (Gunmen Wars): the Display page leaves out the light gun's
                                                   * shot flash and the recoil / lamp outputs */
    int photo_page;                               /* the texture page (polygon tbase) whose texels the GMEN camera board supplies, not the ROM
                                                   * (Gunmen Wars: 0x1F000, the players' photos); 0 = none. Drawn from s23_photo_tex (src/s23_render.c) */
    int no_xycull;                                /* 1 = keep translucent polygons with far off-screen corners (the Crisis Zone smoke rule, src/s23_render.c
                                                   * finish(), would drop Gunmen Wars' near water: the river went black under the camera) */
    int imm_right;                                /* 1 = C435 0.2 without bit 8 is t = m2 * transpose(immediate) (Gunmen Wars; src/s23_c435.c) */
    int model_fov_max;                            /* 1 = MODELS use the largest projection scale among the frame's clip sets, immediates keep their own
                                                   * (Gunmen Wars, against the footage: the game opens each frame with a 90-degree set (320) its world is
                                                   * NOT drawn with -- fighters, team intro, the select card need the 60-degree set (554) it sends last;
                                                   * the suit-up close-ups open with their own narrower set (1955) and need that one) */
    float znear_model;                            /* optional: near plane (C435 units) for models in the 3D view; 0 = the default */                /* optional: adjust the frame's render list (a s23_render_list *) before it is drawn */
    struct { uint32_t pc, flag; } idle[4];         /* optional: the game's WAIT LOOPS (src/s23_cpu.c rr_tick): a poll from pc (the loop branch's delay
                                                   * slot) while the word at flag (a main-RAM address) is still 0 skips to the next event --
                                                   * Gunmen Wars waits for its vblank flag so (19% of the main thread, measured) */
    const struct s23_link_ops *link;              /* optional: the game's OWN cabinet link in place of the C422 twin-cabinet one (src/s23_c422.c);
                                                   * Gunmen Wars: the GMEN boards' IEEE 1394 bus (gunwars/src/gw_link.c). NULL = the C422 */
} s23_game_t;

/* A GAME'S OWN CABINET LINK (s23_game.link): the engine calls these where it would call the C422's (src/s23_c422.c) */
typedef struct s23_link_ops {
    const char *title;                            /* the power-on wait screen's first line */
    void (*gate)(int (*pump)(const char *l1, const char *l2));   /* power-on: wait for the other cabinets (pump: the window, or NULL) */
    void (*tick)(void);                           /* time passed (each slice): the cable read, kept in step */
    void (*report)(void);                         /* closing: the totals to the log, the others told */
    int  (*linked)(void);                         /* 1 = in a linked session (no pause: the others would lose this one) */
    int  (*lights)(int kind[2], int lit[2]);      /* the link lights as s23_c422_lights; NULL = none */
} s23_link_ops;

extern const s23_game_t s23_game;                 /* the one this binary was built for */
#endif
