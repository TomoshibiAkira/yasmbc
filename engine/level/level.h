/* engine/level/level.h - Level data loading and area parsing */

#ifndef SMB_LEVEL_H
#define SMB_LEVEL_H

#include "constants/types.h"

/* Terrain control bitmasks (TerrainRenderBits in main.asm:1843)
 * Each entry: [ceiling_byte, floor_byte]
 * Ceiling bits checked against Bitmasks[0-7] for metatile rows 0-7
 * Floor bits checked against Bitmasks[0-4] for metatile rows 8-12
 */
extern uint8_t TerrainRenderBits[16][2];
extern uint8_t TerrainMetatiles[4];
extern uint8_t Palette0_MTiles[];
extern uint8_t Palette1_MTiles[];
extern uint8_t Palette2_MTiles[];
extern uint8_t Palette3_MTiles[];
extern uint8_t WarpZoneNumbers[12];

/* Level state */
extern uint8_t g_AreaDataOffset;      /* Current read offset into area data */
extern const uint8_t* g_AreaDataPtr;  /* Pointer to loaded area data */
extern uint16_t g_AreaDataLen;        /* Length of loaded area data */
extern uint16_t s_NTBaseOffset;       /* Nametable base offset (0=NT0, 0x400=NT1) */

/* Physical ProcessAreaData/MetatileBuffer state.  These fields are the
 * direct C projection of the zero-page/RAM owners used by the 6502 parser;
 * AreaObjectSlot's decoded convenience fields are not part of this export. */
typedef struct LevelAreaParserState {
    uint8_t object_page_loc;       /* $072A AreaObjectPageLoc */
    uint8_t object_page_sel;       /* $072B AreaObjectPageSel */
    uint8_t data_offset;           /* $072C AreaDataOffset */
    uint8_t object_offsets[3];     /* $072D-$072F AreaObjOffsetBuffer */
    uint8_t object_lengths[3];     /* $0730-$0732 AreaObjectLength */
    uint8_t block_buffer_column;   /* $06A0 BlockBufferColumnPos */
    uint8_t metatile_buffer[13];   /* $06A1-$06AD MetatileBuffer */
} LevelAreaParserState;

void Level_GetAreaParserState(LevelAreaParserState *state);
/* Encode the 32-column x 13-row host buffer into the two physical NES banks
 * at $0500 and $05d0.  Each bank is row-major: 13 rows x 16 columns. */
void Level_GetVerifierBlockBuffers(uint8_t first[208], uint8_t second[208]);

/* BlockBufferCollision result (main.asm:10361-10400).  The producer keeps
 * the raw metatile and the 6502 scratch values together so consumers do not
 * reconstruct probe coordinates from framebuffer or solidity state. */
typedef struct LevelBlockBufferProbe {
    uint8_t metatile;       /* $03: raw value from $0500/$05d0 */
    uint8_t probe_index;    /* $04 on entry, before the routine's increment */
    uint8_t coordinate_low; /* $04 on return: object X or Y low nibble */
    uint8_t aligned_y;      /* $02: playfield-aligned block-buffer offset */
    uint8_t adjusted_x;     /* $05: object X plus table adder */
    uint8_t adjusted_page;  /* page after the ADC carry */
    uint8_t buffer_select;  /* $06/$07 high-byte selection: 0 or 1 */
    uint8_t buffer_column;  /* selected $0500/$05d0 column, 0..31 */
    uint8_t buffer_address_low; /* $06: low byte before the Y row offset */
} LevelBlockBufferProbe;

/* Functions */
/* The second argument is AreaNumber ($0760), not the HUD LevelNumber.  It
 * indexes the ROM's variable-length WorldNAreas table. */
void Level_Load(uint8_t world, uint8_t area);
int Level_SelectAreaPointer(uint8_t world, uint8_t area);
int Level_LoadAreaPointer(uint8_t pointer);
int Level_AdvanceArea(void);
int Level_SelectWarpZone(uint8_t warp_control, uint8_t player_x);
void Level_ParseHeader(const uint8_t* data);

/* Get the terrain metatile for current area type */
uint8_t Level_GetTerrainMetatile(void);

/* Level header values (for Entrance_GameTimerSetup) */
void Level_BeginDeferred(void);
void Level_RenderColumnRightHalf(uint8_t page, uint8_t col);
void Level_RenderColumnLeftHalf(uint8_t page, uint8_t col);
void Level_AreaParserCore(uint8_t page, uint8_t col);
void Level_RenderAttributeTables(void);
void Level_AreaObjectsReset(void);
void Level_ProcessLoopCommand(void);
uint8_t Level_BlockSolid(uint8_t mt_col, uint8_t mt_row);
uint8_t Level_GetBlockMetatile(uint8_t mt_col, uint8_t mt_row);
uint8_t Level_BlockBufferCollision(LevelBlockBufferProbe *probe,
                                   uint8_t page_loc, uint8_t x_position,
                                   uint8_t y_position, uint8_t probe_index,
                                   uint8_t horizontal_return);
void Level_WriteBlockBufferAddress(uint16_t address, uint8_t value);
void Level_WriteBlockBufferProbe(const LevelBlockBufferProbe *probe,
                                 uint8_t value);
void Level_QueueBlankMetatile(uint8_t abs_col, uint8_t mt_row);
void Level_QueueCoinRemoval(uint8_t abs_col, uint8_t mt_row);
void Level_QueueDestroyedBlockMetatile(uint8_t abs_col, uint8_t mt_row);
void Level_ClearBlockBufferCell(uint8_t abs_col, uint8_t mt_row);
void Level_QueueMetatile(uint8_t abs_col, uint8_t mt_row, uint8_t metatile);
void Level_RegisterCastleBridge(uint8_t page, uint8_t start_col,
                                uint8_t span);
void Level_CollapseCastleBridge(uint8_t address_low);
void Level_FlushDeferred(void);

uint8_t Level_TimerSetting(void);
uint8_t Level_PlayerEntranceCtrl(void);
uint8_t Level_CloudTypeOverride(void);
uint8_t Level_EnemyDataIndex(void);
int Level_LoadRomTables(void);
int Level_LoadPointerTables(void);
int Level_LoadObjectTables(void);
int Level_LoadColumnTables(void);

#endif /* SMB_LEVEL_H */
