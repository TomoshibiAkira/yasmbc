/* engine/level/level-internal.h - Shared types/state for split level TUs */
#ifndef SMB_LEVEL_INTERNAL_H
#define SMB_LEVEL_INTERNAL_H

#include "level/level.h"
#include <string.h>

typedef struct { uint16_t addr; uint8_t val; } PendingWrite;

typedef struct {
    uint16_t offset;
    uint8_t length;
    uint8_t mushroom_half_len;
    uint8_t page;
    uint8_t page_select;
    uint8_t first;
    uint8_t second;
    uint8_t row;
    uint8_t category;
    uint8_t dispatch_index;
    uint8_t parameter;
    uint8_t start_col;
    uint8_t span;
    uint8_t horizontal;
    uint8_t active;
} AreaObjectSlot;

#define AREA_OBJECT_LENGTH_EMPTY 0xff

enum AreaObjectProcessResult {
    AREA_OBJECT_NOOP = 0,
    AREA_OBJECT_ACTIVE = 1,
    AREA_OBJECT_CONSUMED = 2,
    /* DecodeAreaData::InitRear stores ObjectOffset=$00 and returns through
     * ChkLength, ending this X=2..0 pass after the slot-0 check. */
    AREA_OBJECT_INIT_REAR_EXIT = 3
};

typedef struct {
    uint8_t active;
    uint8_t page;
    uint8_t start_col;
    uint8_t span;
} CastleBridgeState;

extern uint8_t Bitmasks[8];
extern const uint8_t *const MetatileGfxTables[4];
extern uint8_t s_BlockBuffer[13][32];
extern uint8_t s_BlockSolid[13][32];
extern uint8_t s_AreaDataIndex;
extern uint8_t s_EnemyDataIndex;
extern PendingWrite s_PendingWrites[512];
extern int s_PendingCount;
extern uint8_t s_DeferVRAM;
extern int16_t s_ObjectColFilter;
extern int8_t s_ObjectHalfFilter;
extern uint8_t s_ParserMTBuf[13];
extern uint8_t s_ParserSolidOverride[13];
extern uint8_t s_ParserBlockOverride[13];
extern uint8_t s_ParserCeiling, s_ParserFloor, s_ParserPage, s_ParserCol;
extern uint8_t s_ParserCoreValid, s_ParserComposing, s_ParserObjectMergeValid;
extern uint8_t s_AttributeBuffer[7];
extern AreaObjectSlot s_AreaObjectSlots[3];
extern uint16_t s_AreaObjectCursor;
extern uint8_t s_AreaObjectPageLoc, s_AreaObjectPageSel, s_AreaObjectEnd;
extern uint8_t s_ObjectRenderColumn, s_ObjectRenderHalves;
extern CastleBridgeState s_CastleBridge;

void Level_SetBlockSolid(uint8_t mt_col, uint8_t mt_row, uint8_t solid);
void EmitVRAM(uint16_t addr, uint8_t val);
uint8_t EmitReadVRAM(uint16_t addr);
void GetMetatileTiles(uint8_t metatile, uint8_t tiles[4]);
void WriteMetatileToNT(uint8_t nt_col, uint8_t nt_row, uint8_t metatile);
void WriteAttributeForMetatile(uint8_t nt_col, uint8_t nt_row, uint8_t palette_group);
void AccumulateAttributeForMetatile(uint8_t metatile_row, uint8_t metatile);
void AccumulateAttributeColumn(const uint8_t mtbuf[13]);
void RenderBackgroundScenery(uint8_t page, uint8_t col, uint8_t mtbuf[13]);
void RenderForegroundScenery(uint8_t mtbuf[13]);
uint8_t BlockBufferFiltered(uint8_t metatile);
void CommitBlockBufferColumn(uint8_t page, uint8_t col, const uint8_t mtbuf[13],
                             uint8_t ceiling_byte, uint8_t floor_byte);
void BuildColumnBuffer(uint8_t page, uint8_t col, uint8_t mtbuf[13],
                       uint8_t *ceiling_byte, uint8_t *floor_byte);
void AdvanceCurrentNTAddr(void);
void Level_ApplyAreaAttributes(uint8_t value);
void AppendRenderAreaGraphicsColumn(const uint8_t mtbuf[13], uint8_t right_half);
void Level_PrepassAreaAttributes(uint8_t target_page);
int ParserColumnIsComposed(uint8_t abs_col);
void Level_ProcessAreaDataSlots(uint8_t page, uint8_t col);
void WriteMetatileAtGrid(uint8_t abs_col, uint8_t mt_row, uint8_t metatile);
void WriteNonSolidMetatileAtGrid(uint8_t abs_col, uint8_t mt_row, uint8_t metatile);
void Level_SetRenderedMetatile(uint8_t abs_col, uint8_t mt_row, uint8_t metatile);
uint8_t Level_CurrentRenderedMetatile(uint8_t abs_col, uint8_t mt_row);
void Level_RenderUnderPart(uint8_t abs_col, uint8_t start_row, uint8_t metatile,
                           uint8_t height);

#endif
