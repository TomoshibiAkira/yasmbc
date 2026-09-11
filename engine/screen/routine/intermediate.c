/* intermediate.c - Intermediate screen setup (from setup-intermediate.asm
 * and main.asm:269-311 DisplayIntermediate / DisplayTimeUp,
 * main.asm:511-521 ResetSpritesAndScreenTimer)
 */

#include <stddef.h>
#include <string.h>

#include "screen/screen.h"
#include "assets.h"
#include "constants/globals.h"
#include "constants/defs.h"
#include "level/level.h"
#include "colors.h"

static uint8_t GameText[155];
static uint8_t GameTextOffsets[10];
static uint8_t LuigiName[5];
static uint8_t IntermediatePlayerData[6];
static uint8_t IntermediatePlayerGraphics[208];

int Intermediate_LoadRom(void) {
    static const char *const message_assets[7] = {
        "tables/top_status_bar.bin", "tables/world_lives_display.bin",
        "tables/two_player_time_up.bin", "tables/one_player_time_up.bin",
        "tables/two_player_game_over.bin", "tables/one_player_game_over.bin",
        "tables/warp_zone_welcome.bin"
    };
    uint8_t starts[7];
    uint8_t cursor = 0;
    unsigned i;

    if (Assets_Copy("tables/luigi_name.bin", LuigiName, sizeof(LuigiName)) ||
        Assets_Copy("tables/intermediate_player.bin", IntermediatePlayerData,
                    sizeof(IntermediatePlayerData)) ||
        Assets_Copy("tables/player_graphics.bin", IntermediatePlayerGraphics,
                    sizeof(IntermediatePlayerGraphics)))
        return -1;
    for (i = 0; i < 7; i++) {
        size_t length = 0;
        const uint8_t *message = Assets_Load(message_assets[i], &length);
        starts[i] = cursor;
        if (!message || length > sizeof(GameText) - cursor)
            return -1;
        memcpy(GameText + cursor, message, length);
        cursor = (uint8_t)(cursor + length);
    }
    if (cursor != sizeof(GameText))
        return -1;
    GameTextOffsets[0] = starts[0];
    GameTextOffsets[1] = starts[0];
    GameTextOffsets[2] = starts[1];
    GameTextOffsets[3] = starts[1];
    GameTextOffsets[4] = starts[2];
    GameTextOffsets[5] = starts[3];
    GameTextOffsets[6] = starts[4];
    GameTextOffsets[7] = starts[5];
    GameTextOffsets[8] = starts[6];
    GameTextOffsets[9] = starts[6];
    return 0;
}

/* ========================================================================
 * SETUP INTERMEDIATE (task 1)
 * Saves bg color control / player status, sets black background,
 * outputs player colors, restores. From setup-intermediate.asm.
 * ======================================================================== */

void SetupIntermediate(void) {
    uint8_t savedBGColor = g_BackgroundColorCtrl;
    uint8_t savedPlayerStatus = g_PlayerStatus;

    g_BackgroundColorCtrl = 0x02;
    g_PlayerStatus = 0x00;

    GetPlayerColors();

    g_PlayerStatus = savedPlayerStatus;
    g_BackgroundColorCtrl = savedBGColor;

    Screen_IncTask();
}

/* ========================================================================
 * WriteGameText (main.asm:418-507).
 * Copies a message entry stream to VRAM_Buffer1 starting at index 0 and
 * applies message-specific fixups (lives, world, level numbers, and the
 * warp-zone number group selected by text values 4-6).
 * ======================================================================== */

void Screen_WriteGameText(uint8_t textnum) {
    uint8_t y = (uint8_t)(textnum << 1);
    uint8_t x;
    uint8_t i = 0;

    /* WriteGameText (main.asm:418-466) indexes GameTextOffsets with 2*text
     * number, then selects the 1P vs 2P stream for TIME UP / GAME OVER. */
    if (y >= 4) {
        if (y >= 8)
            y = 8;
        if (g_NumberOfPlayers == 0)
            y++;
    }
    x = GameTextOffsets[y];
    while (GameText[x] != 0xff) {
        g_VRAM_Buffer1[i] = GameText[x];
        x++;
        i++;
    }
    g_VRAM_Buffer1[i] = 0x00;

    /* CheckPlayerName (main.asm:468-489) is reached for top status (0),
     * TIME UP (2), and GAME OVER (3); the world/lives message (1) returns
     * through its lives fixups before this branch.  Text 0/3 name the
     * current player; TIME UP names the other player unless the mode is
     * GAME OVER.  The previous SDL code handled only one current-player
     * case each way. */
    if ((textnum == 0 || textnum == 2 || textnum == 3) &&
        g_NumberOfPlayers != 0) {
        uint8_t named_player = g_CurrentPlayer;
        uint8_t j;
        if (textnum == 2 && g_OperMode != GAME_OVER_MODE)
            named_player ^= 1;
        if (named_player != 0) {
            for (j = 0; j < sizeof(LuigiName); j++)
                g_VRAM_Buffer1[3 + j] = LuigiName[j];
        }
    }

    if (textnum == 1) {
        /* World/lives display fixups (main.asm:449-465) */
        uint8_t lives = g_NumberofLives + 1;
        if (lives >= 10) {
            lives -= 10;
            g_VRAM_Buffer1[7] = 0x9f; /* crown tile */
        }
        g_VRAM_Buffer1[8] = lives;
        g_VRAM_Buffer1[19] = g_WorldNumber + 1;
        g_VRAM_Buffer1[21] = g_LevelNumber + 1;
    }
    if (textnum >= 4) {
        uint8_t base = (uint8_t)((textnum - 4) * 4);
        for (i = 0; i < 3; i++)
            g_VRAM_Buffer1[27 + i * 4] = WarpZoneNumbers[base + i];
        /* SetVRAMOffset (colors.asm:74) leaves the message terminator at
         * the next free byte so the NMI buffer consumer owns the write. */
        g_VRAM_Buffer1_Offset = 0x2c;
    }
}

/* ========================================================================
 * DISPLAY INTERMEDIATE (task 6)
 * ======================================================================== */

void DisplayIntermediate(void) {
    if (g_OperMode == TITLE_SCREEN_MODE) {
        /* Title screen mode: skip to task 8 directly */
        Screen_SetTask(8);
        return;
    }
    if (g_OperMode == GAME_OVER_MODE) {
        /* Game over mode: set longer screen timer, display game over text,
         * then advance the operating mode task. */
        g_ScreenTimer = 0x12;
        Screen_WriteGameText(3);
        g_OperMode_Task++;
        return;
    }
    if (g_AltEntranceControl != 0) {
        Screen_SetTask(8);
        return;
    }
    if (g_AreaType != AREA_TYPE_CASTLE && g_DisableIntermediate != 0) {
        Screen_SetTask(8);
        return;
    }

    /* PlayerInter: DrawPlayer_Intermediate (main.asm:11905-11921). */
    {
        const uint8_t *tiles = &IntermediatePlayerGraphics[0xb8];
        uint8_t base_y = IntermediatePlayerData[0];
        uint8_t attr = IntermediatePlayerData[2];
        uint8_t base_x = IntermediatePlayerData[3];
        int row;
        for (row = 0; row < 4; row++) {
            int slot = 1 + row * 2;
            uint8_t y = (uint8_t)(base_y + row * 8);
            g_SpriteData[slot * 4 + 0] = y;
            g_SpriteData[slot * 4 + 1] = tiles[row * 2];
            g_SpriteData[slot * 4 + 2] = attr;
            g_SpriteData[slot * 4 + 3] = base_x;
            g_SpriteData[(slot + 1) * 4 + 0] = y;
            g_SpriteData[(slot + 1) * 4 + 1] = tiles[row * 2 + 1];
            g_SpriteData[(slot + 1) * 4 + 2] = (uint8_t)(attr |
                ((row == 3) ? 0x40 : 0x00));
            g_SpriteData[(slot + 1) * 4 + 3] = (uint8_t)(base_x + 8);
        }
    }

    /* OutputInter: write world/lives display, reset screen timer,
     * re-enable screen output. main.asm:296-302. */
    Screen_WriteGameText(1);
    g_ScreenTimer = 7;
    Screen_IncTask();
    g_DisableScreenFlag = 0;
}

/* ========================================================================
 * DISPLAY TIME UP (task 4)
 * ======================================================================== */

void DisplayTimeUp(void) {
    if (g_GameTimerExpiredFlag == 0) {
        /* Skip two tasks (past task 5) */
        g_ScreenRoutineTask++;
        Screen_IncTask();
    } else {
        g_GameTimerExpiredFlag = 0;
        /* Output "TIME UP" display, reset screen timer, enable screen */
        Screen_WriteGameText(2);
        g_ScreenTimer = 7;
        Screen_IncTask();
        g_DisableScreenFlag = 0;
    }
}

/* ========================================================================
 * RESET SPRITES AND SCREEN TIMER (tasks 5 and 7)
 * Waits for the ScreenTimer interval timer to expire before advancing.
 * ======================================================================== */

static void MoveAllSpritesOffscreen(void) {
    extern uint8_t g_SpriteData[256];
    int i;
    for (i = 0; i < 256; i += 4) {
        g_SpriteData[i] = 0xF8;
    }
}

void ResetSpritesAndScreenTimer(void) {
    if (g_ScreenTimer != 0) {
        return; /* wait for timer to expire (NoReset) */
    }
    MoveAllSpritesOffscreen();
    g_ScreenTimer = 7;
    Screen_IncTask();
}
