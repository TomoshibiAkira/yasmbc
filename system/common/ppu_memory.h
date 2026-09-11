/* Host-side NES PPU memory and render-state abstraction. */

#ifndef PPU_MEMORY_H
#define PPU_MEMORY_H

#include <stdint.h>

void PPU_Init(void);
void PPU_CopyRenderMemory(uint8_t *nametables, uint8_t *colors);

void PPU_SetCtrl(uint8_t value);
uint8_t PPU_GetCtrl(void);
void PPU_SetIncrement32(uint8_t enable);
void PPU_SetScroll(uint8_t x, uint8_t y);
void PPU_SetFullAddr(uint16_t addr);
void PPU_WriteData(uint8_t value);

uint8_t PPU_ReadVRAM(uint16_t offset);
uint8_t PPU_ReadPalette(uint8_t index);
uint32_t PPU_GetRenderGeneration(void);
uint32_t PPU_GetNametableGeneration(void);
uint8_t PPU_TakePaletteDirty(void);
int PPU_DirtyTileCount(void);
void PPU_ClearDirtyTiles(void);

void PPU_RenderNametable(uint8_t nt_index);
void PPU_RenderNametableXRange(uint8_t nt_index, int x0, int x1);
void PPU_RedrawDirtyTiles(uint8_t nt_index);
void PPU_RedrawTilesUsingPalettes(uint8_t nt_index, uint8_t pal_mask);

#endif
