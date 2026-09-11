/* engine/level/level-column.c - AreaParserCore / RenderAreaGraphics */

#include <string.h>
#include "level/level.h"
#include "level/level-internal.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "assets.h"

static uint8_t BSceneDataOffsets[3];
static uint8_t BackSceneryData[144];
static uint8_t BackSceneryMetatiles[36];
static uint8_t FSceneDataOffsets[3];
static uint8_t ForeSceneryData[39];
static uint8_t low_bounds[4];

int Level_LoadColumnTables(void) {
    if (Assets_Copy("tables/bscene_offsets.bin", BSceneDataOffsets,
                    sizeof(BSceneDataOffsets)) ||
        Assets_Copy("tables/back_scenery.bin", BackSceneryData,
                    sizeof(BackSceneryData)) ||
        Assets_Copy("tables/back_scenery_metatiles.bin", BackSceneryMetatiles,
                    sizeof(BackSceneryMetatiles)) ||
        Assets_Copy("tables/fscene_offsets.bin", FSceneDataOffsets,
                    sizeof(FSceneDataOffsets)) ||
        Assets_Copy("tables/fore_scenery.bin", ForeSceneryData,
                    sizeof(ForeSceneryData)) ||
        Assets_Copy("tables/block_buff_low_bounds.bin", low_bounds,
                    sizeof(low_bounds)))
        return -1;
    return 0;
}



void WriteMetatileToNT(uint8_t nt_col, uint8_t nt_row, uint8_t metatile) {
    uint8_t tiles[4];
    GetMetatileTiles(metatile, tiles);
    uint16_t base = 0x2000 + s_NTBaseOffset;

    if (s_ObjectHalfFilter != 1) {
        EmitVRAM(base + nt_row * 32 + nt_col, tiles[0]);           /* top-left */
        EmitVRAM(base + (nt_row + 1) * 32 + nt_col, tiles[1]);     /* bottom-left */
    }
    if (s_ObjectHalfFilter != 0) {
        EmitVRAM(base + nt_row * 32 + nt_col + 1, tiles[2]);       /* top-right */
        EmitVRAM(base + (nt_row + 1) * 32 + nt_col + 1, tiles[3]); /* bottom-right */
    }
}

/* Write palette group bits to attribute table for a metatile position.
 * NES attribute table (main.asm:577-604): each byte at $23C0 + (row/4)*8 + (col/4)
 * contains four 2-bit palette selectors for a 4x4 tile block.
 * nt_col: nametable column (0-31), should be even (metatile-aligned)
 * nt_row: nametable row (0-29), should be even
 * palette_group: 2-bit palette index (0-3) from metatile bits 7-6
 */
void WriteAttributeForMetatile(uint8_t nt_col, uint8_t nt_row, uint8_t palette_group) {
    /* Attribute table address: $23C0 + s_NTBaseOffset + (tile_row/4)*8 + (tile_col/4) */
    uint8_t attr_col = nt_col / 4;
    uint8_t attr_row = nt_row / 4;
    uint16_t attr_addr = 0x23C0 + s_NTBaseOffset + attr_row * 8 + attr_col;

    /* Read current attribute byte (honoring pending writes) */
    uint8_t attr_byte = EmitReadVRAM(attr_addr);

    /* Determine which 2-bit field to set.
     * shift = ((row & 2) << 1) | (col & 2)
     * row&2 and col&2 select the quadrant within the 4x4 block:
     *   (0,0)=bits 1-0, (0,2)=bits 3-2, (2,0)=bits 5-4, (2,2)=bits 7-6
     */
    uint8_t shift = ((nt_row & 2) << 1) | (nt_col & 2);
    uint8_t mask = 0x03 << shift;
    attr_byte = (attr_byte & ~mask) | ((palette_group & 0x03) << shift);

    EmitVRAM(attr_addr, attr_byte);
}

/* RenderAreaGraphics/SetAttrib (main.asm:577-604).  The metatile's two
 * palette bits start in bits 7-6; the current metatile-column parity and
 * row parity select the corresponding quadrant in AttributeBuffer. */
void AccumulateAttributeForMetatile(uint8_t metatile_row,
                                            uint8_t metatile) {
    uint8_t raw = (uint8_t)(metatile & 0xc0);
    uint8_t shifted;
    uint8_t attr_row = (uint8_t)(metatile_row >> 1);

    if (attr_row >= 7) return;

    if ((g_CurrentColumnPos & 1) == 0) {
        /* upper-left / lower-left quadrants */
        shifted = (metatile_row & 1) ? (uint8_t)(raw >> 2)
                                     : (uint8_t)(raw >> 6);
    } else {
        /* upper-right / lower-right quadrants */
        shifted = (metatile_row & 1) ? raw : (uint8_t)(raw >> 4);
    }
    s_AttributeBuffer[attr_row] |= shifted;
}

void AccumulateAttributeColumn(const uint8_t mtbuf[13]) {
    uint8_t row;
    for (row = 0; row < 13; row++)
        AccumulateAttributeForMetatile(row, mtbuf[row]);
}

/* RenderAttributeTables (main.asm:633-679).  The C parser keeps tile writes
 * in its deferred queue, but the attribute producer uses the real Buffer2
 * entry format so the NMI owns the same address/control/data stream as the
 * NES. */
void Level_RenderAttributeTables(void) {
    uint8_t low = (uint8_t)(g_CurrentNTAddrLow & 0x1f);
    uint8_t high = g_CurrentNTAddrHigh;
    uint8_t carry = (uint8_t)(low >= 4);
    uint8_t i;
    uint8_t offset = g_VRAM_Buffer2_Offset;

    low = (uint8_t)((low - 4) & 0x1f);
    if (!carry) high ^= 0x04;
    high = (uint8_t)((high & 0x04) | 0x23);

    /* SBC/LSR/LSR/ADC reproduces the 6502 carry from the original bit 1. */
    low = (uint8_t)(0xc0 + (low >> 2) + ((low >> 1) & 1));

    for (i = 0; i < 7; i++) {
        /* VRAM_Buffer2_Offset is an 8-bit NES cursor; leave room for the
         * four-byte entry without wrapping the terminator to byte zero. */
        if (offset > 251) break;
        g_VRAM_Buffer2[offset] = high;
        g_VRAM_Buffer2[(uint8_t)(offset + 1)] = (uint8_t)(low + 8);
        g_VRAM_Buffer2[(uint8_t)(offset + 2)] = 1;
        g_VRAM_Buffer2[(uint8_t)(offset + 3)] = s_AttributeBuffer[i];
        s_AttributeBuffer[i] = 0;
        offset = (uint8_t)(offset + 4);
        low = (uint8_t)(low + 8);
    }
    g_VRAM_Buffer2[offset] = 0;
    g_VRAM_Buffer2_Offset = offset;
    g_VRAM_Buffer_AddrCtrl = 0x06;
}

/* ========================================================================
 * BACKGROUND SCENERY (main.asm:1788-1825, 1873-1916)
 * ======================================================================== */





/* RenderBackgroundScenery: fills mtbuf rows with scenery metatiles per
 * the NES column lookup. Terrain rendering afterwards overwrites rows. */
void RenderBackgroundScenery(uint8_t page, uint8_t col, uint8_t mtbuf[13]) {
    uint8_t p, idx, data, lo, x, y, count;
    if (g_BackgroundScenery == 0) return;
    p = page;
    while (p >= 3) p -= 3;
    idx = (uint8_t)((p << 4) + BSceneDataOffsets[g_BackgroundScenery - 1] + col);
    data = BackSceneryData[idx];
    if (data == 0) return;
    lo = (uint8_t)((data & 0x0F) - 1);
    x = (uint8_t)(lo * 3);
    y = data >> 4;
    count = 3;
    for (;;) {
        mtbuf[y] = BackSceneryMetatiles[x];
        x++;
        y++;
        if (y == 0x0B) break;
        count--;
        if (count == 0) break;
    }
}

/* ForegroundScenery/SceLoop2 (main.asm:1918-1928). */
void RenderForegroundScenery(uint8_t mtbuf[13]) {
    uint8_t i;
    uint8_t offset;

    if (g_ForegroundScenery == 0 || g_ForegroundScenery > 3)
        return;
    offset = FSceneDataOffsets[g_ForegroundScenery - 1];
    for (i = 0; i < 13; i++) {
        uint8_t mt = ForeSceneryData[offset + i];
        if (mt != 0)
            mtbuf[i] = mt;
    }
}

/* ========================================================================
 * TERRAIN COLUMN RENDERING
 * ========================================================================
 */

/* Render terrain for one column-set (2 PPU columns = 1 metatile column).
 * Matches AreaParserCore terrain rendering (main.asm:1931-1986).
 *
 * The metatile buffer has 13 entries (rows 0-12).
 * Row 0 maps to nametable row 4 (render base $2080).
 * Each metatile row = 2 nametable rows.
 *
 * page: scroll page (0 for first screen)
 * col: metatile column within the page (0-15, each = 2 PPU columns)
 */

/* RendBBuf (main.asm:1988-2020): store the complete composed metatile
 * column, including zeroes, into the selected 32-column block-buffer slot.
 * BlockBuffLowBounds filters scenery/blank values exactly as the 6502 does. */
uint8_t BlockBufferFiltered(uint8_t metatile) {
    return metatile >= low_bounds[(metatile >> 6) & 0x03] ? metatile : 0;
}

void CommitBlockBufferColumn(uint8_t page, uint8_t col,
                                    const uint8_t mtbuf[13],
                                    uint8_t ceiling_byte,
                                    uint8_t floor_byte) {
    uint8_t terr_solid[13];
    memset(terr_solid, 0, sizeof(terr_solid));
    for (int row = 0; row < 8; row++) {
        if (ceiling_byte & Bitmasks[row]) terr_solid[row] = 1;
    }
    for (int row = 0; row < 5; row++) {
        if (floor_byte & Bitmasks[row]) terr_solid[row + 8] = 1;
    }
    for (int row = 0; row < 13; row++) {
        /* RendBBuf calls GetBlockBufferAddr(BlockBufferColumnPos), not
         * GetBlockBufferAddr(CurrentPageLoc|CurrentColumnPos).  The latter
         * is the world coordinate; the former is the physical 32-column
         * ring slot owned by $06a0 and advanced by IncrementColumnPos. */
        uint8_t buffer_col = (uint8_t)(g_BlockBufferColumnPos & 0x1f);
        uint8_t block_value = BlockBufferFiltered(mtbuf[row]);
        uint8_t solid_value = terr_solid[row];
        if (s_ParserObjectMergeValid &&
            page == s_ParserPage && col == s_ParserCol) {
            if (s_ParserBlockOverride[row] == 0)
                block_value = 0;
            if (s_ParserSolidOverride[row] != 0xff)
                solid_value = s_ParserSolidOverride[row];
        }
        s_BlockBuffer[row][buffer_col] = block_value;
        Level_SetBlockSolid(buffer_col, (uint8_t)row, solid_value);
    }
}

/* Build the metatile buffer for a column (scenery + terrain) */
void BuildColumnBuffer(uint8_t page, uint8_t col, uint8_t mtbuf[13],
                              uint8_t* ceiling_byte, uint8_t* floor_byte) {
    uint8_t terrain_mtile = Level_GetTerrainMetatile();
    uint8_t row;
    *ceiling_byte = TerrainRenderBits[g_TerrainControl][0];
    *floor_byte = TerrainRenderBits[g_TerrainControl][1];
    memset(mtbuf, 0, 13);
    RenderBackgroundScenery(page, col, mtbuf);
    RenderForegroundScenery(mtbuf);

    /* TerrLoop (main.asm:1947-1984) consumes the two terrain bytes as one
     * 13-row bit stream. CloudTypeOverride masks the lower byte to bit 3;
     * underground rows 11/12 switch to ground metatile $54. */
    for (row = 0; row < 13; row++) {
        uint8_t terrain_bits = row < 8 ? *ceiling_byte : *floor_byte;
        uint8_t bit_index = row < 8 ? row : (uint8_t)(row - 8);
        uint8_t row_terrain = terrain_mtile;

        if (g_CloudTypeOverride != 0 && row >= 8)
            terrain_bits &= 0x08;
        if (g_AreaType == AREA_TYPE_UNDERGROUND && row >= 11)
            row_terrain = 0x54;
        if (terrain_bits & Bitmasks[bit_index])
            mtbuf[row] = row_terrain;
    }
}

/* Render only the LEFT tile column of a metatile column (first
 * RenderAreaGraphics pass). */
void AdvanceCurrentNTAddr(void) {
    /* RenderAreaGraphics increments CurrentNTAddr_Low once per tile-column
     * buffer entry and wraps $xxa0 back to $xx80 while toggling nametable
     * bit 2 (main.asm:617-627). */
    g_CurrentNTAddrLow++;
    if ((g_CurrentNTAddrLow & 0x1f) == 0) {
        g_CurrentNTAddrLow = 0x80;
        g_CurrentNTAddrHigh ^= 0x04;
    }
}


/* RenderAreaGraphics (main.asm:533-632) emits one complete 26-byte tile
 * column into VRAM_Buffer2.  The address is CurrentNTAddr, and the $9a
 * control byte makes the NMI advance the PPU by 32 for each tile.  The caller
 * supplies the half selected by the ASM task-table order: left is tiles 0/1,
 * right is tiles 2/3 from the column-major metatile graphics table. */
void AppendRenderAreaGraphicsColumn(const uint8_t mtbuf[13],
                                           uint8_t right_half) {
    uint8_t offset = g_VRAM_Buffer2_Offset;
    uint8_t row;

    g_VRAM_Buffer2[offset] = g_CurrentNTAddrHigh;
    g_VRAM_Buffer2[(uint8_t)(offset + 1)] = g_CurrentNTAddrLow;
    g_VRAM_Buffer2[(uint8_t)(offset + 2)] = 0x9a;

    for (row = 0; row < 13; row++) {
        uint8_t tiles[4];
        uint8_t tile_offset = right_half ? 2 : 0;
        uint8_t data_offset = (uint8_t)(offset + 3 + row * 2);

        GetMetatileTiles(mtbuf[row], tiles);
        g_VRAM_Buffer2[data_offset] = tiles[tile_offset];
        g_VRAM_Buffer2[(uint8_t)(data_offset + 1)] =
            tiles[(uint8_t)(tile_offset + 1)];
    }

    offset = (uint8_t)(offset + 29);
    g_VRAM_Buffer2[offset] = 0;
    g_VRAM_Buffer2_Offset = offset;
    g_VRAM_Buffer_AddrCtrl = 0x06;
}

void Level_RenderColumnLeftHalf(uint8_t page, uint8_t col) {
    uint8_t mtbuf[13], cb, fb;
    if (s_ParserCoreValid && s_ParserPage == page && s_ParserCol == col) {
        memcpy(mtbuf, s_ParserMTBuf, sizeof(mtbuf));
        cb = s_ParserCeiling;
        fb = s_ParserFloor;
    } else {
        s_ParserObjectMergeValid = 0;
        BuildColumnBuffer(page, col, mtbuf, &cb, &fb);
    }
    AppendRenderAreaGraphicsColumn(mtbuf, 0);
    AccumulateAttributeColumn(mtbuf);
    /* AreaParserCore/RendBBuf already committed the composed column before
     * the first RenderAreaGraphics half.  The fallback path remains useful
     * for callers that render a half without a preceding Core task. */
    if (!(s_ParserCoreValid && s_ParserPage == page && s_ParserCol == col))
        CommitBlockBufferColumn(page, col, mtbuf, cb, fb);
    AdvanceCurrentNTAddr();
}

/* Render only the RIGHT tile column of a metatile column (the second
 * RenderAreaGraphics pass for the column set). */
void Level_RenderColumnRightHalf(uint8_t page, uint8_t col) {
    uint8_t mtbuf[13], cb, fb;
    if (s_ParserCoreValid && s_ParserPage == page && s_ParserCol == col) {
        memcpy(mtbuf, s_ParserMTBuf, sizeof(mtbuf));
        cb = s_ParserCeiling;
        fb = s_ParserFloor;
    } else {
        BuildColumnBuffer(page, col, mtbuf, &cb, &fb);
    }
    AppendRenderAreaGraphicsColumn(mtbuf, 1);
    /* No separate attribute write here: the quadrant was accumulated by the
     * same metatile stream, and RenderAttributeTables drains it after the
     * complete parser round. */
    AccumulateAttributeColumn(mtbuf);
    AdvanceCurrentNTAddr();
}

/* AreaParserCore's backloading entry (main.asm:1861-1864) calls
 * ProcessAreaData before ClrMTBuf.  The source records already behind the
 * saved start page are decoded by that pass, including AlterAreaAttributes;
 * those shared header bytes must therefore be in place before the scenery
 * renderer reads them.  The ordinary object-slot pass below remains the
 * owner of object construction and residual lengths.  Keep this narrow
 * prepass limited to the state-only row-$0e handler until the other
 * pre-clear object handlers have matching scratch-buffer ownership. */
void Level_PrepassAreaAttributes(uint8_t target_page) {
    uint16_t cursor = s_AreaObjectCursor;
    uint8_t page = s_AreaObjectPageLoc;
    uint8_t page_select = s_AreaObjectPageSel;

    if (!g_AreaDataPtr || g_AreaDataLen < 2)
        return;

    while (cursor + 1 < g_AreaDataLen && page < target_page) {
        uint8_t first = g_AreaDataPtr[cursor];
        uint8_t second = g_AreaDataPtr[cursor + 1];
        uint8_t row = first & 0x0f;

        if (first == 0xfd)
            break;
        if ((second & 0x80) != 0 && page_select == 0) {
            page_select = 1;
            page++;
        }
        if (row == 0x0d && (second & 0x40) == 0 && page_select == 0) {
            page = second & 0x1f;
            page_select = 1;
        }
        if (row == 0x0e && page < target_page)
            Level_ApplyAreaAttributes(second);

        cursor = (uint16_t)(cursor + 2);
        page_select = 0;
    }
}

/* AreaParserCore/RendBBuf supported terrain boundary.  The composed terrain
 * column is now consumed by the live RenderAreaGraphics Buffer2 producer;
 * unsupported object-specific branches remain at their named boundaries. */
void Level_AreaParserCore(uint8_t page, uint8_t col) {
    if (g_BackloadingFlag != 0)
        Level_PrepassAreaAttributes(page);
    s_ParserObjectMergeValid = 0;

    /* AreaParserCore calls ProcessAreaData before clearing MetatileBuffer
     * when BackloadingFlag is set (main.asm:1861-1864).  InitRear's forced
     * ObjectOffset=$00 exit belongs to this pre-clear call; RendBBuf then
     * calls ProcessAreaData again below after the terrain buffer is built. */
    if (g_BackloadingFlag != 0) {
        s_ParserComposing = 1;
        Level_ProcessAreaDataSlots(page, col);
        s_ParserComposing = 0;
    }

    BuildColumnBuffer(page, col, s_ParserMTBuf,
                      &s_ParserCeiling, &s_ParserFloor);
    s_ParserPage = page;
    s_ParserCol = col;
    s_ParserCoreValid = 1;
    memset(s_ParserSolidOverride, 0xff, sizeof(s_ParserSolidOverride));
    memset(s_ParserBlockOverride, 0xff, sizeof(s_ParserBlockOverride));
    s_ParserComposing = 1;
    Level_ProcessAreaDataSlots(page, col);
    s_ParserComposing = 0;
    s_ParserObjectMergeValid = 1;
    CommitBlockBufferColumn(page, col, s_ParserMTBuf,
                            s_ParserCeiling, s_ParserFloor);
    s_ParserObjectMergeValid = 0;
}

