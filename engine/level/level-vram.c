/* engine/level/level-vram.c - Deferred nametable writes (VRAM_Buffer2) */

#include <string.h>
#include "level/level.h"
#include "level/level-internal.h"
#ifdef NES
#include "system/ppu.h"
#else
#include "system/common/ppu_memory.h"
#endif

/* Deferred VRAM write queue: the NES area parser renders into
 * VRAM_Buffer2 which the NEXT NMI flush writes to the PPU (one output
 * frame of latency). Render calls between Begin/Flush accumulate here. */
PendingWrite s_PendingWrites[512];
int s_PendingCount = 0;
uint8_t s_DeferVRAM = 0;

void EmitVRAM(uint16_t addr, uint8_t val) {
    if (s_DeferVRAM) {
        if (s_PendingCount < (int)(sizeof(s_PendingWrites) / sizeof(s_PendingWrites[0]))) {
            s_PendingWrites[s_PendingCount].addr = addr;
            s_PendingWrites[s_PendingCount].val = val;
            s_PendingCount++;
        }
    } else {
        PPU_SetFullAddr(addr);
        PPU_WriteData(val);
    }
}

uint8_t EmitReadVRAM(uint16_t addr) {
    int i;
    uint16_t off = addr & 0x07FF;
    for (i = s_PendingCount - 1; i >= 0; i--) {
        if ((s_PendingWrites[i].addr & 0x07FF) == off) return s_PendingWrites[i].val;
    }
    return PPU_ReadVRAM(off);
}

void Level_BeginDeferred(void) { s_DeferVRAM = 1; }

void Level_FlushDeferred(void) {
    int i;
    for (i = 0; i < s_PendingCount; i++) {
        PPU_SetFullAddr(s_PendingWrites[i].addr);
        PPU_WriteData(s_PendingWrites[i].val);
    }
    s_PendingCount = 0;
    s_DeferVRAM = 0;
}

/* Get the 4 PPU tiles for a metatile ID.
 * metatile: full metatile ID (palette group in bits 7-6, index in bits 5-0)
 * tiles[4]: output array [TL, TR, BL, BR]
 */
void GetMetatileTiles(uint8_t metatile, uint8_t tiles[4]) {
    uint8_t group = (metatile >> 6) & 0x03;
    uint8_t index = (metatile & 0x3F);
    const uint8_t* table = MetatileGfxTables[group];
    memcpy(tiles, &table[index * 4], 4);
}

