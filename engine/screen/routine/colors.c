/* colors.c - Color palette routines (from colors.asm) */

#include "screen/screen.h"
#include "assets.h"
#include <string.h>

/* Forward declaration - GetBGPlayerColor falls through to GetPlayerColors */
void GetPlayerColors(void);

/* Area palettes by type */
static uint8_t g_AreaPalette[4];
static uint8_t g_BGColorCtrl_Addr[4];

/* Get palette based on area type */
void GetAreaPalette(void) {
    /* Select appropriate palette based on area type */
    uint8_t palette = g_AreaPalette[g_AreaType & 0x03];
    
    /* Store palette offset into VRAM buffer control */
    g_VRAM_Buffer_AddrCtrl = palette;
    
    Screen_IncTask();
}

/* Get background and player colors.
 * NES: GetBGPlayerColor falls through directly into GetPlayerColors
 * (no rts between them in colors.asm). This writes player sprite palette
 * and background color to the VRAM buffer every time this task runs.
 */
void GetBGPlayerColor(void) {
    /* Check if background color control is set */
    if (g_BackgroundColorCtrl != 0) {
        /* Use bg color ctrl to get palette */
        uint8_t palette = g_BGColorCtrl_Addr[(g_BackgroundColorCtrl - 4) & 0x03];
        g_VRAM_Buffer_AddrCtrl = palette;
    }

    /* Increment to next subtask */
    Screen_IncTask();

    /* NES fall-through: always call GetPlayerColors after this task.
     * When BackgroundColorCtrl=0, GetPlayerColors uses AreaType to index
     * BackgroundColors[], writing the correct backdrop (e.g. $22 for overworld)
     * to $3F10 (mirrors $3F00).
     */
    GetPlayerColors();
}

/* Get alternate palette for mushroom levels */
void GetAlternatePalette1(void) {
    /* Check for mushroom level style */
    if (g_AreaStyle == 0x01) {
        g_VRAM_Buffer_AddrCtrl = 0x0B;  /* Mushroom palette */
    }
    
    Screen_IncTask();
}

static uint8_t PlayerColors[12];
static uint8_t BackgroundColors[8];

int Colors_LoadRom(void) {
    if (Assets_Copy("tables/player_colors.bin", PlayerColors, sizeof(PlayerColors)))
        return -1;
    if (Assets_Copy("tables/background_colors.bin", BackgroundColors, sizeof(BackgroundColors)))
        return -1;
    if (Assets_Copy("tables/area_palette.bin", g_AreaPalette, sizeof(g_AreaPalette)))
        return -1;
    if (Assets_Copy("tables/bg_color_ctrl_addr.bin", g_BGColorCtrl_Addr,
                    sizeof(g_BGColorCtrl_Addr)))
        return -1;
    return 0;
}

/* Get player colors (Mario/Luigi/Fiery) - used by intermediate.c */
void GetPlayerColors(void) {
    uint8_t x = g_VRAM_Buffer1_Offset;
    uint8_t y = 0;

    /* Check which player is on screen */
    if (g_CurrentPlayer != 0) {
        y = 4;  /* Luigi offset */
    }

    /* Check for fiery status (overrides player-specific palette) */
    if (g_PlayerStatus == PLAYER_STATUS_FIRE) {
        y = 8;  /* Fiery offset */
    }

    /* Copy 4 player color bytes to buffer */
    for (int i = 0; i < 4; i++) {
        g_VRAM_Buffer1[x + 3 + i] = PlayerColors[y + i];
    }

    /* Set background color based on BackgroundColorCtrl or AreaType */
    uint8_t bg_idx;
    if (g_BackgroundColorCtrl != 0) {
        bg_idx = g_BackgroundColorCtrl;
    } else {
        bg_idx = g_AreaType;
    }
    g_VRAM_Buffer1[x + 3] = BackgroundColors[bg_idx];

    /* Set sprite palette address $3F10 */
    g_VRAM_Buffer1[x] = 0x3F;
    g_VRAM_Buffer1[x + 1] = 0x10;
    g_VRAM_Buffer1[x + 2] = 0x04;  /* Length */
    g_VRAM_Buffer1[x + 7] = 0x00;  /* Null terminator */

    /* Advance buffer offset by 7 */
    g_VRAM_Buffer1_Offset = x + 7;
}
