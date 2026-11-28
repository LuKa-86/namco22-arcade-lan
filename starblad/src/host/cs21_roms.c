/*
 * cs21_roms.c -- the first run: Starblade's ROMs straight out of MAME's zips (the shared engine/romzip.c, as every game does).
 *
 * The set is three zips: cybsled.zip (the game), namcoc67.zip (c67.bin, the C67 DSP's internal ROM) and namcoc68.zip (c68.bin, the
 * C68 I/O MCU's internal ROM); a MAME set that carries the two BIOS files inside cybsled.zip works too. Every chip is taken by name and
 * size, checked against the zip's own CRC and written into the ROM folder (extracted/). The zips are looked for in the current folder,
 * roms/, the program's folder and its roms/. The CY1 files under cybsleda/ in the zip are not used. tools/setup_roms.py stays the
 * developer's way (it also checks every chip against MAME's CRC32 and builds the program images Ghidra reads; the game needs neither).
 */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "romzip.h"
#include "s21_host.h"

static const eng_rom_t cs_roms[] = {          /* MAME -listxml starblad / namcoc67 / namcoc68 (CRCs: docs/ROM_CHECKSUMS.md) */
    { "st2_mpu.mpru", 0x80000 },   { "st2_mpl.mprl", 0x80000 },      /* master 68000 (ST2) */
    { "st1-sp-u.bin", 0x40000 },   { "st1-sp-l.bin", 0x40000 },      /* slave 68000 */
    { "st1-snd0.bin", 0x20000 },                                      /* 6809 sound CPU */
    { "st1-obj0.bin", 0x80000 },   { "st1-obj1.bin", 0x80000 },   { "st1-obj2.bin", 0x80000 },   { "st1-obj3.bin", 0x80000 },   /* C355 sprites */
    { "st1-data-u.bin", 0x20000 }, { "st1-data-l.bin", 0x20000 },    /* the shared data ROM */
    { "st1-pt0-h.bin", 0x80000 },  { "st1-pt0-u.bin", 0x80000 },  { "st1-pt0-l.bin", 0x80000 },    /* point ROM */
    { "st1-pt1-h.bin", 0x80000 },  { "st1-pt1-u.bin", 0x80000 },  { "st1-pt1-l.bin", 0x80000 },
    { "st1-voi0.bin", 0x80000 },   { "st1-voi1.bin", 0x80000 },   { "st1-voi2.bin", 0x80000 },   { "st1-voi3.bin", 0x80000 },  /* C140 samples */
    { "c67.bin", 0x2000 },                                            /* namcoc67.zip */
    { "c68.bin", 0x8000 },                                            /* namcoc68.zip */
};
#define N_ROMS ((int)(sizeof cs_roms / sizeof cs_roms[0]))

const char *cs21_rom_setup(const char *romdir, int window)
{
    if (eng_romzip_missing(romdir, cs_roms, N_ROMS) && !strcmp(romdir, "extracted") && !eng_romzip_missing("roms", cs_roms, N_ROMS))
        return "roms";                                   /* chips unzipped loose into roms/ work too */
    if (!eng_romzip_missing(romdir, cs_roms, N_ROMS)) return romdir;
    static const char *const zips[] = { "starblad.zip", "namcoc67.zip", "namcoc68.zip" };
    char err[512], *base = SDL_GetBasePath();
    const bool ok = eng_romzip_autosetup(romdir, base, zips, 3, cs_roms, N_ROMS, err, sizeof err);
    SDL_free(base);
    if (ok) return romdir;
    fprintf(stderr, "Starblade needs its ROMs: %s\n", err);
    if (window) {
        char msg[1024];
        snprintf(msg, sizeof msg, "Starblade needs its ROMs.\n\nPut starblad.zip, namcoc67.zip and namcoc68.zip (the MAME ROM sets) in the "
                 "\"roms\" folder next to this program, then start it again.\n\n(%s)", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Starblade", msg, NULL);
    }
    return NULL;
}
