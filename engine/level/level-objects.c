/* engine/level/level-objects.c - ProcessAreaData / DecodeAreaData */

#include <string.h>
#include "level/level.h"
#include "level/level-internal.h"
#include "misc.h"
#include "enemy/enemy.h"
#include "collision.h"
#include "scroll.h"
#include "screen/routine/intermediate.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "assets.h"

/* Write a 2×2 metatile to nametable VRAM.
 * nt_col: nametable column (0-31)
 * nt_row: nametable row (0-29), must be even for metatile alignment
 * metatile: metatile ID
 *
 * NES Palette_MTiles format (from DrawMTLoop, main.asm:571-576):
 * The NES renders metatiles column-by-column with vertical PPU increment.
 * Format is column-major: [TL, BL, TR, BR]
 *   tiles[0] = top-left,  tiles[1] = bottom-left
 *   tiles[2] = top-right, tiles[3] = bottom-right
 */
/* When >= 0, object metatiles only render into this column (NES: an
 * object's run contributes to each column as the parser reaches it).
 * s_ObjectHalfFilter is retained for the legacy full-screen object path;
 * AreaParserCore now merges parser-owned objects before RendBBuf and the
 * two RenderAreaGraphics halves consume that one composed source. */
int16_t s_ObjectColFilter = -1;
int8_t s_ObjectHalfFilter = -1;

/* Persistent source state between AreaParserCore and its two render calls.
 * The override arrays are the C representation of RendBBuf's final
 * metatile-valued write into the selected $0500/$05d0 column.  FF means
 * that ProcessAreaData did not replace the terrain result for this row;
 * zero explicitly clears the block-buffer cell for a non-solid decorative
 * object. */
uint8_t s_ParserMTBuf[13];
uint8_t s_ParserSolidOverride[13];
uint8_t s_ParserBlockOverride[13];
uint8_t s_ParserCeiling;
uint8_t s_ParserFloor;
uint8_t s_ParserPage;
uint8_t s_ParserCol;
uint8_t s_ParserCoreValid;
uint8_t s_ParserComposing;
uint8_t s_ParserObjectMergeValid;

/* RenderAreaGraphics accumulates one attribute byte for every two
 * metatile rows.  The seven bytes are the exact SDL owner for the
 * disassembly's AttributeBuffer ($03xx scratch), and are drained only by
 * RenderAttributeTables after the parser's eighth subtask. */
uint8_t s_AttributeBuffer[7];

/* ProcessAreaData ownership (main.asm:2026-2220).  These are the three
 * persistent object slots at AreaObjOffsetBuffer/AreaObjectLength
 * ($072D-$0732), with page/page-select state at $072A/$072B and the stream
 * cursor at AreaDataOffset $072C. */

/* AreaObjectLength is a signed parser flag in the ROM: $ff means that the
 * slot is empty, while 0..$fe is the residual count consumed by the current
 * handler.  Keep that distinction separate from span, which is only the
 * parser's source-column scheduling range. */

AreaObjectSlot s_AreaObjectSlots[3];

uint16_t s_AreaObjectCursor = 0;
uint8_t s_AreaObjectPageLoc;
uint8_t s_AreaObjectPageSel;
uint8_t s_AreaObjectEnd;
uint8_t s_ObjectRenderColumn = 0xff;
uint8_t s_ObjectRenderHalves;

/* CastleBridgeObj owns the world location that BridgeCollapse later maps
 * from BridgeCollapseData's name-table low bytes back to block-buffer
 * columns.  Keeping this as parser state avoids a fixed VRAM address while
 * preserving the ASM's address-table-driven removal order. */

CastleBridgeState s_CastleBridge;

static uint8_t loop_world[11];
static uint8_t loop_page[11];
static uint8_t loop_y[11];
static uint8_t loop_area_offset[11];
static uint8_t BrickQBlockMetatiles[14];
static uint8_t SolidBlockMetatiles[4];
static uint8_t BrickMetatiles[5];
static uint8_t CoinMetatiles[4];
static uint8_t VerticalPipeData[8];
static uint8_t SidePipeShaftData[4];
static uint8_t SidePipeTopPart[4];
static uint8_t SidePipeBottomPart[4];
static uint8_t HoleMetatiles[4];
static uint8_t PulleyRopeMetatiles[3];
static uint8_t CastleMetatiles[55];
static uint8_t StaircaseHeightData[9];
static uint8_t StaircaseRowData[9];
static uint8_t CObjectRow[3];
static uint8_t CObjectMetatile[3];

int Level_LoadObjectTables(void) {
    if (Assets_Copy("tables/loop_cmd_world.bin", loop_world, sizeof(loop_world)) ||
        Assets_Copy("tables/loop_cmd_page.bin", loop_page, sizeof(loop_page)) ||
        Assets_Copy("tables/loop_cmd_y.bin", loop_y, sizeof(loop_y)) ||
        Assets_Copy("tables/area_data_ofs_loopback.bin", loop_area_offset,
                    sizeof(loop_area_offset)) ||
        Assets_Copy("tables/brick_qblock_metatiles.bin", BrickQBlockMetatiles,
                    sizeof(BrickQBlockMetatiles)) ||
        Assets_Copy("tables/solid_block_metatiles.bin", SolidBlockMetatiles,
                    sizeof(SolidBlockMetatiles)) ||
        Assets_Copy("tables/brick_metatiles.bin", BrickMetatiles,
                    sizeof(BrickMetatiles)) ||
        Assets_Copy("tables/coin_metatiles.bin", CoinMetatiles,
                    sizeof(CoinMetatiles)) ||
        Assets_Copy("tables/vertical_pipe.bin", VerticalPipeData,
                    sizeof(VerticalPipeData)) ||
        Assets_Copy("tables/side_pipe_shaft.bin", SidePipeShaftData,
                    sizeof(SidePipeShaftData)) ||
        Assets_Copy("tables/side_pipe_top.bin", SidePipeTopPart,
                    sizeof(SidePipeTopPart)) ||
        Assets_Copy("tables/side_pipe_bottom.bin", SidePipeBottomPart,
                    sizeof(SidePipeBottomPart)) ||
        Assets_Copy("tables/hole_metatiles.bin", HoleMetatiles,
                    sizeof(HoleMetatiles)) ||
        Assets_Copy("tables/pulley_rope_metatiles.bin", PulleyRopeMetatiles,
                    sizeof(PulleyRopeMetatiles)) ||
        Assets_Copy("tables/castle_metatiles.bin", CastleMetatiles,
                    sizeof(CastleMetatiles)) ||
        Assets_Copy("tables/staircase_height.bin", StaircaseHeightData,
                    sizeof(StaircaseHeightData)) ||
        Assets_Copy("tables/staircase_row.bin", StaircaseRowData,
                    sizeof(StaircaseRowData)) ||
        Assets_Copy("tables/c_object_row.bin", CObjectRow, sizeof(CObjectRow)) ||
        Assets_Copy("tables/c_object_metatile.bin", CObjectMetatile,
                    sizeof(CObjectMetatile)))
        return -1;
    return 0;
}



int ParserColumnIsComposed(uint8_t abs_col) {
    return s_ParserComposing &&
           abs_col == (uint8_t)(s_ParserPage * 16 + s_ParserCol);
}

void Level_AreaObjectsReset(void) {
    /* InitializeMemory (main.asm:1417-1450) clears the block-buffer RAM
     * ($0500-$06ff) and its solid-state companion before ProcessAreaData
     * begins the new entrance.  The parser metadata reset must preserve that
     * ownership boundary; otherwise columns beyond the newly rendered
     * ColumnSets retain the previous area's terrain. */
    memset(s_BlockBuffer, 0, sizeof(s_BlockBuffer));
    memset(s_BlockSolid, 0, sizeof(s_BlockSolid));
    memset(s_AreaObjectSlots, 0, sizeof(s_AreaObjectSlots));
    for (int i = 0; i < 3; ++i)
        s_AreaObjectSlots[i].length = AREA_OBJECT_LENGTH_EMPTY;
    s_AreaObjectCursor = g_AreaDataOffset;
    s_AreaObjectPageLoc = 0;
    s_AreaObjectPageSel = 0;
    s_AreaObjectEnd = 0;
    s_ObjectRenderColumn = 0xff;
    s_ObjectRenderHalves = 0;
    /* InitializeArea clears through $074b, including the global
     * StaircaseControl=$0734 alias.  Individual staircase handlers then
     * reinitialize it on their ChkLrgObjLength carry path. */
    g_StaircaseControl = 0;
    memset(s_ParserMTBuf, 0, sizeof(s_ParserMTBuf));
    memset(s_ParserSolidOverride, 0xff, sizeof(s_ParserSolidOverride));
    memset(s_ParserBlockOverride, 0xff, sizeof(s_ParserBlockOverride));
    memset(s_AttributeBuffer, 0, sizeof(s_AttributeBuffer));
    s_ParserComposing = 0;
    s_ParserObjectMergeValid = 0;
    memset(&s_CastleBridge, 0, sizeof(s_CastleBridge));
    g_BehindAreaParserFlag = 0;
    g_LoopCommand = 0;
    g_MultiLoopCorrectCntr = 0;
    g_MultiLoopPassCntr = 0;
}

void Level_GetAreaParserState(LevelAreaParserState *state) {
    int i;

    if (state == NULL)
        return;

    state->object_page_loc = s_AreaObjectPageLoc;
    state->object_page_sel = s_AreaObjectPageSel;
    state->data_offset = g_AreaDataOffset;
    for (i = 0; i < 3; ++i) {
        state->object_offsets[i] = (uint8_t)s_AreaObjectSlots[i].offset;
        state->object_lengths[i] = s_AreaObjectSlots[i].length;
    }
    state->block_buffer_column = g_BlockBufferColumnPos;
    memcpy(state->metatile_buffer, s_ParserMTBuf,
           sizeof(state->metatile_buffer));
}

/* LoopCmdWorldNumber/LoopCmdPageNumber/LoopCmdYPosition and
 * AreaDataOfsLoopback (main.asm:4793-4830).  This is called at the same
 * EnemiesAndLoopsCore empty-slot boundary as ProcLoopCommand; a failed table
 * lookup leaves LoopCommand latched for a later pass. */
void Level_ProcessLoopCommand(void) {
    int8_t match = -1;
    int8_t i;
    uint8_t correct;

    if (g_LoopCommand == 0 || g_CurrentColumnPos != 0)
        return;

    for (i = 10; i >= 0; i--) {
        if (g_WorldNumber == loop_world[(uint8_t)i] &&
            g_CurrentPageLoc == loop_page[(uint8_t)i]) {
            match = i;
            break;
        }
    }
    if (match < 0)
        return;

    correct = (uint8_t)(g_Player_Y_Position == loop_y[(uint8_t)match] &&
                        g_Player_State == PLAYER_STATE_GROUND);
    if (g_WorldNumber == WORLD_7) {
        if (correct)
            g_MultiLoopCorrectCntr++;
        g_MultiLoopPassCntr++;
        if (g_MultiLoopPassCntr != 3) {
            g_LoopCommand = 0;
            return;
        }
        if (g_MultiLoopCorrectCntr == 3) {
            g_MultiLoopPassCntr = 0;
            g_MultiLoopCorrectCntr = 0;
            g_LoopCommand = 0;
            return;
        }
    } else if (correct) {
        g_MultiLoopPassCntr = 0;
        g_MultiLoopCorrectCntr = 0;
        g_LoopCommand = 0;
        return;
    } else {
        /* WrongChk branches directly to DoLpBack for every non-world-7
         * loop.  The loop command is cleared only after ExecGameLoopback
         * and KillAllEnemies, not on this failed-position path. */
    }

    /* ExecGameLoopback: all page aliases move back four pages, then the
     * parser owners restart from the command-specific area-data offset. */
    g_Player_PageLoc = (uint8_t)(g_Player_PageLoc - 4);
    g_CurrentPageLoc = (uint8_t)(g_CurrentPageLoc - 4);
    g_ScreenLeft_PageLoc = (uint8_t)(g_ScreenLeft_PageLoc - 4);
    g_ScreenRight_PageLoc = (uint8_t)(g_ScreenRight_PageLoc - 4);
    s_AreaObjectPageLoc = (uint8_t)(s_AreaObjectPageLoc - 4);
    s_AreaObjectPageSel = 0;
    /* ExecGameLoopback (main.asm:4823-4829) stores the selected absolute
     * AreaDataOfsLoopback byte in AreaDataOffset; it does not add the table
     * entry to the pre-loop cursor. */
    s_AreaObjectCursor = loop_area_offset[(uint8_t)match];
    g_AreaDataOffset = (uint8_t)s_AreaObjectCursor;
    s_AreaObjectEnd = 0;
    /* ExecGameLoopback does not clear AreaObjOffsetBuffer or
     * AreaObjectLength.  Those physical parser slots ($072d-$0732) survive
     * the cursor/page reset and are consumed by the next ProcessAreaData
     * pass (main.asm:4802-4830).  Level_AreaObjectsReset() owns the separate
     * InitializeArea clear at a new entrance. */
    Enemy_ApplyLoopback();
    Enemy_KillAllForLoop();
    Scroll_ApplyLoopback();
    g_MultiLoopPassCntr = 0;
    g_MultiLoopCorrectCntr = 0;
    g_LoopCommand = 0;
}

/* Metatile tables for question blocks and bricks (main.asm:4344-4349) */

/* Hidden1UpBlock/QuestionBlock/BrickWithItem (main.asm:2252-2260,
 * 2959-2990).  The hidden 1-up entry is not an ordinary question block:
 * it is suppressed when Hidden1UpFlag is clear, and when enabled it clears
 * that player-record flag before using BrickWithItem's area-type offset.
 * Keep this decision at the parser owner so both initial and scroll paths
 * consume the same object without inventing a renderer or coordinate flag. */
static int Level_GetSmallObjectMetatile(uint8_t parameter,
                                        uint8_t* metatile) {
    uint8_t area_index;

    if (parameter == 3) { /* Hidden1UpBlock */
        if (g_Hidden1UpFlag == 0)
            return 0;
        /* Hidden1UpBlock stores through the active player record at $075d
         * before entering BrickWithItem (main.asm:2959-2964).  The scalar
         * is the C mirror of that byte; keep both views of the same NES RAM
         * location synchronized at the owning parser boundary. */
        g_OnscreenPlayerInfo[3] = 0; /* Hidden1UpFlag=$075d */
        g_Hidden1UpFlag = 0;
        area_index = (g_AreaType == AREA_TYPE_GROUND) ? 0 : 5;
        *metatile = BrickQBlockMetatiles[(uint8_t)(area_index + parameter)];
        return 1;
    }
    if (parameter <= 3) { /* QuestionBlock, including hidden coin */
        *metatile = BrickQBlockMetatiles[parameter];
        return 1;
    }
    if (parameter <= 8) { /* BrickWithItem / BrickWithCoins */
        area_index = (g_AreaType == AREA_TYPE_GROUND) ? 0 : 5;
        *metatile = BrickQBlockMetatiles[(uint8_t)(area_index + parameter)];
        return 1;
    }
    return 0;
}

/* Solid block metatiles (main.asm:2835) */

/* Brick metatiles for row objects (main.asm:2837-2839) */

/* Coin metatiles (main.asm:2793) */

/* Vertical pipe data (main.asm:2611-2615) */

/* ExitPipe (main.asm:2558-2610).  RenderSidewaysPipe reloads the residual
 * AreaObjectLength into Y before indexing SidePipeShaftData, so only the
 * final two columns receive a vertical shaft.  The decoded low nybble,
 * after the two DEY instructions, remains the shaft height. */

/* Structure tables from main.asm:2445-2476, 2705-2729, 2771-2785. */

/* StaircaseObject (main.asm:2918-2928) uses the single RAM byte at $0734
 * as a parser-global step cursor.  Both parser entry points call this same
 * owner so a new object claims nine only on the ROM carry path, then moves
 * from table index eight down to zero one rendered column at a time. */
static uint8_t Level_StaircaseNextStep(uint8_t initialize) {
    if (initialize)
        g_StaircaseControl = 9;
    g_StaircaseControl--;
    return g_StaircaseControl;
}

/* Write a metatile at a specific metatile grid position.
 * mt_col: metatile column (0-15, maps to PPU col *2)
 * mt_row: metatile row (0-12, maps to PPU row 4 + row*2)
 */
/* Place an object metatile at an ABSOLUTE metatile column (0-31 for the
 * two-nametable window). NT placement derived from the absolute column;
 * the block buffer uses the absolute column as well. */
void WriteMetatileAtGrid(uint8_t abs_col, uint8_t mt_row, uint8_t metatile) {
    if (mt_row > 12) return;
    if (s_ObjectColFilter >= 0 && abs_col != (uint8_t)s_ObjectColFilter) return;
    if (ParserColumnIsComposed(abs_col)) {
        s_ParserMTBuf[mt_row] = metatile;
        s_ParserSolidOverride[mt_row] = 1;
        s_ParserBlockOverride[mt_row] = 0xff;
        return;
    }
    s_BlockBuffer[mt_row][abs_col & 0x1f] = metatile;
    uint16_t saved = s_NTBaseOffset;
    s_NTBaseOffset = (abs_col & 0x10) ? 0x0400 : 0x0000;
    uint8_t nt_col = (abs_col & 0x0F) * 2;
    uint8_t nt_row = 4 + mt_row * 2;
    WriteMetatileToNT(nt_col, nt_row, metatile);
    uint8_t palette_group = (metatile >> 6) & 0x03;
    WriteAttributeForMetatile(nt_col, nt_row, palette_group);
    s_NTBaseOffset = saved;
    /* All placed area objects (bricks, blocks, pipes) are solid */
    Level_SetBlockSolid(abs_col, mt_row, 1);
}

/* RenderUnderPart may place decorative structure metatiles without making
 * them collision objects.  The NES RendBBuf filtering leaves these values
 * out of the block buffer; preserve that distinction in SDL as well. */
void WriteNonSolidMetatileAtGrid(uint8_t abs_col, uint8_t mt_row,
                                        uint8_t metatile) {
    uint16_t saved;
    uint8_t nt_col;
    uint8_t nt_row;
    if (mt_row > 12) return;
    if (s_ObjectColFilter >= 0 && abs_col != (uint8_t)s_ObjectColFilter) return;
    if (ParserColumnIsComposed(abs_col)) {
        s_ParserMTBuf[mt_row] = metatile;
        s_ParserSolidOverride[mt_row] = 0;
        s_ParserBlockOverride[mt_row] = 0;
        return;
    }
    s_BlockBuffer[mt_row][abs_col & 0x1f] = 0;
    saved = s_NTBaseOffset;
    s_NTBaseOffset = (abs_col & 0x10) ? 0x0400 : 0x0000;
    nt_col = (abs_col & 0x0f) * 2;
    nt_row = 4 + mt_row * 2;
    WriteMetatileToNT(nt_col, nt_row, metatile);
    WriteAttributeForMetatile(nt_col, nt_row,
                              (uint8_t)((metatile >> 6) & 0x03));
    s_NTBaseOffset = saved;
    Level_SetBlockSolid(abs_col, mt_row, 0);
}

/* DestroyBlockMetatile/WriteBlockMetatile (main.asm:767-808): queue a
 * blank two-column metatile into VRAM_Buffer1 for the next NMI.  The raw
 * block-buffer byte is a caller-owned distinction: ordinary removal stores
 * zero, while PlayerHeadCollision stores $23 until EnemyToBGCollisionDet
 * consumes the one-frame block-hit marker. */
static void queue_blank_metatile(uint8_t abs_col, uint8_t mt_row,
                                 uint8_t block_buffer_value) {
    uint8_t x = g_VRAM_Buffer1_Offset;
    uint16_t base;
    uint16_t addr;
    if (mt_row >= 13 || x > 0xf0) return;

    s_BlockBuffer[mt_row][abs_col & 0x1f] = block_buffer_value;
    Level_SetBlockSolid(abs_col, mt_row, 0);
    base = 0x2000 + ((abs_col & 0x10) ? 0x0400 : 0x0000);
    addr = base + (uint16_t)(4 + mt_row * 2) * 32 + (abs_col & 0x0f) * 2;
    g_VRAM_Buffer1[x + 0] = (uint8_t)(addr >> 8);
    g_VRAM_Buffer1[x + 1] = (uint8_t)addr;
    g_VRAM_Buffer1[x + 2] = 0x02;
    g_VRAM_Buffer1[x + 3] = 0x24;
    g_VRAM_Buffer1[x + 4] = 0x24;
    g_VRAM_Buffer1[x + 5] = (uint8_t)(addr >> 8);
    g_VRAM_Buffer1[x + 6] = (uint8_t)(addr + 32);
    g_VRAM_Buffer1[x + 7] = 0x02;
    g_VRAM_Buffer1[x + 8] = 0x24;
    g_VRAM_Buffer1[x + 9] = 0x24;
    g_VRAM_Buffer1[x + 10] = 0x00;
    g_VRAM_Buffer1_Offset = (uint8_t)(x + 10);
}

void Level_QueueBlankMetatile(uint8_t abs_col, uint8_t mt_row) {
    queue_blank_metatile(abs_col, mt_row, 0x00);
}

/* RemBridge (main.asm:841-879) writes only the visual blank-metatile
 * commands.  Unlike DestroyBlockMetatile/WriteBlockMetatile, it does not
 * call PutBlockMetatile and therefore must not alter BlockBuffer or the
 * collision-solid mirror.  BridgeCollapse supplies a literal $22 high byte
 * and BridgeCollapseData supplies the low byte; the second command is one
 * nametable row ($20) below the first. */
static void queue_bridge_blank_metatile(uint8_t address_low) {
    uint8_t x = g_VRAM_Buffer1_Offset;

    if (x > 0xf0) return;

    g_VRAM_Buffer1[x + 0] = 0x22;
    g_VRAM_Buffer1[x + 1] = address_low;
    g_VRAM_Buffer1[x + 2] = 0x02;
    g_VRAM_Buffer1[x + 3] = 0x24;
    g_VRAM_Buffer1[x + 4] = 0x24;
    g_VRAM_Buffer1[x + 5] = 0x22;
    g_VRAM_Buffer1[x + 6] = (uint8_t)(address_low + 0x20);
    g_VRAM_Buffer1[x + 7] = 0x02;
    g_VRAM_Buffer1[x + 8] = 0x24;
    g_VRAM_Buffer1[x + 9] = 0x24;
    g_VRAM_Buffer1[x + 10] = 0x00;
    g_VRAM_Buffer1_Offset = (uint8_t)(x + 10);
}

/* RemoveCoin_Axe/PutBlockMetatile (main.asm:760-769, 808-864) writes the
 * erased coin/axe metatile through the fixed $0341 buffer address and then
 * selects VRAM_Buffer2 with VRAM_Buffer_AddrCtrl=$06.  It is distinct from
 * DestroyBlockMetatile/WriteBlockMetatile, which appends to Buffer1. */
void Level_QueueCoinRemoval(uint8_t abs_col, uint8_t mt_row) {
    uint16_t base;
    uint16_t addr;
    uint8_t tile = (g_AreaType == AREA_TYPE_WATER) ? 0x26 : 0x24;

    if (mt_row >= 13) return;

    s_BlockBuffer[mt_row][abs_col & 0x1f] = 0;
    Level_SetBlockSolid(abs_col, mt_row, 0);
    base = 0x2000 + ((abs_col & 0x10) ? 0x0400 : 0x0000);
    addr = base + (uint16_t)(4 + mt_row * 2) * 32 +
           (abs_col & 0x0f) * 2;

    g_VRAM_Buffer2[0] = (uint8_t)(addr >> 8);
    g_VRAM_Buffer2[1] = (uint8_t)addr;
    g_VRAM_Buffer2[2] = 0x02;
    g_VRAM_Buffer2[3] = tile;
    g_VRAM_Buffer2[4] = tile;
    g_VRAM_Buffer2[5] = (uint8_t)(addr >> 8);
    g_VRAM_Buffer2[6] = (uint8_t)(addr + 32);
    g_VRAM_Buffer2[7] = 0x02;
    g_VRAM_Buffer2[8] = tile;
    g_VRAM_Buffer2[9] = tile;
    g_VRAM_Buffer2[10] = 0x00;
    g_VRAM_Buffer2_Offset = 0;
    g_VRAM_Buffer_AddrCtrl = 0x06;
}

/* PlayerHeadCollision writes $23 into the selected block-buffer cell after
 * DestroyBlockMetatile.  This is the object-state handoff consumed by
 * EnemyToBGCollisionDet/HandleEToBGCollision; it is not a fixed coordinate
 * or a renderer-derived flag. */
void Level_QueueDestroyedBlockMetatile(uint8_t abs_col, uint8_t mt_row) {
    queue_blank_metatile(abs_col, mt_row, 0x23);
}

/* HandleEToBGCollision clears the raw $23 marker without issuing another
 * nametable write; BlockObjMT_Updater owns the eventual replacement. */
void Level_ClearBlockBufferCell(uint8_t abs_col, uint8_t mt_row) {
    if (mt_row >= 13) return;
    s_BlockBuffer[mt_row][abs_col & 0x1f] = 0x00;
    Level_SetBlockSolid(abs_col, mt_row, 0);
}

/* ReplaceBlockMetatile/BlockObjMT_Updater (main.asm:4495-4520,
 * 767-808): restore the metatile recorded in the block object's block-buffer
 * slot through VRAM_Buffer1.  The block-buffer model is updated together with
 * the queued nametable tiles so collision sees the replacement after the
 * updater runs. */
void Level_QueueMetatile(uint8_t abs_col, uint8_t mt_row, uint8_t metatile) {
    uint8_t x = g_VRAM_Buffer1_Offset;
    uint8_t tiles[4];
    uint16_t base;
    uint16_t addr;

    if (mt_row >= 13 || x > 0xf0) return;

    s_BlockBuffer[mt_row][abs_col & 0x1f] = metatile;
    Level_SetBlockSolid(abs_col, mt_row, 1);
    GetMetatileTiles(metatile, tiles);
    base = 0x2000 + ((abs_col & 0x10) ? 0x0400 : 0x0000);
    addr = base + (uint16_t)(4 + mt_row * 2) * 32 + (abs_col & 0x0f) * 2;

    g_VRAM_Buffer1[x + 0] = (uint8_t)(addr >> 8);
    g_VRAM_Buffer1[x + 1] = (uint8_t)addr;
    g_VRAM_Buffer1[x + 2] = 0x02;
    g_VRAM_Buffer1[x + 3] = tiles[0];
    g_VRAM_Buffer1[x + 4] = tiles[2];
    g_VRAM_Buffer1[x + 5] = (uint8_t)(addr >> 8);
    g_VRAM_Buffer1[x + 6] = (uint8_t)(addr + 32);
    g_VRAM_Buffer1[x + 7] = 0x02;
    g_VRAM_Buffer1[x + 8] = tiles[1];
    g_VRAM_Buffer1[x + 9] = tiles[3];
    g_VRAM_Buffer1[x + 10] = 0x00;
    g_VRAM_Buffer1_Offset = (uint8_t)(x + 10);
}

void Level_RegisterCastleBridge(uint8_t page, uint8_t start_col,
                                uint8_t span) {
    s_CastleBridge.active = 1;
    s_CastleBridge.page = page;
    s_CastleBridge.start_col = start_col;
    s_CastleBridge.span = span;
}

/* BridgeCollapseData (main.asm:7214-7218) supplies the exact low bytes in
 * the order axe, chain, then the thirteen bridge columns.  RemBridge uses
 * those ROM values directly; its $22 high byte and VRAM-buffer-only writes
 * are part of the BridgeCollapse ASM contract. */
void Level_CollapseCastleBridge(uint8_t address_low) {
    if (!s_CastleBridge.active || s_CastleBridge.span == 0) return;
    queue_bridge_blank_metatile(address_low);
}

/* Write a vertical column of metatiles starting at a grid position.
 * Renders downward from mt_row for 'height' rows.
 */
static void WriteMetatileColumn(uint8_t abs_col, uint8_t mt_row, uint8_t metatile, uint8_t height) {
    for (uint8_t i = 0; i < height && (mt_row + i) <= 12; i++) {
        WriteMetatileAtGrid(abs_col, mt_row + i, metatile);
    }
}

/* GetLrgObjAttrib (main.asm:3084-3093) returns the source low nybble in Y
 * as well as publishing the row in $0007.  The source record is immutable
 * after DecodeAreaData stores its offset, but read it through that offset when
 * available so the C slot has the same pointer ownership as the 6502 code. */
static uint8_t Level_GetLrgObjLengthForSlot(const AreaObjectSlot *slot) {
    uint8_t first = slot->first;
    uint8_t second = slot->second;

    if (g_AreaDataPtr != NULL && slot->offset + 1 < g_AreaDataLen) {
        first = g_AreaDataPtr[slot->offset];
        second = g_AreaDataPtr[slot->offset + 1];
    }
    g_ZeroPageScratch07 = first & 0x0f;
    return second & 0x0f;
}

static uint8_t Level_GetLrgObjAttribForSlot(const AreaObjectSlot *slot) {
    (void)Level_GetLrgObjLengthForSlot(slot);
    return g_ZeroPageScratch07;
}

/* ChkLrgObjLength/ChkLrgObjFixedLength (main.asm:3070-3082) return carry
 * only when the handler claims an empty slot.  The source length is loaded
 * for every call, but an already nonnegative AreaObjectLength remains the
 * authoritative residual byte. */
static uint8_t Level_ChkLrgObjLength(AreaObjectSlot *slot) {
    uint8_t source_length = Level_GetLrgObjLengthForSlot(slot);

    if (slot->length != AREA_OBJECT_LENGTH_EMPTY)
        return 0;
    slot->length = source_length;
    return 1;
}

static uint8_t Level_ChkLrgObjFixedLength(AreaObjectSlot *slot,
                                          uint8_t fixed_length) {
    (void)Level_GetLrgObjLengthForSlot(slot);
    if (slot->length != AREA_OBJECT_LENGTH_EMPTY)
        return 0;
    slot->length = fixed_length;
    return 1;
}

static uint8_t Level_AreaObjectColumnIndex(const AreaObjectSlot *slot) {
    if (slot->length == AREA_OBJECT_LENGTH_EMPTY || slot->span == 0)
        return 0;
    return (uint8_t)(slot->span - 1 - slot->length);
}

/* GetAreaObjYPosition (main.asm:3107-3115) consumes the row left in $0007.
 * This is parser/object state, not a screen-coordinate reconstruction. */
static uint8_t Level_GetAreaObjectYPosition(void) {
    return (uint8_t)((g_ZeroPageScratch07 << 4) + 0x20);
}

/* BulletBillCannon (main.asm:2879-2907).  The length byte is the number of
 * rows below the top piece: the first two rows are $64/$65 and the remaining
 * rows use $66 through RenderUnderPart's normal metatile merge. */
static void RenderBulletBillCannon(const AreaObjectSlot* slot,
                                   uint8_t abs_col) {
    Level_GetLrgObjAttribForSlot(slot);
    uint8_t length = slot->parameter;
    uint8_t x = (uint8_t)((abs_col & 0x0f) << 4);
    uint8_t y = Level_GetAreaObjectYPosition();

    WriteMetatileAtGrid(abs_col, slot->row, 0x64);
    if (length >= 1)
        WriteMetatileAtGrid(abs_col, (uint8_t)(slot->row + 1), 0x65);
    if (length >= 2)
        WriteMetatileColumn(abs_col, (uint8_t)(slot->row + 2), 0x66,
                            (uint8_t)(length - 1));

    /* Cannon_PageLoc/X_Position/Y_Position are persistent producer state,
     * not a renderer-local coordinate. */
    Misc_RegisterCannon(slot->page, x, y);
}

/* RenderUnderPart (main.asm:3040-3068) writes into the shared metatile
 * buffer only when the existing value permits it.  Its collision result is
 * not "every object is solid": RendBBuf later applies BlockBuffLowBounds
 * ($10,$51,$88,$c0) to the final metatile.  Keep this helper at that same
 * parser-buffer boundary so tree/mushroom stems cannot become a boolean
 * collision substitute. */
uint8_t Level_CurrentRenderedMetatile(uint8_t abs_col,
                                             uint8_t mt_row) {
    if (mt_row == 13)
        return Collision_GetHammerEnemyOffset(0);
    if (ParserColumnIsComposed(abs_col))
        return s_ParserMTBuf[mt_row];
    return s_BlockBuffer[mt_row][abs_col & 0x1f];
}

void Level_SetRenderedMetatile(uint8_t abs_col, uint8_t mt_row,
                                      uint8_t metatile) {
    uint8_t filtered;

    if (mt_row == 13) {
        Collision_SetHammerEnemyOffset(0, metatile);
        return;
    }
    if (mt_row > 12) return;
    filtered = BlockBufferFiltered(metatile);
    if (ParserColumnIsComposed(abs_col)) {
        s_ParserMTBuf[mt_row] = metatile;
        s_ParserSolidOverride[mt_row] = (uint8_t)(filtered != 0);
        s_ParserBlockOverride[mt_row] = 0xff;
    } else if (filtered != 0) {
        WriteMetatileAtGrid(abs_col, mt_row, metatile);
    } else {
        WriteNonSolidMetatileAtGrid(abs_col, mt_row, metatile);
    }
}

void Level_RenderUnderPart(uint8_t abs_col, uint8_t start_row,
                                  uint8_t metatile, uint8_t height) {
    uint8_t row = start_row;
    uint8_t remaining = height;
    uint8_t first_iteration = 1;

    for (;;) {
        uint8_t current;
        uint8_t draw = 1;

        /* RenderUnderPart has no bounds check before its first indexed
         * access, but after each store it exits when X reaches $0d.  Thus a
         * call entered with X=$0d may touch the physical HammerEnemyOffset[0]
         * alias once, while a call entered at X<=$0c must stop after row
         * $0c.  Higher indices are outside the live routine contract. */
        if (row > 12 &&
            (!first_iteration || row != 13))
            break;
        current = Level_CurrentRenderedMetatile(abs_col, row);
        if (current == 0x17 || current == 0x1a) {
            draw = 0;
        } else if (current >= 0xc0 && current != 0xc0) {
            draw = 0;
        } else if (current == 0x54 && metatile == 0x50) {
            draw = 0;
        }
        if (draw)
            Level_SetRenderedMetatile(abs_col, row, metatile);

        if (remaining == 0) break;
        remaining--;
        row++;
        first_iteration = 0;
    }
}

/* ExitPipe/RenderSidewaysPipe (main.asm:2558-2610).  The parser stores the
 * fixed four-column length as residual AreaObjectLength=$03..$00.  The
 * encoded low nybble is the vertical height; after the ROM's two DEY
 * instructions it selects both the shaft height and the row at which the
 * two sideways pieces are written. */
static void RenderExitPipeColumn(uint8_t abs_col, uint8_t parameter,
                                 uint8_t residual_length) {
    uint8_t shaft_index;
    uint8_t side_row;

    if (parameter < 2 || (uint8_t)(parameter - 2) >= 13 ||
        residual_length >= sizeof(SidePipeTopPart))
        return;
    shaft_index = (uint8_t)(parameter - 2);
    if (SidePipeShaftData[residual_length] != 0) {
        Level_RenderUnderPart(abs_col, 0,
                              SidePipeShaftData[residual_length], shaft_index);
    }
    side_row = (uint8_t)(shaft_index + 1);
    if (side_row < 12) {
        /* DrawSidePart stores directly into MetatileBuffer after the
         * RenderUnderPart call; the grid helper preserves that parser
         * ownership in both composed and direct rendering paths. */
        WriteMetatileAtGrid(abs_col, side_row,
                            SidePipeTopPart[residual_length]);
        WriteMetatileAtGrid(abs_col, (uint8_t)(side_row + 1),
                            SidePipeBottomPart[residual_length]);
    }
}

/* IntroPipe/RenderSidewaysPipe (main.asm:2556-2570, 2588-2609).  The
 * special-row producer has a fixed four-column residual counter just like
 * ChkLrgObjFixedLength #$03.  RenderSidewaysPipe is entered with Y=$0a,
 * therefore its two DEY instructions give an eight-row shaft and place the
 * side pieces at buffer rows nine and ten.  CMP #$00 leaves carry set when
 * SidePipeShaftData selects no shaft; a shaft path clears carry after
 * RenderUnderPart and then IntroPipe blanks rows zero through six. */
static uint8_t RenderIntroPipeColumn(uint8_t abs_col,
                                     uint8_t residual_length) {
    const uint8_t shaft_height = 8;
    uint8_t shaft;
    uint8_t row;

    if (residual_length >= sizeof(SidePipeTopPart))
        return 1;
    shaft = SidePipeShaftData[residual_length];
    if (shaft != 0)
        Level_RenderUnderPart(abs_col, 0, shaft, shaft_height);

    WriteMetatileAtGrid(abs_col, 9,
                        SidePipeTopPart[residual_length]);
    WriteMetatileAtGrid(abs_col, 10,
                        SidePipeBottomPart[residual_length]);

    /* CMP/BEQ returns carry set for the first two columns.  On the shaft
     * path DrawSidePart reloads Y from $06 (the residual AreaObjectLength),
     * so IntroPipe's final indexed read is VerticalPipeData,Y (main.asm:
     * 2556-2570), yielding $10 then $11 for residuals 1 then 0. */
    if (shaft == 0)
        return 1;
    for (row = 0; row <= 6; row++)
        Level_SetRenderedMetatile(abs_col, row, 0x00);
    Level_SetRenderedMetatile(abs_col, 7, VerticalPipeData[residual_length]);
    return 0;
}

/* AxeObj/ChainObj (main.asm:2814-2832).  The axe is a one-column
 * row-$0d object: ChainObj selects C_ObjectRow[0]=$06 and
 * C_ObjectMetatile[0]=$c5 after AxeObj selects the Bowser palette stream
 * with VRAM_Buffer_AddrCtrl=$08.  Keep the palette request and metatile
 * write at the parser-owned object boundary; the collision consumer later
 * owns the transition when this $c5 cell is touched. */
static void RenderAxeObject(uint8_t abs_col) {
    g_VRAM_Buffer_AddrCtrl = 0x08;
    WriteMetatileAtGrid(abs_col, 6, 0xc5);
}

/* AreaStyleObject (main.asm:2380-2442), styles 0 and 1.  The parser's
 * AreaObjectLength byte is the residual column count, so the C slot span is
 * lower-nibble length + 1 and column_index identifies the first column.
 * MushroomLedgeHalfLen is the per-slot $0736-$0738 companion state. */
static void RenderTreeLedge(AreaObjectSlot* slot, uint8_t abs_col) {
    uint8_t first = Level_ChkLrgObjLength(slot);
    uint8_t column_index = Level_AreaObjectColumnIndex(slot);

    /* TreeLedge uses GetLrgObjAttrib directly and stores Y itself when the
     * parser length is still negative.  This is the handler-local equivalent
     * of ChkLrgObjLength's carry path. */
    if (first)
        slot->length = Level_GetLrgObjLengthForSlot(slot);

    if (column_index == 0) {
        if (slot->page == 0 && slot->start_col == 0) {
            Level_SetRenderedMetatile(abs_col, slot->row, 0x17);
            Level_RenderUnderPart(abs_col, (uint8_t)(slot->row + 1),
                                  0x4c, 0x0f);
        } else {
            Level_RenderUnderPart(abs_col, slot->row, 0x16, 0x00);
        }
        return;
    }
    if (slot->length == 0) {
        Level_RenderUnderPart(abs_col, slot->row, 0x18, 0x00);
        return;
    }
    Level_SetRenderedMetatile(abs_col, slot->row, 0x17);
    Level_RenderUnderPart(abs_col, (uint8_t)(slot->row + 1),
                          0x4c, 0x0f);
}

static void RenderMushroomLedge(AreaObjectSlot* slot, uint8_t abs_col) {
    uint8_t first = Level_ChkLrgObjLength(slot);

    if (first) {
        /* ChkLrgObjLength's carry-set path stores the original length/2. */
        slot->mushroom_half_len = (uint8_t)(slot->length >> 1);
        Level_RenderUnderPart(abs_col, slot->row, 0x19, 0x00);
        return;
    }
    if (slot->length == 0) {
        Level_RenderUnderPart(abs_col, slot->row, 0x1b, 0x00);
        return;
    }
    Level_SetRenderedMetatile(abs_col, slot->row, 0x1a);
    if (slot->length == slot->mushroom_half_len) {
        Level_SetRenderedMetatile(abs_col, (uint8_t)(slot->row + 1),
                                  0x4f);
        Level_RenderUnderPart(abs_col, (uint8_t)(slot->row + 2),
                              0x50, 0x0f);
    }
}

/* ProcessAreaData/DecodeAreaData runtime ownership.  Scroll-time parsing
 * retains the three ASM slots instead of reparsing the whole stream on each
 * tile-column half. */
static void AreaObjectAdvanceSource(void) {
    s_AreaObjectCursor = (uint16_t)(s_AreaObjectCursor + 2);
    /* IncAreaObjOffset (main.asm:2230-2238) advances the live
     * AreaDataOffset zero-page pointer as the persistent object slot is
     * filled.  Keep the public $072c owner synchronized with the cursor;
     * it is consumed by the next parser boundary and by loopback. */
    g_AreaDataOffset = (uint8_t)s_AreaObjectCursor;
    s_AreaObjectPageSel = 0;
}

/* DecodeAreaData (main.asm:2103-2187) owns both the shared zero-page
 * scratch byte and the JumpEngine index.  The index is not equivalent to
 * the encoded d6-d4 category: small objects start at $16, row-$0d objects
 * start at $22, and a vertical pipe with d3 set deliberately dispatches
 * through table entry zero instead of entry seven.  Keep the decoded index
 * in each persistent slot because the renderer runs after DecodeAreaData's
 * A/$07 values have been reused by later parser work. */
static uint8_t DecodeAreaObjectDispatch(uint8_t row, uint8_t second) {
    uint8_t dispatch_offset;
    uint8_t object_id;

    if (row == 0x0f)
        dispatch_offset = 0x10;
    else if (row == 0x0c)
        dispatch_offset = 0x08;
    else
        dispatch_offset = 0x00;
    g_ZeroPageScratch07 = dispatch_offset;

    if (row == 0x0e) {
        /* DecodeAreaData loads #$2e after replacing the initial offset with
         * zero, selecting AlterAreaAttributes directly. */
        g_ZeroPageScratch07 = 0x00;
        return 0x2e;
    }
    if (row == 0x0d) {
        g_ZeroPageScratch07 = 0x22;
        return (uint8_t)(0x22 + (second & 0x3f));
    }
    if (row < 0x0c) {
        object_id = second & 0x70;
        if (object_id == 0) {
            g_ZeroPageScratch07 = 0x16;
            return (uint8_t)(0x16 + (second & 0x0f));
        }
        if (object_id == 0x70 && (second & 0x08) != 0)
            object_id = 0x00;
        return (uint8_t)(object_id >> 4);
    }

    return (uint8_t)(dispatch_offset + ((second & 0x70) >> 4));
}

void Level_ApplyAreaAttributes(uint8_t value) {
    /* AlterAreaAttributes (main.asm:2287-2315). */
    if ((value & 0x40) == 0) {
        g_TerrainControl = value & 0x0f;
        g_BackgroundScenery = (uint8_t)((value & 0x30) >> 4);
    } else {
        uint8_t foreground = value & 0x07;
        if (foreground >= 4) {
            g_BackgroundColorCtrl = foreground;
            foreground = 0;
        }
        g_ForegroundScenery = foreground;
    }
}

/* ScrollLockObject_Warp/ScrollLockObject (main.asm:2319-2339).  The
 * row-$0d object handler owns both the warp-zone selector and the shared
 * ScrollLock latch; it falls through from the warp text/Piranha cleanup into
 * the same XOR toggle used by the two ordinary scroll-lock objects. */
static void Level_ActivateScrollLockObject(uint8_t parameter) {
    if (parameter == 5) {
        uint8_t text_number = 4;

        /* X starts at 4.  Nonzero worlds select the next group, and only
         * ground AreaType ($01) selects its final entry. */
        if (g_WorldNumber != 0) {
            text_number++;
            if (g_AreaType == AREA_TYPE_GROUND)
                text_number++;
        }
        g_WarpZoneControl = text_number;
        Screen_WriteGameText(text_number);
        Enemy_KillByID(PiranhaPlant);
    }

    /* The warp variant falls through to ScrollLockObject in the ROM. */
    g_ScrollLock ^= 0x01;
}

/* Process one empty AreaObjectLength slot.  This intentionally consumes at
 * most one source command: ProcessAreaData advances AreaDataOffset in
 * NextAObj and then decrements ObjectOffset, so assigning multiple commands
 * to one C slot would change the three-slot ownership contract. */
static enum AreaObjectProcessResult AreaObjectActivate(uint16_t target_abs,
                                                        AreaObjectSlot* slot) {
    uint8_t target_page = (uint8_t)(target_abs / 16);
    uint8_t target_col = (uint8_t)(target_abs & 0x0f);

    uint16_t offset;
    uint8_t first;
    uint8_t second;
    uint8_t row;
    uint8_t col;
    uint8_t category;
    uint8_t dispatch_index;
    uint8_t span = 1;

    if (s_AreaObjectEnd || s_AreaObjectCursor + 1 >= g_AreaDataLen)
        return AREA_OBJECT_NOOP;
    offset = s_AreaObjectCursor;
    first = g_AreaDataPtr[offset];
    if (first == 0xfd) {
        s_AreaObjectEnd = 1;
        return AREA_OBJECT_NOOP;
    }
    second = g_AreaDataPtr[offset + 1];
    if ((second & 0x80) != 0 && s_AreaObjectPageSel == 0) {
        s_AreaObjectPageSel = 1;
        s_AreaObjectPageLoc++;
    }
    row = first & 0x0f;
    col = first >> 4;
    dispatch_index = DecodeAreaObjectDispatch(row, second);

    /* Chk1Row13: page-control objects are consumed before CheckRear, but
     * row-$0d objects with d6 set must still obey the page-behind gate. */
    if (row == 0x0d && (second & 0x40) == 0 &&
        s_AreaObjectPageSel == 0) {
        s_AreaObjectPageLoc = second & 0x1f;
        AreaObjectAdvanceSource();
        return AREA_OBJECT_CONSUMED;
    }

    /* CheckRear (main.asm:2070-2079) consumes exactly one behind object and
     * asks ProcessAreaData to repeat the X=2,1,0 pass through $0729. */
    /* Chk1Row14 branches directly to DecodeAreaData while BackloadingFlag is
     * set (main.asm:2065-2069).  After DecodeAreaData, a row-$0e command on
     * either side of the saved start page takes the ROM's StrAObj path
     * (main.asm:2182-2191), so AlterAreaAttributes must run before the
     * ordinary behind-renderer gate can consume it. */
    if (g_BackloadingFlag != 0 && s_AreaObjectPageLoc == target_page) {
        /* DecodeAreaData's InitRear (main.asm:2194-2202) clears the
         * saved-page latch for every decoded object before the ordinary
         * column test; the source record here is row $09, not row $0e. */
        g_BackloadingFlag = 0;
        g_BehindAreaParserFlag = 0;
        return AREA_OBJECT_INIT_REAR_EXIT;
    }
    if (row == 0x0e && g_BackloadingFlag != 0 &&
        s_AreaObjectPageLoc != target_page) {
        /* DecodeAreaData reaches StrAObj before AlterAreaAttributes, so the
         * physical AreaObjOffsetBuffer,X is written even though the
         * length-$ff row-$0e command is only consumed for its attributes
         * (main.asm:2180-2216). */
        slot->offset = offset;
        Level_ApplyAreaAttributes(second);
        AreaObjectAdvanceSource();
        return AREA_OBJECT_CONSUMED;
    }
    if (s_AreaObjectPageLoc < target_page) {
        g_BehindAreaParserFlag = 1;
        AreaObjectAdvanceSource();
        return AREA_OBJECT_CONSUMED;
    }

    /* DecodeAreaData (main.asm:2128-2144) latches a row-$0d loop command
     * after ProcessAreaData's CheckRear gate, but before NormObj performs the
     * same-page column admission.  A behind-page object takes SetBehind and
     * never reaches DecodeAreaData in the ROM. */
    if (row == 0x0d && (second & 0x40) != 0 &&
        (second & 0x7f) == 0x4b)
        g_LoopCommand++;

    if (s_AreaObjectPageLoc > target_page || col != target_col)
        return AREA_OBJECT_NOOP;

    /* InitRear (main.asm:2194-2202) consumes the one-time backloading
     * pass before a same-page object is admitted to an empty slot.  It
     * clears BackloadingFlag and returns without advancing AreaDataOffset;
     * the same source record is decoded on the next ProcessAreaData pass.
     * Rendering it here would repeat a fixed-length object in this column,
     * consuming the vertical-pipe residual counter twice and reversing the
     * first/second sides in the metatile buffer. */
    if (g_BackloadingFlag != 0) {
        g_BackloadingFlag = 0;
        g_BehindAreaParserFlag = 0;
        return AREA_OBJECT_NOOP;
    }

    /* At the owning column, StrAObj stores the source offset and calls
     * IncAreaObjOffset before the LoopCmdE jump target returns. */
    if (row == 0x0d && (second & 0x40) != 0 &&
        (second & 0x7f) == 0x4b) {
        /* StrAObj stores AreaDataOffset before LoopCmdE handles the
         * command (main.asm:2214-2216, 2299). */
        slot->offset = offset;
        AreaObjectAdvanceSource();
        return AREA_OBJECT_CONSUMED;
    }

    if (row == 0x0e) {
        /* DecodeAreaData reaches StrAObj before the row-$0e
         * AlterAreaAttributes handler, so the empty slot still retains this
         * source offset in AreaObjOffsetBuffer,X (main.asm:2180-2216). */
        slot->offset = offset;
        Level_ApplyAreaAttributes(second);
        AreaObjectAdvanceSource();
        return AREA_OBJECT_CONSUMED;
    }

    /* The C slot retains the ROM JumpEngine index while category remains a
     * renderer convenience for the selected table family. */
    if (row <= 11) {
        category = dispatch_index < 0x16 ? dispatch_index : 0;
    } else if (row == 0x0c) {
        category = (uint8_t)(dispatch_index - 0x08);
    } else if (row == 0x0f) {
        category = (uint8_t)(dispatch_index - 0x10);
    } else {
        category = 0xff;
    }
    if (row <= 11) {
        if ((dispatch_index < 0x16 && category == 1 && g_AreaStyle != 2) ||
            (dispatch_index < 0x16 &&
             (category == 2 || category == 3 || category == 4)))
            span = (uint8_t)((second & 0x0f) + 1);
        else if (dispatch_index < 0x16 &&
                 (category == 0 || category == 7))
            span = 2; /* VerticalPipe writes one side per column. */
    } else if (row == 0x0c && category <= 7) {
        span = (uint8_t)((second & 0x0f) + 1);
    } else if (row == 0x0d) {
        uint8_t object_id = second & 0x3f;
        if (object_id == 0)
            span = 4; /* IntroPipe: ChkLrgObjFixedLength #$03. */
        else if (object_id == 4)
            span = 13; /* CastleBridgeObj fixed length. */
    } else if (row == 0x0f) {
        if (category == 2)
            span = 5; /* CastleObject fixed length. */
        else if (category == 3)
            span = (uint8_t)((second & 0x0f) + 1);
        else if (category == 4)
            span = 4; /* ExitPipe: ChkLrgObjFixedLength #$03. */
    }

    memset(slot, 0, sizeof(*slot));
    slot->offset = offset;
    /* DecodeAreaData leaves AreaObjectLength negative.  A handler that needs
     * a residual count claims it through ChkLrgObjLength or
     * ChkLrgObjFixedLength during RenderAreaObjectSlot. */
    slot->length = AREA_OBJECT_LENGTH_EMPTY;
    slot->page = s_AreaObjectPageLoc;
    slot->page_select = s_AreaObjectPageSel;
    slot->first = first;
    slot->second = second;
    slot->row = row;
    slot->category = category;
    slot->dispatch_index = dispatch_index;
    slot->parameter = row == 0x0d ? (second & 0x3f) : (second & 0x0f);
    slot->start_col = col;
    slot->span = span;
    slot->horizontal = span > 1;
    slot->active = 1;
    if (row <= 11 && dispatch_index >= 0x16 && slot->parameter == 11) {
        /* Jumpspring (main.asm:2934-2954) is constructed at decode time,
         * before the parser writes its $67/$68 collision metatiles.  The
         * area-object stream supplies the page, column, and row; the enemy
         * owner derives the same X=(CurrentColumnPos<<4) and
         * Y=(row<<4)+$20 values used by GetAreaObjX/YPosition. */
        Level_GetLrgObjAttribForSlot(slot);
        Enemy_SetupJumpspring(slot->page,
                              (uint8_t)(slot->start_col << 4),
                              Level_GetAreaObjectYPosition());
    } else if (row == 0x0d && slot->parameter == 1) {
        uint16_t object_x = (uint16_t)(((uint16_t)slot->page << 8) |
                                       ((uint16_t)slot->start_col << 4));
        object_x = (uint16_t)(object_x - 0x08);
        Enemy_SetupFlagpole((uint8_t)(object_x >> 8),
                            (uint8_t)object_x);
    } else if (row == 0x0d && slot->parameter == 4) {
        Level_RegisterCastleBridge(slot->page, slot->start_col, slot->span);
    } else if (row == 0x0d && slot->parameter >= 8 &&
               slot->parameter <= 10) {
        /* AreaFrenzy is a producer at DecodeAreaData time, not a
         * renderer-side event.  It writes the shared $06CD queue once for
         * the persistent area-object command. */
        Enemy_AreaFrenzy(slot->parameter);
    } else if (row == 0x0d && slot->parameter >= 5 &&
               slot->parameter <= 7) {
        /* ScrollLockObject_Warp is parameter 5; parameters 6 and 7 use the
         * same handler without the warp-zone text/Piranha cleanup. */
        Level_ActivateScrollLockObject(slot->parameter);
    }
    AreaObjectAdvanceSource();
    return AREA_OBJECT_ACTIVE;
}

static void RenderAreaObjectSlot(AreaObjectSlot* slot, uint8_t abs_col) {
    uint8_t category = slot->category;
    uint8_t parameter = slot->parameter;
    uint8_t column_index = Level_AreaObjectColumnIndex(slot);

    /* Every large/small normal handler and every supported row-$0c/row-$0f
     * handler reaches GetLrgObjAttrib before touching MetatileBuffer.  The
     * row-$0d and row-$0e entries deliberately retain DecodeAreaData's
     * $22/$00 family values because their handlers do not call it. */
    if (slot->row <= 0x0c ||
        (slot->row == 0x0f && category >= 1 && category <= 5))
        Level_GetLrgObjAttribForSlot(slot);

    if (slot->row == 0x0e) {
        /* Row-$0e is normally consumed immediately by AreaObjectActivate;
         * retain this call as a safe owner if an already-buffered slot is
         * encountered during a backloading transition. */
        Level_ApplyAreaAttributes(slot->second);
        return;
    }

    if (slot->row == 0x0d) {
        /* Special-row jump table at main.asm:2261-2279.  These are the
         * static metatile portions; dynamic flag, axe, frenzy, and scroll
         * lock state belongs to their object handlers. */
        switch (parameter) {
            case 0: /* IntroPipe */
                Level_ChkLrgObjFixedLength(slot, 3);
                RenderIntroPipeColumn(abs_col, slot->length);
                break;
            case 2: /* AxeObj → ChainObj, C_ObjectRow[0]/Metatile[0]. */
                RenderAxeObject(abs_col);
                break;
            case 1: /* FlagpoleObject */
                /* FlagpoleObject (main.asm:2744-2752) writes the ball
                 * directly, then enters RenderUnderPart with X=$01, Y=$08,
                 * and A=$25.  RenderUnderPart's result is subsequently
                 * filtered by RendBBuf/BlockBuffLowBounds, so the climbable
                 * shaft remains $25 in Block_Buffer_1/$05d0; it is not a
                 * decorative/non-solid write. */
                WriteMetatileAtGrid(abs_col, 0, 0x24);
                Level_RenderUnderPart(abs_col, 1, 0x25, 0x08);
                WriteMetatileAtGrid(abs_col, 10, 0x61);
                break;
            case 3: /* ChainObj, C_ObjectRow[1] */
                WriteMetatileAtGrid(abs_col, CObjectRow[1],
                                    CObjectMetatile[1]);
                break;
            case 4: /* CastleBridgeObj, C_ObjectRow[2] */
                Level_ChkLrgObjFixedLength(slot, 12);
                WriteMetatileAtGrid(abs_col, CObjectRow[2],
                                    CObjectMetatile[2]);
                break;
            default:
                break;
        }
        return;
    }

    if (slot->row == 0x0c) {
        /* Hole_*, PulleyRopeObject, Bridge_* and question-row branches
         * (main.asm:2445-2460, 2686-2730, 2846-2860). */
        switch (category) {
            case 0: { /* Hole_Empty */
                uint8_t first = Level_ChkLrgObjLength(slot);

                /* Hole_Empty performs its whirlpool producer on the same
                 * carry-set first pass as ChkLrgObjLength.  The parser slot's
                 * source page/column remains the only coordinate owner. */
                if (first && g_AreaType == AREA_TYPE_WATER) {
                    uint16_t left = (uint16_t)(((uint16_t)slot->page << 8) |
                                               ((uint16_t)slot->start_col << 4));
                    left = (uint16_t)(left - 0x10);
                    Misc_RegisterWhirlpool((uint8_t)(left >> 8),
                                           (uint8_t)left,
                                           (uint8_t)((slot->length + 2) << 4));
                }
                for (uint8_t row = 8; row < 13; row++)
                    WriteNonSolidMetatileAtGrid(abs_col, row,
                                                HoleMetatiles[g_AreaType & 3]);
                break;
            }
            case 1: { /* PulleyRopeObject */
                uint8_t first = Level_ChkLrgObjLength(slot);
                uint8_t part = first ? 0
                    : (slot->length == 0 ? 2 : 1);
                WriteNonSolidMetatileAtGrid(abs_col, 0,
                                            PulleyRopeMetatiles[part]);
                break;
            }
            case 2: /* Bridge_High */
            case 3: /* Bridge_Middle */
            case 4: { /* Bridge_Low */
                Level_ChkLrgObjLength(slot);
                uint8_t row = category == 2 ? 6 : (category == 3 ? 7 : 9);
                WriteMetatileAtGrid(abs_col, row, 0x0b);
                WriteMetatileAtGrid(abs_col, row + 1, 0x63);
                break;
            }
            case 5: /* Hole_Water */
                Level_ChkLrgObjLength(slot);
                WriteNonSolidMetatileAtGrid(abs_col, 10, 0x86);
                WriteNonSolidMetatileAtGrid(abs_col, 11, 0x87);
                WriteNonSolidMetatileAtGrid(abs_col, 12, 0x87);
                break;
            case 6: /* QuestionBlockRow_High */
            case 7: /* QuestionBlockRow_Low */
                Level_ChkLrgObjLength(slot);
                WriteMetatileAtGrid(abs_col, category == 6 ? 3 : 7, 0xc0);
                break;
            default:
                break;
        }
        return;
    }
    if (slot->row == 0x0f) {
        /* EndlessRope, BalancePlatRope, CastleObject, StaircaseObject,
         * ExitPipe, and residual flag balls (main.asm:2558-2610,
         * 2771-2808). */
        if (category == 0) {
            /* EndlessRope loads X=$00, Y=$0f, A=$40 and enters
             * RenderUnderPart.  The merge routine must retain an existing
             * ledge/question/palette-3 metatile instead of blindly replacing
             * it, even though the rope itself is non-solid. */
            Level_RenderUnderPart(abs_col, 0, 0x40, 0x0f);
        } else if (category == 1) {
            /* BalancePlatRope first blanks rows 1..12 through the same
             * RenderUnderPart filter, then reloads the source low nybble via
             * GetLrgObjAttrib and draws the rope from row 1 for that many
             * additional rows (inclusive). */
            Level_RenderUnderPart(abs_col, 1, 0x44, 0x0f);
            (void)Level_GetLrgObjAttribForSlot(slot);
            Level_RenderUnderPart(abs_col, 1, 0x40, parameter);
        } else if (category == 2) {
            Level_ChkLrgObjFixedLength(slot, 4);
            /* GetLrgObjAttrib returns the second-byte low nybble in Y and
             * CastleObject immediately executes STY $07, so that object
             * parameter is the destination row (main.asm:2478-2487). */
            uint8_t row = parameter;
            /* CastleObject (main.asm:2499-2526) indexes the 55-byte table
             * with the residual AreaObjectLength byte, then advances five
             * entries for each successive metatile row.  The residual is
             * initialized to four and decremented by ChkLength after each
             * rendered column; it is not the zero-based column index. */
            uint8_t index = slot->length;
            while (row < 11 && index < sizeof(CastleMetatiles)) {
                uint8_t mt = CastleMetatiles[index];
                if (mt != 0)
                    WriteMetatileAtGrid(abs_col, row, mt);
                else
                    WriteNonSolidMetatileAtGrid(abs_col, row, 0);
                row++;
                index = (uint8_t)(index + 5);
            }

            /* CastleObject (main.asm:2499-2531) creates the StarFlagObject
             * on the third column of a non-tall castle.  Its two adjacent
             * length gates also write the floor-stop brick; keep those
             * parser-owned metatile writes beside the shared enemy producer. */
            if (g_CurrentPageLoc != 0) {
                if (slot->length == 1) {
                    WriteMetatileAtGrid(abs_col, 10, 0x52);
                } else if (parameter == 0 && slot->length == 3) {
                    WriteMetatileAtGrid(abs_col, 10, 0x52);
                } else if (slot->length == 2) {
                    uint8_t x = (uint8_t)((g_CurrentColumnPos & 0x0f) << 4);
                    Enemy_SetupStarFlag(g_CurrentPageLoc, x, 0x90);
                }
            }
        } else if (category == 3) {
            /* StaircaseObject (main.asm:2918-2930) owns one global
             * StaircaseControl=$0734, not a per-slot column counter.  The
             * first ChkLrgObjLength carry initializes it to nine; every
             * rendered column decrements it before indexing the original
             * row/height tables.  Keep the guard only for malformed source
             * lengths that would wrap the 6502 index outside those tables. */
            uint8_t first = Level_ChkLrgObjLength(slot);
            uint8_t step = Level_StaircaseNextStep(first);
            if (step < sizeof(StaircaseRowData))
                Level_RenderUnderPart(abs_col, StaircaseRowData[step], 0x61,
                                      StaircaseHeightData[step]);
        } else if (category == 4) {
            Level_ChkLrgObjFixedLength(slot, 3);
            column_index = Level_AreaObjectColumnIndex(slot);
            RenderExitPipeColumn(abs_col, parameter, slot->length);
        } else if (category == 5) {
            for (uint8_t row = 2; row <= (uint8_t)(2 + parameter) && row < 13; row++)
                WriteNonSolidMetatileAtGrid(abs_col, row, 0x6d);
        }
        return;
    }
    if (slot->row > 11)
        return; /* row-$0d specials and row-$0e attributes are deferred. */

    if (slot->dispatch_index >= 0x16) {
        uint8_t metatile;
        if (parameter <= 8) {
            if (!Level_GetSmallObjectMetatile(parameter, &metatile))
                return; /* Hidden1UpBlock: ExitDecBlock */
        }
        else if (parameter == 9) {
            /* WaterPipe (main.asm:2541-2549). */
            WriteMetatileAtGrid(abs_col, slot->row, 0x6b);
            WriteMetatileAtGrid(abs_col, (uint8_t)(slot->row + 1), 0x6c);
            return;
        } else if (parameter == 10) {
            /* EmptyBlock (main.asm:2824-2831). */
            metatile = 0xc4;
        } else if (parameter == 11) {
            /* Jumpspring (main.asm:2934-2951): the two collision metatiles
             * are the parser-owned portion; its enemy-object animation is a
             * separate owner at the GameEngine object-pass boundary. */
            WriteMetatileAtGrid(abs_col, slot->row, 0x67);
            WriteMetatileAtGrid(abs_col, (uint8_t)(slot->row + 1), 0x68);
            return;
        } else {
            return;
        }
        if (parameter == 7) {
            /* BrickWithCoins clears the shared $06BC latch when this
             * parser object is decoded (main.asm:2970-2978). */
            g_BrickCoinTimerFlag = 0;
        }
        WriteMetatileAtGrid(abs_col, slot->row, metatile);
        return;
    }

    switch (category) {
        case 1:
            switch (g_AreaStyle) {
                case 0:
                    RenderTreeLedge(slot, abs_col);
                    break;
                case 1:
                    RenderMushroomLedge(slot, abs_col);
                    break;
                case 2:
                    RenderBulletBillCannon(slot, abs_col);
                    break;
                default:
                    break;
            }
            break;
        case 2: {
            Level_ChkLrgObjLength(slot);
            uint8_t mt = BrickMetatiles[
                g_CloudTypeOverride ? 4 : (g_AreaType & 0x03)];
            WriteMetatileAtGrid(abs_col, slot->row, mt);
            break;
        }
        case 3:
            Level_ChkLrgObjLength(slot);
            WriteMetatileAtGrid(abs_col, slot->row,
                                SolidBlockMetatiles[g_AreaType & 0x03]);
            break;
        case 4:
            Level_ChkLrgObjLength(slot);
            WriteMetatileAtGrid(abs_col, slot->row,
                                CoinMetatiles[g_AreaType & 0x03]);
            break;
        case 5:
            WriteMetatileColumn(abs_col, slot->row,
                                BrickMetatiles[g_AreaType & 0x03],
                                (uint8_t)(parameter + 1));
            break;
        case 6:
            WriteMetatileColumn(abs_col, slot->row,
                                SolidBlockMetatiles[g_AreaType & 0x03],
                                (uint8_t)(parameter + 1));
            break;
        case 0:
        case 7: {
            Level_ChkLrgObjFixedLength(slot, 1);
            column_index = Level_AreaObjectColumnIndex(slot);
            /* DecodeAreaData (main.asm:2157-2166) preserves the vertical
             * pipe selector for decoration pipes, but clears it when the
             * d3 usage-control bit is set.  VerticalPipe then treats the
             * cleared selector as the warp-pipe table entry. */
            /* DecodeAreaData sends d3-set $70 objects through JumpEngine
             * entry 0 (warp pipe) and d3-clear objects through entry 7
             * (decoration pipe).  Use the selected ROM entry, not a second
             * independent interpretation of the command byte. */
            uint8_t pipe_table = category == 0 ? 0 : 4;
            uint8_t pipe_y = (uint8_t)(1 - column_index);
            uint8_t top = VerticalPipeData[pipe_table + pipe_y];
            uint8_t shaft = VerticalPipeData[pipe_table + pipe_y + 2];

            /* VerticalPipe performs its one-shot producer before drawing the
             * first side.  World/Area 0-1 deliberately has no plant; every
             * other area derives the page/X/Y from the parser-owned object
             * coordinates and lets the enemy owner scan FindEmptyEnemySlot. */
            if (column_index == 0 &&
                (g_WorldNumber != 0 || g_AreaNumber != 0)) {
                uint16_t object_x = (uint16_t)(((uint16_t)slot->page << 8) |
                                               ((uint16_t)slot->start_col << 4));
                object_x = (uint16_t)(object_x + 0x08);
                Enemy_SetupPiranhaPlant((uint8_t)(object_x >> 8),
                                        (uint8_t)object_x,
                                        Level_GetAreaObjectYPosition());
            }
            WriteMetatileAtGrid(abs_col, slot->row, top);
            WriteMetatileColumn(abs_col, slot->row + 1, shaft,
                                (uint8_t)(parameter & 0x07));
            break;
        }
        default:
            /* AreaStyleObject and other decoder branches remain explicit
             * deferred cases until their own metatile-buffer routines land. */
            break;
    }
}

void Level_ProcessAreaDataSlots(uint8_t page, uint8_t col) {
    uint16_t target_abs = (uint16_t)page * 16 + col;
    uint16_t previous_cursor;
    uint8_t previous_page;
    uint8_t previous_backloading;
    int i;

    if (!g_AreaDataPtr || g_AreaDataLen < 1)
        return;

    s_ObjectRenderColumn = (uint8_t)target_abs;
    s_ObjectRenderHalves = 0;

    /* ProcessAreaData (main.asm:2026-2092) repeats the complete X=2,1,0
     * pass when an object was behind the renderer or while the initial
     * backloading flag remains set.  The guard only handles malformed input
     * with no state progress; valid ROM streams always advance or clear one
     * of these latches before repeating. */
    for (;;) {
        previous_cursor = s_AreaObjectCursor;
        previous_page = s_AreaObjectPageLoc;
        previous_backloading = g_BackloadingFlag;
        g_BehindAreaParserFlag = 0;
        s_ObjectColFilter = (int16_t)target_abs;
        s_ObjectHalfFilter = -1;

        /* ObjectOffset starts at 2 and decrements after each source/slot
         * decision.  DecodeAreaData's renderer is called before ChkLength,
         * so active and newly decoded objects share this exact ordering. */
        for (i = 2; i >= 0; i--) {
            AreaObjectSlot* slot = &s_AreaObjectSlots[i];
            uint16_t start;
            enum AreaObjectProcessResult result = AREA_OBJECT_NOOP;

            if (slot->active && target_abs >
                (uint16_t)slot->page * 16 + slot->start_col +
                    slot->span - 1)
                slot->active = 0;
            if (!slot->active)
                result = AreaObjectActivate(target_abs, slot);

            if (result == AREA_OBJECT_INIT_REAR_EXIT) {
                /* InitRear (main.asm:2194-2202) forces ObjectOffset=0 and
                 * returns to ChkLength.  Reproduce that one final physical
                 * AreaObjectLength[0] check, then end this parser pass;
                 * ProcessAreaData will retry the same source offset with the
                 * cleared BackloadingFlag. */
                if ((s_AreaObjectSlots[0].length & 0x80) == 0) {
                    s_AreaObjectSlots[0].length--;
                    if (s_AreaObjectSlots[0].length ==
                        AREA_OBJECT_LENGTH_EMPTY)
                        s_AreaObjectSlots[0].active = 0;
                }
                s_ObjectColFilter = -1;
                return;
            }

            if (result == AREA_OBJECT_ACTIVE || slot->active) {
                start = (uint16_t)slot->page * 16 + slot->start_col;
                if (target_abs >= start && target_abs < start + slot->span) {
                    RenderAreaObjectSlot(slot, (uint8_t)target_abs);
                    /* ChkLength decrements only a nonnegative
                     * AreaObjectLength.  A handler that never claims a
                     * length leaves the ROM sentinel negative and is a
                     * one-column/one-pass object. */
                    if (slot->length == AREA_OBJECT_LENGTH_EMPTY)
                        slot->active = 0;
                    else if (slot->length != 0)
                        slot->length--;
                    else {
                        /* ChkLength always executes DEC for a nonnegative
                         * AreaObjectLength.  A final residual zero therefore
                         * wraps to the physical empty sentinel $FF before
                         * the slot is retired (main.asm:2081-2088). */
                        slot->length = AREA_OBJECT_LENGTH_EMPTY;
                        slot->active = 0;
                    }
                }
            }
        }
        s_ObjectColFilter = -1;

        if (g_BehindAreaParserFlag == 0 && g_BackloadingFlag == 0)
            break;
        if (previous_cursor == s_AreaObjectCursor &&
            previous_page == s_AreaObjectPageLoc &&
            previous_backloading == g_BackloadingFlag &&
            g_BehindAreaParserFlag == 0)
            break;
    }
}

/* Header accessors for game core (Entrance_GameTimerSetup) */
uint8_t Level_TimerSetting(void) { return g_GameTimerSetting; }
uint8_t Level_PlayerEntranceCtrl(void) { return g_PlayerEntranceCtrl; }
uint8_t Level_CloudTypeOverride(void) { return g_CloudTypeOverride; }
uint8_t Level_EnemyDataIndex(void) { return s_EnemyDataIndex; }
