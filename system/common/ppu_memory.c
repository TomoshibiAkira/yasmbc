/* Host-side NES PPU memory and render-state implementation. */

#include "ppu_memory.h"
#include "../platform.h"
#include <stdio.h>
#include <string.h>

extern uint8_t g_RenderSprite0Split;

#define PPU_CTRL_INCREMENT32 0x04

typedef struct {
    uint8_t ppuctrl;
    uint16_t v;
    uint16_t t;
    uint8_t x;
    uint8_t w;
} PPU_State;

static PPU_State ppu = {0};
static uint8_t vram[0x0800];
static uint8_t palette[32];
void PPU_CopyRenderMemory(uint8_t *nametables, uint8_t *colors)
{
    memcpy(nametables, vram, sizeof(vram));
    memcpy(colors, palette, sizeof(palette));
}
/* FCEUX keeps the $3F04/$3F08/$3F0C color-zero writes in separate
 * readback latches.  The rendered palette slots remain the universal
 * background color written through $3F00 (the active SMB PPU path in
 * verifier/_deps/fceux/src/ppu.cpp:B2007). */
static uint8_t palette_readback[3];
static uint16_t scroll_x = 0;
static uint32_t render_generation;
static uint32_t nametable_generation;
static uint8_t tile_dirty[2][32 * 30];
typedef struct {
    uint8_t nt;
    uint8_t col;
    uint8_t row;
} PPU_DirtyTile;
static PPU_DirtyTile dirty_tiles[2 * 32 * 30];
static int tiles_dirty_count;
static uint8_t palette_dirty;

static void mark_tile_dirty(uint8_t nt, int col, int row)
{
    uint16_t idx;
    if (nt > 1 || col < 0 || col > 31 || row < 0 || row > 29)
        return;
    idx = (uint16_t)row * 32 + (uint16_t)col;
    if (!tile_dirty[nt][idx]) {
        tile_dirty[nt][idx] = 1;
        dirty_tiles[tiles_dirty_count].nt = nt;
        dirty_tiles[tiles_dirty_count].col = (uint8_t)col;
        dirty_tiles[tiles_dirty_count].row = (uint8_t)row;
        ++tiles_dirty_count;
    }
}

static void mark_vram_dirty(uint16_t offset)
{
    uint8_t nt = (offset >= 0x400) ? 1 : 0;
    uint16_t local = offset & 0x3FF;
    if (local < 0x3C0) {
        mark_tile_dirty(nt, local % 32, local / 32);
    } else {
        uint16_t attr = local - 0x3C0;
        int ac = attr % 8;
        int ar = attr / 8;
        int r, c;
        for (r = ar * 4; r < ar * 4 + 4; r++)
            for (c = ac * 4; c < ac * 4 + 4; c++)
                mark_tile_dirty(nt, c, r);
    }
}

void PPU_Init(void) {
    memset(&ppu, 0, sizeof(ppu));
    memset(vram, 0, sizeof(vram));
    memset(palette, 0, sizeof(palette));
    memset(palette_readback, 0, sizeof(palette_readback));
    memset(tile_dirty, 0, sizeof(tile_dirty));
    memset(dirty_tiles, 0, sizeof(dirty_tiles));
    tiles_dirty_count = 0;
    palette_dirty = 0x0F;
    render_generation = 1;
    nametable_generation = 1;
    printf("PPU initialized\n");
}

void PPU_SetCtrl(uint8_t value) {
    ppu.ppuctrl = value;
    ppu.t = (ppu.t & 0xF3FF) | ((value & 0x03) << 10);
}

uint8_t PPU_GetCtrl(void) {
    return ppu.ppuctrl;
}

void PPU_SetIncrement32(uint8_t enable) {
    if (enable) {
        ppu.ppuctrl |= PPU_CTRL_INCREMENT32;
    } else {
        ppu.ppuctrl &= (uint8_t)~PPU_CTRL_INCREMENT32;
    }
}

void PPU_SetScroll(uint8_t x, uint8_t y) {
    if (ppu.w == 0) {
        scroll_x = x;
        ppu.t = (ppu.t & 0xFFE0) | (x >> 3);
        ppu.x = x & 0x07;
        ppu.w = 1;
    } else {
        ppu.t = (ppu.t & 0x8C1F) | ((y & 0xF8) << 2) | ((y & 0x07) << 12);
        ppu.w = 0;
    }
}

void PPU_SetFullAddr(uint16_t addr) {
    uint8_t high = (addr >> 8) & 0x3F;
    uint8_t low = addr & 0xFF;
    ppu.t = (ppu.t & 0x00FF) | ((uint16_t)high << 8);
    ppu.w = 1;
    ppu.t = (ppu.t & 0xFF00) | low;
    ppu.v = ppu.t;
    ppu.w = 0;
}

void PPU_WriteData(uint8_t value) {
    uint16_t addr = ppu.v & 0x3FFF;

    if (addr < 0x2000) {
        /* Pattern table - ignored in SDL, we use CHR files */
    } else if (addr < 0x3F00) {
        uint16_t offset = addr & 0x07FF;
        if (vram[offset] != value) {
            vram[offset] = value;
            mark_vram_dirty(offset);
            ++render_generation;
            ++nametable_generation;
        }
    } else if (addr < 0x4000) {
        uint8_t pi = addr & 0x1F;
        /* Match the active FCEUX B2007/FFCEUX_PPUWrite_Default path.
         * Any color-zero address is special: $3F00 writes all four
         * rendered background color-zero slots, while $3F04/$3F08/$3F0C
         * update only their readback latches.  This also applies to the
         * $3F10-$3F1C range, whose low five address bits are decoded in
         * the same way by the reference. */
        if ((pi & 0x03) == 0) {
            if ((pi & 0x0c) == 0) {
                if (palette[0] != value) {
                    ++render_generation;
                    palette_dirty = 0x0F;
                }
                palette[0] = value;
                palette[4] = value;
                palette[8] = value;
                palette[12] = value;
            } else {
                palette_readback[((pi & 0x0c) >> 2) - 1] = value;
            }
        } else {
            if (palette[pi] != value) {
                ++render_generation;
                palette_dirty |= (uint8_t)(1u << (pi / 4));
            }
            palette[pi] = value;
        }
    }

    if (ppu.ppuctrl & PPU_CTRL_INCREMENT32) {
        ppu.v += 32;
    } else {
        ppu.v += 1;
    }
    ppu.v &= 0x7FFF;
}

static void nametable_row_scroll(int row, uint8_t nt_primary,
                                 uint8_t *row_scroll_tile,
                                 uint8_t *row_scroll_fine,
                                 uint16_t *row_primary_base,
                                 uint16_t *row_secondary_base)
{
    uint8_t h_scroll = (uint8_t)(scroll_x & 0xFF);
    if (row < 4 && g_RenderSprite0Split) {
        *row_scroll_tile = 0;
        *row_scroll_fine = 0;
        *row_primary_base = 0x0000;
        *row_secondary_base = 0x0400;
    } else {
        *row_scroll_tile = h_scroll / 8;
        *row_scroll_fine = h_scroll % 8;
        *row_primary_base = (nt_primary & 1) * 0x0400;
        *row_secondary_base = ((nt_primary ^ 1) & 1) * 0x0400;
    }
}

static void render_visible_tile(int row, int vis_col, uint8_t row_scroll_tile,
                                uint8_t row_scroll_fine,
                                uint16_t row_primary_base,
                                uint16_t row_secondary_base)
{
    int src_col = row_scroll_tile + vis_col;
    uint16_t base, attr_base_src;
    uint16_t addr;
    uint8_t tile, attr_byte;
    int screen_x, screen_y, attr_col, attr_row, shift, palette_idx;

    if (src_col < 32) {
        base = row_primary_base;
        attr_base_src = row_primary_base + 0x03C0;
    } else {
        base = row_secondary_base;
        attr_base_src = row_secondary_base + 0x03C0;
        src_col -= 32;
    }

    addr = base + row * 32 + src_col;
    tile = vram[addr];
    screen_x = vis_col * 8 - row_scroll_fine;
    screen_y = row * 8;
    if (screen_x <= -8 || screen_x >= 256)
        return;

    attr_col = src_col / 4;
    attr_row = row / 4;
    attr_byte = vram[attr_base_src + attr_row * 8 + attr_col];
    shift = ((row & 2) << 1) | (src_col & 2);
    palette_idx = (attr_byte >> shift) & 0x03;
    platform_draw_tile_at(tile, (uint8_t)palette_idx, screen_x, screen_y);
}

/* Draw the nametable to the pixel buffer with horizontal scroll.
 * The visible 256px window starts at the scroll offset within the
 * primary nametable and wraps into the secondary nametable.
 *
 * Rows 0-3 (status bar) do not scroll: NES uses a mid-frame scroll
 * write via sprite 0 hit.  We emulate that by rendering those rows
 * from NT0 at scroll=0 when g_RenderSprite0Split is set.
 */
void PPU_RenderNametableXRange(uint8_t nt_primary, int x0, int x1)
{
    int row;

    if (x0 < 0)
        x0 = 0;
    if (x1 > 255)
        x1 = 255;
    if (x0 > x1)
        return;

    for (row = 0; row < 30; row++) {
        uint8_t row_scroll_tile;
        uint8_t row_scroll_fine;
        uint16_t row_primary_base;
        uint16_t row_secondary_base;
        int first_vis, last_vis, vis_col;

        nametable_row_scroll(row, nt_primary, &row_scroll_tile,
                             &row_scroll_fine, &row_primary_base,
                             &row_secondary_base);
        first_vis = (x0 + row_scroll_fine) / 8;
        last_vis = (x1 + row_scroll_fine) / 8;
        if (first_vis < 0)
            first_vis = 0;
        if (last_vis > 32)
            last_vis = 32;
        for (vis_col = first_vis; vis_col <= last_vis; vis_col++)
            render_visible_tile(row, vis_col, row_scroll_tile,
                                row_scroll_fine, row_primary_base,
                                row_secondary_base);
    }
}

void PPU_RenderNametable(uint8_t nt_primary)
{
    PPU_RenderNametableXRange(nt_primary, 0, 255);
    PPU_ClearDirtyTiles();
}

void PPU_RedrawDirtyTiles(uint8_t nt_primary)
{
    int dirty_index;

    if (tiles_dirty_count <= 0)
        return;

    for (dirty_index = 0; dirty_index < tiles_dirty_count; ++dirty_index) {
        uint8_t nt = dirty_tiles[dirty_index].nt;
        int col = dirty_tiles[dirty_index].col;
        int row = dirty_tiles[dirty_index].row;
        int screen_x, screen_y;
        uint16_t base, attr_base;
        uint8_t tile, attr_byte;
        int attr_col, attr_row, shift, palette_idx;
        int hud = (row < 4 && g_RenderSprite0Split);

        if (hud) {
            if (nt != 0)
                continue;
            screen_x = col * 8;
            screen_y = row * 8;
            base = 0x0000;
        } else {
            int primary = (nt == (nt_primary & 1));
            int world_x = (primary ? 0 : 256) + col * 8;
            screen_x = world_x - (int)(scroll_x & 0xFF);
            screen_y = row * 8;
            base = nt * 0x0400;
        }
        if (screen_x <= -8 || screen_x >= 256)
            continue;

        attr_base = base + 0x03C0;
        tile = vram[base + row * 32 + col];
        attr_col = col / 4;
        attr_row = row / 4;
        attr_byte = vram[attr_base + attr_row * 8 + attr_col];
        shift = ((row & 2) << 1) | (col & 2);
        palette_idx = (attr_byte >> shift) & 0x03;
        platform_draw_tile_at(tile, (uint8_t)palette_idx,
                              screen_x, screen_y);
    }
    memset(tile_dirty, 0, sizeof(tile_dirty));
    tiles_dirty_count = 0;
}

void PPU_RedrawTilesUsingPalettes(uint8_t nt_primary, uint8_t pal_mask)
{
    uint8_t nt;
    int row, col;

    pal_mask &= 0x0F;
    if (!pal_mask)
        return;

    for (nt = 0; nt < 2; nt++) {
        for (row = 0; row < 30; row++) {
            for (col = 0; col < 32; col++) {
                int screen_x, screen_y;
                uint16_t base, attr_base;
                uint8_t tile, attr_byte;
                int attr_col, attr_row, shift, palette_idx;
                int hud = (row < 4 && g_RenderSprite0Split);

                if (hud) {
                    if (nt != 0)
                        continue;
                    screen_x = col * 8;
                    screen_y = row * 8;
                    base = 0x0000;
                } else {
                    int primary = (nt == (nt_primary & 1));
                    int world_x = (primary ? 0 : 256) + col * 8;
                    screen_x = world_x - (int)(scroll_x & 0xFF);
                    screen_y = row * 8;
                    base = nt * 0x0400;
                }
                if (screen_x <= -8 || screen_x >= 256)
                    continue;

                attr_base = base + 0x03C0;
                tile = vram[base + row * 32 + col];
                attr_col = col / 4;
                attr_row = row / 4;
                attr_byte = vram[attr_base + attr_row * 8 + attr_col];
                shift = ((row & 2) << 1) | (col & 2);
                palette_idx = (attr_byte >> shift) & 0x03;
                if (((uint8_t)(1u << palette_idx) & pal_mask) == 0)
                    continue;
                platform_draw_tile_at(tile, (uint8_t)palette_idx,
                                      screen_x, screen_y);
            }
        }
    }
}

void PPU_ClearDirtyTiles(void)
{
    if (tiles_dirty_count) {
        memset(tile_dirty, 0, sizeof(tile_dirty));
        tiles_dirty_count = 0;
    }
}

int PPU_DirtyTileCount(void)
{
    return tiles_dirty_count;
}

uint8_t PPU_TakePaletteDirty(void)
{
    uint8_t dirty = palette_dirty;
    palette_dirty = 0;
    return dirty;
}

uint8_t PPU_ReadVRAM(uint16_t offset) {
    if (offset < 0x0800) {
        return vram[offset];
    }
    return 0;
}

/* Read a rendered palette RAM entry.
 * The active FCEUX PPU renderer keeps the background color-zero slots and
 * sprite color-zero slots as distinct PALRAM entries. */
uint8_t PPU_ReadPalette(uint8_t index) {
    return palette[index & 0x1F];
}

uint32_t PPU_GetRenderGeneration(void) {
    return render_generation;
}

uint32_t PPU_GetNametableGeneration(void) {
    return nametable_generation;
}
