/* s21_cabinet.h -- Starblade's cabinet lamps, vibration, curved screen and pointer setting (s21_cabinet.c) */
#ifndef S21_CABINET_H
#define S21_CABINET_H
#include <stdbool.h>
#include "eng_ui.h"
void s21_cabinet_init(void);                          /* after the settings file is loaded */
const eng_ui_page *s21_cabinet_page(void);            /* the menu's Cabinet page */
bool s21_cabinet_hide_pointer(void);
void s21_cabinet_overlay(int x, int y, int w, int h); /* every frame, over the picture (ss22_host_game.overlay) */
#endif
