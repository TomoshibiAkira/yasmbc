/* init.c - Screen initialization (from init-screen.asm) */

#include "screen/screen.h"
#ifdef NES
#include "system/ppu.h"
#else
#include "system/common/ppu_memory.h"
#endif
#include "constants/globals.h"

/* Render nametable latch corresponding to Mirror_PPU_CTRL_REG1 bit 0
 * ($0778), which InitializeNameTables clears through WritePPUReg1. */
extern uint8_t g_RenderNT;
extern uint8_t g_MirrorPPUCtrl1;

/* Move all sprites offscreen by setting Y position to $F8 */
static void MoveAllSpritesOffscreen(void) {
    /* On NES: sets all 64 OAM sprite Y positions to $F8 (offscreen).
     * Sprite data is at OAM $00-$FF, Y position every 4 bytes.
     */
    extern uint8_t g_SpriteData[256];
    for (int i = 0; i < 256; i += 4) {
        g_SpriteData[i] = 0xF8;
    }
}

/* Initialize name tables */
static void InitializeNameTables(void) {
    /* On NES, this fills BOTH nametables with tile $24 (background color)
     * and clears both attribute tables.
     */
    /* Fill nametable 0 ($2000-$23BF) with tile $24 */
    PPU_SetFullAddr(0x2000);
    for (int i = 0; i < 960; i++) {
        PPU_WriteData(0x24);
    }
    /* Clear attribute table 0 ($23C0-$23FF) */
    PPU_SetFullAddr(0x23C0);
    for (int i = 0; i < 64; i++) {
        PPU_WriteData(0x00);
    }
    /* Fill nametable 1 ($2400-$27BF) with tile $24 */
    PPU_SetFullAddr(0x2400);
    for (int i = 0; i < 960; i++) {
        PPU_WriteData(0x24);
    }
    /* Clear attribute table 1 ($27C0-$27FF) */
    PPU_SetFullAddr(0x27C0);
    for (int i = 0; i < 64; i++) {
        PPU_WriteData(0x00);
    }

    /* InitializeNameTables writes (Mirror_PPU_CTRL_REG1 | $10) & $f0
     * through WritePPUReg1, clearing the nametable-select bit. */
    g_MirrorPPUCtrl1 = (uint8_t)((g_MirrorPPUCtrl1 | 0x10) & 0xf0);
    /* The same routine then clears HorizontalScroll and VerticalScroll
     * before InitScroll writes the PPU scroll registers. */
    g_HorizontalScroll = 0;
}

void InitScreen(void) {
    MoveAllSpritesOffscreen();
    InitializeNameTables();

    /* NES init-screen.asm: only game modes set the VRAM buffer address
     * control to $03 (UndergroundPaletteData); title mode advances directly. */
    if (g_OperMode != TITLE_SCREEN_MODE) {
        g_VRAM_Buffer_AddrCtrl = 0x03;
    }

    /* Move to next task */
    Screen_IncTask();
}
