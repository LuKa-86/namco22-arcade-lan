/* eng_nk_style.h -- the look every Nuklear menu in the tree shares (the engine's eng_ui.c, Rave Racer's rr_ui.c, Prop Cycle's ui_menu.c).
 * Include AFTER nuklear.h. Header-only because each of those menus is its own Nuklear implementation. */
#ifndef ENG_NK_STYLE_H
#define ENG_NK_STYLE_H
#include <stdbool.h>
/* THE SELECTED ROW / TAB (GitHub #43): Nuklear's default marks the selected item with a grey a few shades off the window's, hard to see
 * (worst with a dark desktop theme). Amber with black text; a hovered row is lifted a little. */
static inline void eng_nk_style_highlight(struct nk_context *c)
{
    struct nk_style_selectable *s = &c->style.selectable;
    s->normal_active  = nk_style_item_color(nk_rgb(240, 170, 40));
    s->hover_active   = nk_style_item_color(nk_rgb(255, 196, 80));
    s->pressed_active = nk_style_item_color(nk_rgb(255, 196, 80));
    s->text_normal_active = s->text_hover_active = s->text_pressed_active = nk_rgb(0, 0, 0);
    s->hover = nk_style_item_color(nk_rgb(70, 70, 78));
}
/* A BUTTON THAT IS THE SELECTED ROW: the File page's rows are buttons only (no label column), so the keyboard's place was invisible --
 * "the arrow keys do nothing there". Drawn in the same amber as a selected row. Our fork. */
static inline nk_bool eng_nk_button_sel(struct nk_context *c, const char *text, bool sel)
{
    if (!sel) return nk_button_label(c, text);
    struct nk_style_button b = c->style.button;
    b.normal = nk_style_item_color(nk_rgb(240, 170, 40));
    b.hover = b.active = nk_style_item_color(nk_rgb(255, 196, 80));
    b.text_normal = b.text_hover = b.text_active = nk_rgb(0, 0, 0);
    return nk_button_label_styled(c, &b, text);
}
/* VOLUME PRESETS on the keyboard (the Mute / 25% / 50% / 100% buttons are mouse only): the next preset up or down from v, Enter cycles */
static inline int eng_vol_preset(int v, int dir)
{
    static const int p[4] = { 0, 25, 50, 100 };
    if (dir > 0) { for (int i = 0; i < 4; i++) if (p[i] > v) return p[i]; return 100; }
    if (dir < 0) { for (int i = 3; i >= 0; i--) if (p[i] < v) return p[i]; return 0; }
    for (int i = 0; i < 4; i++) if (p[i] > v) return p[i];
    return 0;                                                   /* Enter at 100%: round to Mute */
}
#endif
