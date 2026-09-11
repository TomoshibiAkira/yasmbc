/* title.c - Title screen routines (from title-screen.asm) */

#include "screen/screen.h"
#include "constants/globals.h"
#include "constants/defs.h"
#include "enemy/enemy.h"
#include "misc.h"
#include "assets.h"
#include <string.h>

enum { TITLE_SCREEN_COPY_LENGTH = 0x13a };
static const uint8_t *g_TitleScreenRLE;
static uint8_t MushroomIconData[8];

int Title_LoadRom(void) {
    size_t n = 0;
    g_TitleScreenRLE = Assets_Load("tables/title_rle.bin", &n);
    if (!g_TitleScreenRLE || n < TITLE_SCREEN_COPY_LENGTH)
        return -1;
    if (Assets_Copy("tables/mushroom_icon.bin", MushroomIconData,
                    sizeof(MushroomIconData)))
        return -1;
    return 0;
}

/* ========================================================================
 * DRAW TITLE SCREEN
 * Copies RLE data (already in VRAM buffer entry format) into g_VRAM_Buffer1
 * ======================================================================== */

void DrawTitleScreen(void) {
    uint8_t i;

    if (g_OperMode != TITLE_SCREEN_MODE) {
        g_OperMode_Task++;
        return;
    }

    /* DrawTitleScreen copies exactly $13a bytes ($0300-$0439) from the
     * TitleScreenDataOffset source; the in-band $00 remains the VRAM stream
     * terminator, while the bytes after it still affect aliased RAM. */
    memcpy(g_VRAM_Buffer1, g_TitleScreenRLE, TITLE_SCREEN_COPY_LENGTH);
    g_VRAM_Buffer1[TITLE_SCREEN_COPY_LENGTH] = 0x00;

    /* DrawTitleScreen (engine/screen/routine/title-screen.asm:9-27) stores
     * the ROM stream through the physical pointer $0300, for 256 bytes and
     * then another $3a bytes.  The C renderer keeps a logical copy in
     * g_VRAM_Buffer1, but the tail of the physical copy aliases object RAM:
     * $0400, $0416, and $0433 are the canonical owners exposed by the state
     * stream.  Mirror those exact ROM offsets so the title copy preserves
     * the same RAM aliasing without disturbing the buffer stream consumed by
     * the SDL NMI flush. */
    g_Player_X_MF2 = g_TitleScreenRLE[0x100];       /* physical $0400 */
    g_Player_YMF_Dummy = g_TitleScreenRLE[0x116];   /* physical $0416 */
    g_Player_Y_MoveForce = g_TitleScreenRLE[0x133]; /* physical $0433 */
    Enemy_SetTitleBufferAliases(g_TitleScreenRLE);
    for (i = 0; i < 3; ++i)
        Misc_SetBubbleYmf(i, g_TitleScreenRLE[0x12c + i]); /* $042c-$042e */

    /* Set buffer transfer control */
    g_VRAM_Buffer1_Offset = 0;
    g_VRAM_Buffer_AddrCtrl = 0x05;

    Screen_IncTask();
}

/* ========================================================================
 * CLEAR BUFFERS AND DRAW ICON
 * Clears VRAM buffer and draws mushroom cursor next to selected option
 *
 * Mushroom icon data (from MushroomIconData in disassembly):
 *   addr=$2249, tile=$CE (mushroom) at position of selected player option
 *   1-player: icon at column position 3 (next to "1 PLAYER GAME")
 *   2-player: icon at column position 5 (next to "2 PLAYER GAME")
 * ======================================================================== */

void DrawMushroomIcon(void) {
    uint8_t y;

    /* MushroomIconData (titlescreen.asm) is copied with its one-byte
     * VRAM_Buffer1_Offset prefix at VRAM_Buffer1-1. */
    g_VRAM_Buffer1_Offset = MushroomIconData[0];
    for (y = 0; y < 7; y++)
        g_VRAM_Buffer1[y] = MushroomIconData[y + 1];
    if (g_NumberOfPlayers != 0) {
        g_VRAM_Buffer1[3] = 0x24;
        g_VRAM_Buffer1[5] = 0xce;
    }
}

void ClearBuffersDrawIcon(void) {
    uint8_t i;

    if (g_OperMode != TITLE_SCREEN_MODE) {
        g_OperMode_Task++;
        return;
    }

    /* ClearBuffersDrawIcon (main.asm:335-344) clears both physical pages,
     * $0300-$03ff and $0400-$04ff.  Buffer1 is kept as the SDL logical
     * stream, while Buffer2 and these named globals represent the physical
     * aliases exposed by the state projection. */
    memset(g_VRAM_Buffer1, 0, 512);
    memset(g_VRAM_Buffer2, 0, 0xbf);
    g_VRAM_Buffer1_Offset = 0;
    g_VRAM_Buffer2_Offset = 0;
    g_Player_X_MF2 = 0;          /* physical $0400 */
    g_Player_YMF_Dummy = 0;      /* physical $0416 */
    g_Player_Y_MoveForce = 0;    /* physical $0433 */
    Enemy_ClearTitleBufferAliases();
    for (i = 0; i < 3; ++i)
        Misc_SetBubbleYmf(i, 0); /* physical $042c-$042e */

    /* Draw the three-row MushroomIconData entry. */
    DrawMushroomIcon();

    g_VRAM_Buffer1_Offset = 0;
    g_VRAM_Buffer_AddrCtrl = 0x05;

    Screen_IncTask();
}
