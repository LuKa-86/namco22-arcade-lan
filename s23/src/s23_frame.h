/* s23_frame.h -- what one frame's picture needs, snapshotted at the start of vertical blank so the picture can be composed on another thread
 * while the CPUs run on (src/s23_render.c fills it, src/s23_text.c and the polygon fill read it) */
#ifndef S23_FRAME_H
#define S23_FRAME_H
#include <stdint.h>
#include "s23_c435.h"
typedef struct {
    uint8_t  c404[0x40];               /* the C404's registers 0x00-0x1F (big-endian words) */
    uint8_t  gamma[3][256];            /* the C404's GAMMA RAM: R / G / B curves at 0x200 / 0x400 / 0x600 (256 words each, low bytes) --
                                        * the last stage of the picture; MAME never applies it, so its picture is darker than the board's */
    uint32_t pens[0x8000];             /* the palette as 0x00RRGGBB */
    uint8_t  text[0x20000];            /* the C361's character RAM (0x06800000) and tile map (0x0681E000) */
    uint16_t linexscroll[1024], yscroll;
    uint16_t sram[0x20000];            /* the C412 SRAM: the stencil tiles */
    s23_render_list list;              /* the render list to draw */
    uint16_t pcz[256];                 /* the C412 PCZ half in use (0x0C000000 bit 2): fog level i is reached at depth pcz[i] (0..0x1FFF) */
    int cz_on;                         /* C412 flags bit 0: the depth fog on */
} s23_frame_state;
void s23_text_layer(const s23_frame_state *fs, uint16_t *mix, uint8_t *pri);
void s23_text_mix(const s23_frame_state *fs, uint32_t *rgba, const uint16_t *mix, const uint8_t *pri, int prival);
#endif
