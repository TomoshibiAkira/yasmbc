/* engine/level/level.c - Shared metatile tables and block-buffer RAM */

#include <string.h>
#include "level/level.h"
#include "level/level-internal.h"
#include "assets.h"

/* ========================================================================
 * DATA TABLES (from NES disassembly)
 * ======================================================================== */

uint8_t TerrainRenderBits[16][2];
uint8_t TerrainMetatiles[4];
uint8_t Palette0_MTiles[156];
uint8_t Palette1_MTiles[184];
uint8_t Palette2_MTiles[40];
uint8_t Palette3_MTiles[24];
uint8_t WarpZoneNumbers[12];

uint8_t Bitmasks[8];
static uint8_t BlockBuffer_X_Adder[28];
static uint8_t BlockBuffer_Y_Adder[28];

const uint8_t *const MetatileGfxTables[4] = {
    Palette0_MTiles, Palette1_MTiles, Palette2_MTiles, Palette3_MTiles
};

int Level_LoadRomTables(void) {
    if (Assets_Copy("tables/terrain_render_bits.bin", TerrainRenderBits,
                    sizeof(TerrainRenderBits)) ||
        Assets_Copy("tables/terrain_metatiles.bin", TerrainMetatiles,
                    sizeof(TerrainMetatiles)) ||
        Assets_Copy("tables/palette0_mtiles.bin", Palette0_MTiles,
                    sizeof(Palette0_MTiles)) ||
        Assets_Copy("tables/palette1_mtiles.bin", Palette1_MTiles,
                    sizeof(Palette1_MTiles)) ||
        Assets_Copy("tables/palette2_mtiles.bin", Palette2_MTiles,
                    sizeof(Palette2_MTiles)) ||
        Assets_Copy("tables/palette3_mtiles.bin", Palette3_MTiles,
                    sizeof(Palette3_MTiles)) ||
        Assets_Copy("tables/warp_zone_numbers.bin", WarpZoneNumbers,
                    sizeof(WarpZoneNumbers)) ||
        Assets_Copy("tables/bitmasks.bin", Bitmasks, sizeof(Bitmasks)) ||
        Assets_Copy("tables/block_buffer_x_adder.bin", BlockBuffer_X_Adder,
                    sizeof(BlockBuffer_X_Adder)) ||
        Assets_Copy("tables/block_buffer_y_adder.bin", BlockBuffer_Y_Adder,
                    sizeof(BlockBuffer_Y_Adder)))
        return -1;
    if (Level_LoadPointerTables() != 0)
        return -1;
    if (Level_LoadObjectTables() != 0)
        return -1;
    return Level_LoadColumnTables();
}

/* ========================================================================
 * LEVEL STATE
 * ======================================================================== */

/* Block-buffer collision state.  The NES owns two 13x16 byte buffers at
 * $0500/$05d0; together they form a 32-column ring.  Keep the metatile
 * bytes (not just a boolean) because PlayerBGCollision and enemy/object
 * collision classify the returned values. */
uint8_t s_BlockBuffer[13][32];

void Level_GetVerifierBlockBuffers(uint8_t first[208], uint8_t second[208]) {
    unsigned column, row;
    for (column = 0; column < 32; ++column) {
        uint8_t *bank = column < 16 ? first : second;
        unsigned local_column = column & 15;
        for (row = 0; row < 13; ++row)
            bank[row * 16 + local_column] = s_BlockBuffer[row][column];
    }
}
/* Derived terrain/object solidity retained for the existing collision
 * probes until their metatile-specific branches are translated. */
uint8_t s_BlockSolid[13][32];

uint8_t Level_BlockSolid(uint8_t mt_col, uint8_t mt_row) {
    if (mt_row >= 13 || mt_col >= 32) return 0;
    return s_BlockSolid[mt_row][mt_col];
}

void Level_SetBlockSolid(uint8_t mt_col, uint8_t mt_row, uint8_t solid) {
    if (mt_row >= 13) return;

    /* BlockBufferCollision masks the page-derived column to the two-
     * nametable window; object writes use the same wrapped slot. */
    s_BlockSolid[mt_row][mt_col & 0x1f] = solid;
}

uint8_t Level_GetBlockMetatile(uint8_t mt_col, uint8_t mt_row) {
    if (mt_row >= 13 || mt_col >= 32) return 0;
    return s_BlockBuffer[mt_row][mt_col];
}

/* Direct block-buffer writes are part of the NES object contract.  The
 * PlayerEntrance/VineEntr branch writes Block_Buffer_1+$b4, so expose the
 * two real $0500/$05d0 13x16 banks without turning that byte into a visual
 * or coordinate-specific substitute. */
void Level_WriteBlockBufferAddress(uint16_t address, uint8_t value) {
    uint16_t offset;
    uint8_t column;
    uint8_t row;

    if (address >= 0x0500 && address < 0x05d0) {
        offset = (uint16_t)(address - 0x0500);
        column = (uint8_t)(offset & 0x0f);
    } else if (address >= 0x05d0 && address < 0x06a0) {
        offset = (uint16_t)(address - 0x05d0);
        column = (uint8_t)(16 + (offset & 0x0f));
    } else {
        return;
    }
    row = (uint8_t)(offset >> 4);
    if (row < 13) s_BlockBuffer[row][column] = value;
}

/* WrCMTile uses the address and row returned by BlockBufferCollision, then
 * writes the raw byte through ($06),Y.  Keep this as a block-buffer-only
 * operation: unlike QueueMetatile it must not enqueue an immediate VRAM
 * update or mark a derived solidity shadow. */
void Level_WriteBlockBufferProbe(const LevelBlockBufferProbe *probe,
                                 uint8_t value) {
    uint16_t base;
    uint16_t address;

    if (probe == NULL || probe->aligned_y >= 0xd0)
        return;
    base = probe->buffer_select ? 0x05d0 : 0x0500;
    address = (uint16_t)(base + probe->aligned_y +
                         (probe->buffer_column & 0x0f));
    Level_WriteBlockBufferAddress(address, value);
}

/* BlockBufferCollision tables (main.asm:10335-10348).  The caller supplies
 * the exact Y index selected by BlockBufferAdderData; Feet increments that
 * index before entering the shared producer. */

uint8_t Level_BlockBufferCollision(LevelBlockBufferProbe *probe,
                                   uint8_t page_loc, uint8_t x_position,
                                   uint8_t y_position, uint8_t probe_index,
                                   uint8_t horizontal_return) {
    uint16_t x_sum;
    uint16_t y_sum;
    uint8_t row;

    memset(probe, 0, sizeof(*probe));
    probe->probe_index = probe_index;
    if (probe_index >= sizeof(BlockBuffer_X_Adder)) return 0;

    /* GetBlockBufferAddr: ADC the X adder, then fold the carried page bit
     * into the high/low half selection used by $0500/$05d0. */
    x_sum = (uint16_t)x_position + BlockBuffer_X_Adder[probe_index];
    probe->adjusted_x = (uint8_t)x_sum;
    probe->adjusted_page = (uint8_t)(page_loc + (x_sum >> 8));
    probe->buffer_select = (uint8_t)(probe->adjusted_page & 0x01);
    probe->buffer_column = (uint8_t)(((probe->adjusted_x >> 4) & 0x0f) |
                                     (probe->buffer_select << 4));
    /* GetBlockBufferAddr leaves the selected bank's low pointer in $06;
     * BlockBufferCollision consumers such as PutPlayerOnVine shift that
     * byte left four times instead of using the adjusted world X. */
    probe->buffer_address_low = (uint8_t)((probe->buffer_select ? 0xd0 : 0x00) |
                                          (probe->buffer_column & 0x0f));

    /* The 6502 masks to a metatile row after subtracting the 32-pixel HUD.
     * Keep the wrapped byte in the result, but do not index outside the
     * modeled 13-row $0500/$05d0 collision buffers. */
    y_sum = (uint16_t)y_position + BlockBuffer_Y_Adder[probe_index];
    probe->aligned_y = (uint8_t)((uint8_t)y_sum & 0xf0);
    probe->aligned_y = (uint8_t)(probe->aligned_y - 0x20);
    row = (uint8_t)(probe->aligned_y >> 4);
    probe->coordinate_low = (uint8_t)((horizontal_return ? x_position :
                                       y_position) & 0x0f);
    if (row >= 13) return 0;

    probe->metatile = s_BlockBuffer[row][probe->buffer_column];
    return probe->metatile;
}

