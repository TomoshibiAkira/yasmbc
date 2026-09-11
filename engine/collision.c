/* engine/collision.c - Tile-based collision detection
 *
 * Uses VRAM nametable data to determine ground/solid collisions.
 * Replaces the hardcoded ground at Y=$A0 with actual level data collision.
 *
 * NES reference: PlayerBGCollision (main.asm)
 *   - Uses Block_Buffer_1/2 (208-byte collision maps from area parser)
 *   - Checks 6-8 bounding box points per frame
 *   - Distinguishes feet/head/side collisions
 *
 * The C port keeps the original Block_Buffer collision domain; presentation
 * tiles are never used as a second gameplay collision oracle.
 */

#include "collision.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "level/level.h"
#include "enemy/enemy.h"
#include "sprite-offsets.h"
#include "spr-object.h"
#include "score.h"
#include "screen/routine/hud.h"
#include <string.h>
#include "assets.h"

/* BoundBoxCtrlData (main.asm:10066-10078).  BoundingBoxCore writes the
 * player entry at $04ac-$04af; PlayerCollisionCore later reads those bytes
 * without recomputing them from the current relative position. */
static uint8_t s_PlayerBoundBoxCtrlData[12][4];
static uint8_t hammer_enemy_offsets[9];
static uint8_t first_x[4];
static uint8_t first_y[4];
static uint8_t second_x[4];
static uint8_t second_y[4];
static uint8_t first_tile[4];
static uint8_t second_tile[4];
static uint8_t attributes[4];
static uint8_t hammer_x_speed[2];
static uint8_t s_ClimbAdderWindow[6];
static uint8_t flagpole_y[5];
static uint8_t brick_qblock_metatiles[14];
static uint8_t DefaultBlockObjTiles[4];
static uint8_t SolidMTileUpperExt[4];
static uint8_t ClimbMTileUpperExt[4];
static uint8_t BlockYPosAdderData[2];
static uint8_t BlockBufferAdderData[3];
static uint8_t MaxSpdBlockData[2];
static uint8_t JumpingCoinTiles[4];

int Collision_LoadRom(void) {
    if (Assets_Copy("tables/bound_box_ctrl.bin", s_PlayerBoundBoxCtrlData,
                    sizeof(s_PlayerBoundBoxCtrlData)) ||
        Assets_Copy("tables/hammer_enemy_ofs.bin", hammer_enemy_offsets,
                    sizeof(hammer_enemy_offsets)) ||
        Assets_Copy("tables/first_spr_x.bin", first_x, sizeof(first_x)) ||
        Assets_Copy("tables/first_spr_y.bin", first_y, sizeof(first_y)) ||
        Assets_Copy("tables/second_spr_x.bin", second_x, sizeof(second_x)) ||
        Assets_Copy("tables/second_spr_y.bin", second_y, sizeof(second_y)) ||
        Assets_Copy("tables/first_spr_tile.bin", first_tile, sizeof(first_tile)) ||
        Assets_Copy("tables/second_spr_tile.bin", second_tile,
                    sizeof(second_tile)) ||
        Assets_Copy("tables/hammer_spr_attrib.bin", attributes,
                    sizeof(attributes)) ||
        Assets_Copy("tables/hammer_x_spd.bin", hammer_x_speed,
                    sizeof(hammer_x_speed)) ||
        Assets_Copy("compat-rom-windows/climb_adder.bin", s_ClimbAdderWindow,
                    sizeof(s_ClimbAdderWindow)) ||
        Assets_Copy("tables/flagpole_ypos.bin", flagpole_y, sizeof(flagpole_y)) ||
        Assets_Copy("tables/brick_qblock_metatiles.bin", brick_qblock_metatiles,
                    sizeof(brick_qblock_metatiles)) ||
        Assets_Copy("tables/default_block_obj_tiles.bin", DefaultBlockObjTiles,
                    sizeof(DefaultBlockObjTiles)) ||
        Assets_Copy("tables/solid_mtile_upper.bin", SolidMTileUpperExt,
                    sizeof(SolidMTileUpperExt)) ||
        Assets_Copy("tables/climb_mtile_upper.bin", ClimbMTileUpperExt,
                    sizeof(ClimbMTileUpperExt)) ||
        Assets_Copy("tables/block_ypos_adder.bin", BlockYPosAdderData,
                    sizeof(BlockYPosAdderData)) ||
        Assets_Copy("tables/block_buffer_adder.bin", BlockBufferAdderData,
                    sizeof(BlockBufferAdderData)) ||
        Assets_Copy("tables/max_spd_block.bin", MaxSpdBlockData,
                    sizeof(MaxSpdBlockData)) ||
        Assets_Copy("tables/jumping_coin_tiles.bin", JumpingCoinTiles,
                    sizeof(JumpingCoinTiles)))
        return -1;
    return 0;
}


void Collision_UpdatePlayerBoundingBox(void) {
    const uint8_t *data;

    if (g_Player_BoundBoxCtrl >= 12) {
        g_Player_BoundingBox[0] = 0xff;
        g_Player_BoundingBox[1] = 0xff;
        g_Player_BoundingBox[2] = 0xff;
        g_Player_BoundingBox[3] = 0xff;
        return;
    }
    data = s_PlayerBoundBoxCtrlData[g_Player_BoundBoxCtrl];
    g_Player_BoundingBox[0] = (uint8_t)(g_Player_Rel_XPos + data[0]);
    g_Player_BoundingBox[1] = (uint8_t)(g_Player_Rel_YPos + data[1]);
    g_Player_BoundingBox[2] = (uint8_t)(g_Player_Rel_XPos + data[2]);
    g_Player_BoundingBox[3] = (uint8_t)(g_Player_Rel_YPos + data[3]);
}

/* PlayerHeadCollision/BlockObjectsCore state (main.asm:4191-4490).
 * SprDataOffset_Ctrl ($03ee) selects one of two persistent block records;
 * BlockObjectsCore later visits those records in slot order 1, then 0.
 * Keep the block-buffer origin and replacement flag in the same record so
 * BlockObjMT_Updater does not borrow player or misc-object state. */
#define BLOCK_SLOT_COUNT 2
typedef struct BlockChunkState {
    /* The second brick chunk is the +2 parallel SprObject record used by
     * SpawnBrickChunks/BlockObjectsCore.  The first chunk remains in the
     * enclosing BlockObjectState fields, just as it does in NES RAM. */
    uint8_t state; /* Block_State+2: not written by SpawnBrickChunks. */
    uint8_t page;
    uint8_t x;
    uint8_t y;
    uint8_t y_high;
    uint8_t rel_x;
    uint8_t rel_y;
    uint8_t offscreen_bits;
    uint8_t x_speed;
    uint8_t x_move_force;
    uint8_t y_speed;
    uint8_t y_move_force;
    uint8_t y_mf_dummy;
} BlockChunkState;

typedef struct BlockObjectState {
    uint8_t active;
    uint8_t page;
    uint8_t page2;
    uint8_t x;
    uint8_t y;
    uint8_t y_high;
    uint8_t rel_x;
    uint8_t rel_y;
    uint8_t offscreen_bits;
    uint8_t state;
    uint8_t metatile;
    uint8_t x_speed;
    uint8_t x_move_force;
    uint8_t y_speed;
    uint8_t y_move_force;
    uint8_t y_mf_dummy;
    uint8_t buffer_col;
    uint8_t buffer_row;
    /* Block_Orig_YPos/Block_BBuf_Low are the raw values saved by
     * PlayerHeadCollision before CheckTopOfBlock changes its scratch
     * registers.  Keep them separate from the host grid coordinates above. */
    uint8_t orig_y;
    uint8_t bbuf_low;
    uint8_t restore_pending;
    uint8_t original_x;
    BlockChunkState lower_chunk;
} BlockObjectState;

static BlockObjectState s_BlockObjects[BLOCK_SLOT_COUNT];
#define MISC_SLOT_COUNT 4 /* Misc_State slots $05..$08. */
#define MISC_RAW_SLOT_COUNT 9 /* Misc_State offsets $00..$08. */
#define MISC_PARALLEL_SLOT_OFFSET 0x0d /* JCoinRun's TXA/ADC #$0d. */
#define MISC_PARALLEL_SLOT_COUNT \
    (MISC_RAW_SLOT_COUNT + MISC_PARALLEL_SLOT_OFFSET)
static uint8_t s_CoinActive[MISC_SLOT_COUNT];
static uint16_t s_CoinWorldX[MISC_SLOT_COUNT];
static uint8_t s_CoinY[MISC_SLOT_COUNT];
static uint8_t s_CoinYHigh[MISC_SLOT_COUNT];
static uint8_t s_CoinRelX[MISC_SLOT_COUNT], s_CoinRelY[MISC_SLOT_COUNT];
static uint8_t s_CoinOffscreenBits[MISC_SLOT_COUNT];
static uint8_t s_CoinSpeed[MISC_SLOT_COUNT];
static uint8_t s_CoinState[MISC_SLOT_COUNT];

/* SprObject_Y_MoveForce ($0433) and SprObject_YMF_Dummy ($0416) are
 * parallel banks shared by every SprObject owner.  Hammer processing enters
 * ImposeGravity after TXA/ADC #$0d (ProcHammerObj main.asm:3865-3876;
 * JCoinRun main.asm:4008-4019), so both use the second bank view at raw+$0d.
 * These
 * bytes are not part of a coin or hammer lifetime: SetupJumpCoin and
 * SpawnHammerObj leave them untouched, and a later object reuses the bytes at
 * the same parallel offset. */
static uint8_t s_MiscYMoveForce[MISC_PARALLEL_SLOT_COUNT];
static uint8_t s_MiscYmfDummy[MISC_PARALLEL_SLOT_COUNT];

/* Misc_State/$2a and the parallel Misc_* arrays are one nine-entry bank.
 * Coin records use the four FindEmptyMiscSlot entries ($05-$08); hammer
 * records may use the full raw range selected by HammerEnemyOfsData. */
typedef struct MiscHammerState {
    uint8_t state;             /* Misc_State, d7 marks a hammer */
    uint8_t source_slot;       /* HammerEnemyOffset=$06ae */
    uint8_t bbox_ctrl;         /* Misc_BoundBoxCtrl=$04a2 */
    uint8_t page, x;           /* Misc_PageLoc/$7a, Misc_X_Position/$93 */
    uint8_t y_high, y;         /* Misc_Y_HighPos/$c2, Misc_Y_Position/$db */
    uint8_t rel_x, rel_y;      /* Misc_Rel_XPos/$03b3, Rel_YPos/$03be */
    uint8_t offscreen_bits;    /* Misc_OffscreenBits=$03d6 */
    uint8_t bbox_ul_x, bbox_ul_y; /* EnemyBoundingBoxCoord[$24*slot+0/1] */
    uint8_t bbox_lr_x, bbox_lr_y; /* EnemyBoundingBoxCoord[$24*slot+2/3] */
    uint8_t x_speed, x_move_force;
    uint8_t y_speed;
    uint8_t collision_flag;    /* Misc_Collision_Flag=$06be */
} MiscHammerState;

static MiscHammerState s_Hammers[MISC_RAW_SLOT_COUNT];

void Collision_GetVerifierState(CollisionVerifierState *out) {
    unsigned i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < BLOCK_SLOT_COUNT; ++i) {
        const BlockObjectState *b = &s_BlockObjects[i];
        unsigned j = i + BLOCK_SLOT_COUNT;
        out->block_state[i] = b->state;
        out->block_page[i] = b->page;
        out->block_x[i] = b->x;
        out->block_y_high[i] = b->y_high;
        out->block_y[i] = b->y;
        out->block_x_speed[i] = b->x_speed;
        out->block_y_speed[i] = b->y_speed;
        out->block_y_force[i] = b->y_move_force;
        /* BlockObjectsCore's UpdSte writes only Block_State,x, where x is
         * the original ObjectOffset.  SpawnBrickChunks uses +2 for the
         * lower chunk's parallel fields but never writes Block_State+2;
         * keep that physical state byte independent of the parent object. */
        out->block_state[j] = b->lower_chunk.state;
        out->block_page[j] = b->lower_chunk.page;
        out->block_x[j] = b->lower_chunk.x;
        out->block_y_high[j] = b->lower_chunk.y_high;
        out->block_y[j] = b->lower_chunk.y;
        out->block_x_speed[j] = b->lower_chunk.x_speed;
        out->block_y_speed[j] = b->lower_chunk.y_speed;
        out->block_y_force[j] = b->lower_chunk.y_move_force;
        out->block_orig_y[i] = b->orig_y;
        out->block_bbuf_low[i] = b->bbuf_low;
        out->block_metatile[i] = b->metatile;
        out->block_page2[i] = b->page2;
        out->block_rep_flag[i] = b->restore_pending;
        out->block_orig_x[i] = b->original_x;
    }
    out->block_residual_counter = g_BlockResidualCounter;
    for (i = 0; i < MISC_RAW_SLOT_COUNT; ++i) {
        const MiscHammerState *h = &s_Hammers[i];
        out->misc_state[i] = h->state;
        out->misc_page[i] = h->page;
        out->misc_x[i] = h->x;
        out->misc_y_high[i] = h->y_high;
        out->misc_y[i] = h->y;
        out->misc_x_speed[i] = h->x_speed;
        out->misc_y_speed[i] = h->y_speed;
        out->misc_bbox_ctrl[i] = h->bbox_ctrl;
        out->hammer_source[i] = h->source_slot;
        out->misc_collision[i] = h->collision_flag;
        if (i >= 5 && s_CoinActive[i - 5] && h->state == 0) {
            unsigned c = i - 5;
            out->misc_state[i] = s_CoinState[c];
            out->misc_page[i] = (uint8_t)(s_CoinWorldX[c] >> 8);
            out->misc_x[i] = (uint8_t)s_CoinWorldX[c];
            out->misc_y_high[i] = s_CoinYHigh[c];
            out->misc_y[i] = s_CoinY[c];
            out->misc_y_speed[i] = s_CoinSpeed[c];
        }
    }
}

static uint8_t misc_slot_occupied(uint8_t raw_slot) {
    if (raw_slot >= MISC_RAW_SLOT_COUNT) return 1;
    if (s_Hammers[raw_slot].state != 0) return 1;
    if (raw_slot >= 5 && s_CoinActive[raw_slot - 5] != 0 &&
        s_CoinState[raw_slot - 5] != 0) return 1;
    return 0;
}

void Collision_ResetBlockObjects(void) {
    memset(s_BlockObjects, 0, sizeof(s_BlockObjects));
    memset(s_CoinActive, 0, sizeof(s_CoinActive));
    memset(s_CoinWorldX, 0, sizeof(s_CoinWorldX));
    memset(s_CoinY, 0, sizeof(s_CoinY));
    memset(s_CoinYHigh, 0, sizeof(s_CoinYHigh));
    memset(s_CoinRelX, 0, sizeof(s_CoinRelX));
    memset(s_CoinRelY, 0, sizeof(s_CoinRelY));
    memset(s_CoinOffscreenBits, 0, sizeof(s_CoinOffscreenBits));
    memset(s_CoinSpeed, 0, sizeof(s_CoinSpeed));
    memset(s_CoinState, 0, sizeof(s_CoinState));
    memset(s_MiscYMoveForce, 0, sizeof(s_MiscYMoveForce));
    memset(s_MiscYmfDummy, 0, sizeof(s_MiscYmfDummy));
    memset(s_Hammers, 0, sizeof(s_Hammers));
    SpriteOffsets_SetControl(0);
}

/* InitBlock_XY_Pos plus the caller's Block_Y_Position=$f0 write
 * (game-timer-setup.asm:64-75).  The entrance vine is sourced from the
 * block-object arrays, not from a second coordinate calculation in the
 * enemy owner.  Block_PageLoc2 is the separate $03ea alias consumed by
 * SetupJumpCoin; preserve it even though the entrance path does not read it
 * again before Setup_Vine. */
void Collision_InitEntranceVineBlock(void) {
    BlockObjectState *block = &s_BlockObjects[0];
    uint16_t x_sum = (uint16_t)g_Player_X_Position + 0x08;

    block->page = (uint8_t)(g_Player_PageLoc + (x_sum >> 8));
    block->page2 = block->page;
    block->x = (uint8_t)x_sum & 0xf0;
    block->y_high = g_Player_Y_HighPos;
    block->y = 0xf0;
}

/* SpawnHammerObj (main.asm:3817-3848).  The nine-entry source table and the
 * PRNG selection are preserved as RAM/object logic.  The caller supplies the
 * parallel Enemy_Flag bytes because that bank remains owned by enemy.c. */
uint8_t Collision_TrySpawnHammer(const uint8_t *enemy_flags,
                                 uint8_t enemy_flag_count,
                                 uint8_t object_offset) {
    uint8_t raw_slot = (uint8_t)(g_PseudoRandomBitReg[1] & 0x07);
    uint8_t source_slot;

    if (raw_slot == 0)
        raw_slot = (uint8_t)(g_PseudoRandomBitReg[1] & 0x08);
    if (raw_slot >= MISC_RAW_SLOT_COUNT || misc_slot_occupied(raw_slot))
        return 0;

    source_slot = hammer_enemy_offsets[raw_slot];
    if (enemy_flags == NULL || source_slot >= enemy_flag_count ||
        enemy_flags[source_slot] != 0)
        return 0;

    /* Only these three fields are written by SpawnHammerObj.  Position and
     * fractional bytes remain persistent parallel RAM until ProcHammerObj's
     * countdown reaches SetHPos, exactly as on the 6502. */
    /* SpawnHammerObj first probes HammerEnemyOfsData[y] for an occupied
     * enemy flag, then restores ObjectOffset before writing HammerEnemyOffset
     * ($06ae).  The misc object must therefore follow the current producer,
     * not the temporary probe slot. */
    s_Hammers[raw_slot].source_slot = object_offset;
    s_Hammers[raw_slot].state = 0x90;
    s_Hammers[raw_slot].bbox_ctrl = 0x07;
    /* SpawnHammerObj replaces a free raw Misc_State slot.  If that slot held
     * an expired jumping coin, its physical fields remain but the hammer now
     * owns the slot, so do not let the detached coin view shadow it after the
     * hammer later clears Misc_State (main.asm:3824-3844). */
    if (raw_slot >= 5) {
        /* SpawnHammerObj changes Misc_State ownership but does not write
         * Misc_Y_Speed.  An expired jumping coin may still own that physical
         * byte, so carry its final speed into the shared raw-slot owner
         * before dropping the detached coin record. */
        if (s_CoinActive[raw_slot - 5])
            s_Hammers[raw_slot].y_speed = s_CoinSpeed[raw_slot - 5];
        s_CoinActive[raw_slot - 5] = 0;
    }
    return 1;
}

uint8_t Collision_GetHammerEnemyOffset(uint8_t raw_slot) {
    if (raw_slot >= MISC_RAW_SLOT_COUNT) return 0;
    return s_Hammers[raw_slot].source_slot;
}

void Collision_SetHammerEnemyOffset(uint8_t raw_slot, uint8_t value) {
    if (raw_slot >= MISC_RAW_SLOT_COUNT) return;
    s_Hammers[raw_slot].source_slot = value;
}

static void hammer_set_position(MiscHammerState *hammer,
                                const CollisionHammerSource *source) {
    uint16_t world_x = (uint16_t)(((uint16_t)source->page << 8) |
                                  source->x);

    hammer->state--;
    world_x = (uint16_t)(world_x + 0x02);
    hammer->page = (uint8_t)(world_x >> 8);
    hammer->x = (uint8_t)world_x;
    hammer->y = (uint8_t)(source->y - 0x0a);
    hammer->y_high = 0x01;
}

static void draw_hammer(uint8_t raw_slot, MiscHammerState *hammer) {
    uint8_t pose = 0;
    uint8_t base;

    /* DrawHammer forces pose zero while TimerControl is active or while the
     * throw is still following the source enemy. */
    if (g_TimerControl == 0 && (hammer->state & 0x7f) == 0x01)
        pose = (uint8_t)((g_FrameCounter >> 2) & 0x03);

    base = SpriteOffset_MiscRaw(raw_slot);
    g_SpriteData[base + 0] = (uint8_t)(hammer->rel_y + first_y[pose]);
    g_SpriteData[base + 4] = (uint8_t)(hammer->rel_y + first_y[pose] +
                                       second_y[pose]);
    g_SpriteData[base + 3] = (uint8_t)(hammer->rel_x + first_x[pose]);
    g_SpriteData[base + 7] = (uint8_t)(hammer->rel_x + first_x[pose] +
                                       second_x[pose]);
    g_SpriteData[base + 1] = first_tile[pose];
    g_SpriteData[base + 5] = second_tile[pose];
    g_SpriteData[base + 2] = attributes[pose];
    g_SpriteData[base + 6] = attributes[pose];

    /* DrawHammer tests all but the two low horizontal offscreen bits.  The
     * original DumpTwoSpr writes only the Y bytes before erasing the state. */
    if (hammer->offscreen_bits & 0xfc) {
        hammer->state = 0;
        g_SpriteData[base + 0] = 0xf8;
        g_SpriteData[base + 4] = 0xf8;
    }
}

/* GetMiscBoundBox -> BoundingBoxCore -> CheckRightScreenBBox (main.asm:
 * 10088-10231).  GetMiscBoundBox deliberately changes the object index by
 * nine before BoundingBoxCore, so its screen-side test reads the physical
 * Block_PageLoc/Block_X_Position aliases for raw slots 0..3 and the
 * Misc_PageLoc/Misc_X_Position aliases for raw slots 4..8.  This is the
 * persistent box consumed by PlayerHammerCollision; it is not reconstructed
 * from the hammer's current world position. */
static void get_misc_bbox_alias_position(uint8_t raw_slot,
                                         uint8_t *page, uint8_t *x) {
    if (raw_slot < BLOCK_SLOT_COUNT) {
        *page = s_BlockObjects[raw_slot].page;
        *x = s_BlockObjects[raw_slot].x;
    } else if (raw_slot < BLOCK_SLOT_COUNT * 2) {
        const BlockObjectState *block =
            &s_BlockObjects[raw_slot - BLOCK_SLOT_COUNT];
        *page = block->lower_chunk.page;
        *x = block->lower_chunk.x;
    } else {
        const MiscHammerState *alias = &s_Hammers[raw_slot -
                                                   BLOCK_SLOT_COUNT * 2];
        *page = alias->page;
        *x = alias->x;
    }
}

static void update_hammer_bounding_box(uint8_t raw_slot,
                                       MiscHammerState *hammer) {
    const uint8_t *data;
    uint8_t alias_page;
    uint8_t alias_x;
    uint16_t object_wx;
    uint16_t middle;

    if (hammer->bbox_ctrl >= sizeof(s_PlayerBoundBoxCtrlData) /
                              sizeof(s_PlayerBoundBoxCtrlData[0])) {
        hammer->bbox_ul_x = 0xff;
        hammer->bbox_ul_y = 0xff;
        hammer->bbox_lr_x = 0xff;
        hammer->bbox_lr_y = 0xff;
        return;
    }

    data = s_PlayerBoundBoxCtrlData[hammer->bbox_ctrl];
    hammer->bbox_ul_x = (uint8_t)(hammer->rel_x + data[0]);
    hammer->bbox_lr_x = (uint8_t)(hammer->rel_x + data[2]);
    hammer->bbox_ul_y = (uint8_t)(hammer->rel_y + data[1]);
    hammer->bbox_lr_y = (uint8_t)(hammer->rel_y + data[3]);

    get_misc_bbox_alias_position(raw_slot, &alias_page, &alias_x);
    object_wx = (uint16_t)(((uint16_t)alias_page << 8) | alias_x);
    middle = (uint16_t)(((uint16_t)g_ScreenLeft_PageLoc << 8) |
                        g_ScreenLeft_X_Pos) + 0x80;
    if (object_wx >= middle) {
        if ((hammer->bbox_lr_x & 0x80) == 0) {
            if ((hammer->bbox_ul_x & 0x80) == 0)
                hammer->bbox_ul_x = 0xff;
            hammer->bbox_lr_x = 0xff;
        }
    } else if ((hammer->bbox_ul_x & 0x80) != 0 &&
               hammer->bbox_ul_x >= 0xa0) {
        if ((hammer->bbox_lr_x & 0x80) != 0)
            hammer->bbox_lr_x = 0x00;
        hammer->bbox_ul_x = 0x00;
    }
}

/* MiscObjectsCore's hammer half.  The caller passes a snapshot of the
 * producer-owned Enemy_State/MovingDir/Page/X/Y fields; all Misc_* state,
 * fractional arithmetic, collision latch, relative/offscreen bytes, and OAM
 * remain in this module's nine raw records. */
uint8_t Collision_ProcessHammers(const CollisionHammerSource *sources,
                                 uint8_t source_count) {
    uint8_t clear_sources = 0;
    int raw_slot;

    for (raw_slot = MISC_RAW_SLOT_COUNT - 1; raw_slot >= 0; raw_slot--) {
        MiscHammerState *hammer = &s_Hammers[raw_slot];
        CollisionHammerSource zero_source = {0};
        const CollisionHammerSource *source = &zero_source;
        uint8_t low_state;
        uint8_t old_offscreen;

        if (hammer->state == 0) continue;
        if (sources != NULL && hammer->source_slot < source_count)
            source = &sources[hammer->source_slot];

        low_state = hammer->state & 0x7f;
        old_offscreen = hammer->offscreen_bits;

        if (g_TimerControl == 0) {
            if (low_state == 0x02) {

                hammer->y_speed = 0xfe;
                if (hammer->source_slot < 8) {
                    clear_sources |= (uint8_t)(1u << hammer->source_slot);
                    hammer->x_speed = source->moving_dir == BTN_LEFT
                        ? hammer_x_speed[1] : hammer_x_speed[0];
                }
                hammer_set_position(hammer, source);
            } else if (low_state >= 0x02) {
                hammer_set_position(hammer, source);
            } else {
                SprObjectView object = {
                    &hammer->page, &hammer->x, &hammer->y_high, &hammer->y,
                    &hammer->x_speed, &hammer->x_move_force,
                    &hammer->y_speed,
                    &s_MiscYMoveForce[raw_slot + MISC_PARALLEL_SLOT_OFFSET],
                    &s_MiscYmfDummy[raw_slot + MISC_PARALLEL_SLOT_OFFSET],
                    &hammer->rel_x, &hammer->rel_y,
                    &hammer->offscreen_bits
                };

                SprObject_ImposeGravity(&object, 0x10, 0x0f, 0x04, 0);
                SprObject_MoveHorizontally(&object);

                /* PlayerHammerCollision runs before this frame's relative
                 * position/offscreen update, so it consumes the persistent
                 * prior-frame bounding box saved by the preceding
                 * GetMiscBoundBox pass. */
                if ((g_FrameCounter & 0x01) != 0 && old_offscreen == 0) {
                    if (Enemy_CheckHammerCollision(
                            hammer->bbox_ul_x, hammer->bbox_ul_y,
                            hammer->bbox_lr_x, hammer->bbox_lr_y)) {
                        if (hammer->collision_flag == 0) {
                            hammer->collision_flag = 1;
                            hammer->x_speed = (uint8_t)(0 - hammer->x_speed);
                            if (g_StarInvincibleTimer == 0)
                                Enemy_HammerInjury();
                        }
                    } else {
                        hammer->collision_flag = 0;
                    }
                }
            }
        }

        {
            SprObjectView object = {
                &hammer->page, &hammer->x, &hammer->y_high, &hammer->y,
                &hammer->x_speed, &hammer->x_move_force,
                &hammer->y_speed,
                &s_MiscYMoveForce[raw_slot + MISC_PARALLEL_SLOT_OFFSET],
                &s_MiscYmfDummy[raw_slot + MISC_PARALLEL_SLOT_OFFSET],
                &hammer->rel_x, &hammer->rel_y,
                &hammer->offscreen_bits
            };
            SprScreenEdges edges;

            SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                                     g_ScreenLeft_X_Pos);
            SprObject_GetOffscreenBits(&object, &edges);
            SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
        }
        update_hammer_bounding_box((uint8_t)raw_slot, hammer);
        draw_hammer((uint8_t)raw_slot, hammer);
    }
    return clear_sources;
}

/* Block_PageLoc/$76, Block_X_Position/$8f, and Block_Y_Position/$d7 are
 * parallel 6502 arrays.  The brick-chunk records at +2/+3 are part of those
 * same arrays, so Setup_Vine's Y index can address the two primary block
 * records and the two lower chunk records without a coordinate-derived
 * substitute.  JumpEngine enters Setup_Vine with Y=$60 for VineObject ($2f);
 * the original zero-page indexed reads then alias Fireball_Y_Position+1,
 * unlabelled $ef, and unlabelled $37. */
uint8_t Collision_GetBlockVineSource(uint8_t index, uint8_t *page,
                                     uint8_t *x, uint8_t *y) {
    const BlockObjectState *block;

    if (index < BLOCK_SLOT_COUNT) {
        block = &s_BlockObjects[index];
        *page = block->page;
        *x = block->x;
        *y = block->y;
        return 1;
    }
    if (index < BLOCK_SLOT_COUNT * 2) {
        block = &s_BlockObjects[index - BLOCK_SLOT_COUNT];
        *page = block->lower_chunk.page;
        *x = block->lower_chunk.x;
        *y = block->lower_chunk.y;
        return 1;
    }
    if (index == 0x60) {
        *page = Enemy_GetFireballY(1); /* $76+$60 -> $d6 */
        *x = g_ZeroPageScratchEF;      /* $8f+$60 -> $ef */
        *y = g_ZeroPageScratch37;      /* $d7+$60 -> $37 */
        return 1;
    }
    return 0;
}

static uint8_t metatile_is_solid(uint8_t metatile) {
    return metatile >= SolidMTileUpperExt[(metatile >> 6) & 0x03];
}

static uint8_t metatile_is_climbable(uint8_t metatile) {
    return metatile >= ClimbMTileUpperExt[(metatile >> 6) & 0x03];
}

static uint8_t metatile_is_coin(uint8_t metatile) {
    return metatile == 0xc2 || metatile == 0xc3;
}

static uint8_t metatile_is_hidden(uint8_t metatile) {
    return metatile == 0x5f || metatile == 0x60;
}

static uint8_t metatile_is_jumpspring(uint8_t metatile) {
    return metatile == 0x67 || metatile == 0x68;
}

/* PutPlayerOnVine (main.asm:9402-9408, 9488-9494) indexes the two-byte
 * ClimbXPosAdder/ClimbPLocAdder tables with PlayerFacingDir-1.  The raw
 * facing latch is the controller bitmask, so values 0 and 3 intentionally
 * read the adjacent ROM bytes; retaining those entries matters when both
 * horizontal buttons are held. */

/* ChkForLandJumpSpring (main.asm:9512-9522).  These are persistent gameplay
 * bytes, not a render delay: MovePlayerHorizontally/Vertically and the
 * JumpspringHandler boundary all consult the same RAM state. */
static void start_jumpspring(void) {
    if (g_JumpspringAnimCtrl != 0) return;
    g_VerticalForce = 0x70;
    g_JumpspringForce = 0xf9;
    g_Timers[TIMER_JUMPSPRING] = 0x03;
    g_JumpspringAnimCtrl = 0x01;
}

/* HandleClimbing's flagpole branch (main.asm:9411-9471).  The raw
 * BlockBufferCollision coordinate and metatile are consumed here; the
 * flagpole object itself owns the subsequent slide/score state. */
static uint8_t handle_flagpole_collision(const LevelBlockBufferProbe *probe) {
    uint8_t facing;
    uint8_t adder;
    uint8_t buffer_x;
    uint16_t x_sum;

    if (probe->coordinate_low < 0x06 || probe->coordinate_low >= 0x0a)
        return 0;
    if (probe->metatile != 0x24 && probe->metatile != 0x25)
        return 0;

    facing = g_PlayerFacingDir;
    if (g_GameEngineSubroutine != 0x05) {
        g_PlayerFacingDir = BTN_RIGHT;
        facing = BTN_RIGHT;
        g_ScrollLock++;
        if (g_GameEngineSubroutine != 0x04) {
            Enemy_KillByID(BulletBill_CannonVar);
            g_EventMusicQueue = Silence;
            g_FlagpoleSoundQueue = Sfx_Flagpole;
            g_FlagpoleCollisionYPos = g_Player_Y_Position;
            g_FlagpoleScore = 4;
            while (g_Player_Y_Position < flagpole_y[g_FlagpoleScore] &&
                   g_FlagpoleScore != 0)
                g_FlagpoleScore--;
        }
        g_GameEngineSubroutine = 0x04;
    }

    g_Player_State = PLAYER_STATE_CLIMB;
    g_Player_X_Speed = 0;
    g_Player_X_MoveForce = 0;
    if ((uint8_t)(g_Player_X_Position - g_ScreenLeft_X_Pos) < 0x10)
        facing = BTN_LEFT;
    g_PlayerFacingDir = facing;

    adder = s_ClimbAdderWindow[facing];
    buffer_x = (uint8_t)(probe->buffer_address_low << 4);
    x_sum = (uint16_t)buffer_x + adder;
    g_Player_X_Position = (uint8_t)x_sum;
    if (probe->buffer_address_low == 0) {
        g_Player_PageLoc = (uint8_t)(g_ScreenRight_PageLoc +
                                     s_ClimbAdderWindow[facing + 2]);
    }
    return 1;
}

/* PutPlayerOnVine/HandleClimbing (main.asm:9411-9509). */
static uint8_t handle_climbing(const LevelBlockBufferProbe *probe) {
    uint8_t facing;
    uint8_t adder;
    uint8_t buffer_x;
    uint16_t x_sum;

    if (handle_flagpole_collision(probe)) return 1;
    if (probe->coordinate_low < 0x06 || probe->coordinate_low >= 0x0a)
        return 0;
    if (probe->metatile != 0x26) return 0;

    facing = g_PlayerFacingDir;
    if ((uint8_t)(g_Player_X_Position - g_ScreenLeft_X_Pos) < 0x10)
        facing = BTN_LEFT;
    g_Player_State = PLAYER_STATE_CLIMB;
    g_Player_X_Speed = 0;
    g_Player_X_MoveForce = 0;
    g_PlayerFacingDir = facing;

    /* ClimbXPosAdder = {$f9,$07}, indexed by the NES facing encoding.  The
     * block-buffer producer supplies the page/X object origin used here. */
    adder = s_ClimbAdderWindow[facing];
    buffer_x = (uint8_t)(probe->buffer_address_low << 4);
    x_sum = (uint16_t)buffer_x + adder;
    g_Player_X_Position = (uint8_t)x_sum;
    if (probe->buffer_address_low == 0)
        g_Player_PageLoc = (uint8_t)(g_ScreenRight_PageLoc +
                                     s_ClimbAdderWindow[facing + 2]);
    if (g_Player_Y_Position < 0x20)
        g_GameEngineSubroutine = 0x01; /* Vine_AutoClimb */
    return 1;
}

/* HandlePipeEntry (main.asm:9265, 9537-9585), vertical foot-pair subset.
 * BlockBufferColli_Feet stores the first (left) foot in $01 and the second
 * (right) foot in $00; the packed WarpZoneNumbers/WorldAddrOffsets selection
 * is owned by level.c. */
static void handle_vertical_pipe_entry(uint8_t left_metatile,
                                       uint8_t right_metatile) {
    if ((g_Up_Down_Buttons & BTN_DOWN) == 0) return;
    if (left_metatile != 0x10 || right_metatile != 0x11) return;
    g_ChangeAreaTimer = 0x30;
    g_GameEngineSubroutine = 0x03; /* VerticalPipeEntry */
    /* HandlePipeEntry (main.asm:9547-9554) writes the shared pipe/injury
     * effect to Square1SoundQueue ($00ff) once when entry is accepted. */
    g_Square1SoundQueue = Sfx_PipeDown_Injury;
    g_Player_SprAttrib = 0x20;
    if (g_WarpZoneControl != 0)
        (void)Level_SelectWarpZone(g_WarpZoneControl,
                                   g_Player_X_Position);
}

/* Sideways pipe entry (main.asm:9328-9365). */
static uint8_t handle_side_pipe_entry(uint8_t metatile) {
    if (g_Player_State != PLAYER_STATE_GROUND ||
        g_PlayerFacingDir != BTN_RIGHT ||
        (metatile != 0x6c && metatile != 0x1f))
        return 0;
    /* PipeDwnS (main.asm:9338-9345) uses Player_SprAttrib as the one-shot
     * latch: play the pipe effect only before the priority bit is set. */
    if (g_Player_SprAttrib == 0)
        g_Square1SoundQueue = Sfx_PipeDown_Injury;
    g_Player_SprAttrib |= 0x20;
    if ((g_Player_X_Position & 0x0f) != 0)
        g_ChangeAreaTimer = g_ScreenLeft_PageLoc ? 0x34 : 0xa0;
    /* CheckSideMTiles/ChkGERtn (main.asm:9357-9365) exits for
     * PlayerEntrance ($07); only PlayerCtrlRoutine ($08) is promoted to
     * SideExitPipeEntry.  PlayerEntrance sees the priority bit on the next
     * frame and owns the EnterSidePipe/decrement sequence itself. */
    if (g_GameEngineSubroutine == 0x08)
        g_GameEngineSubroutine = 0x02; /* SideExitPipeEntry */
    return 1;
}

/* HandleAxeMetatile (main.asm:9369-9388).  The mode/task ownership is real;
 * flag/Bowser/level completion remains in the later end-level cluster. */
static void handle_axe_metatile(const LevelBlockBufferProbe *probe) {
    if (probe->metatile != 0xc5) return;
    g_OperMode_Task = 0;
    g_OperMode = VICTORY_MODE; /* original OperMode value $02 */
    g_Player_X_Speed = 0x18;
    Level_QueueCoinRemoval(probe->buffer_column,
                           (uint8_t)(probe->aligned_y >> 4));
}

/* ImpedePlayerMove (main.asm:9587-9624).  The direction byte is the raw
 * scratch $00 value from CheckSideMTiles or ChkFootMTile.  The 6502 tests
 * only whether it is exactly 1: $01 selects the left-side branch, while
 * every other value (including Player_MovingDir=$03) selects the right-side
 * branch.  Preserve that raw branch contract rather than treating the byte
 * as a two-value enum. */
void Collision_ImpededByPlatform(uint8_t direction) {
    uint8_t clear_mask;
    uint8_t delta;
    uint8_t high_adder;

    if (direction == BTN_RIGHT) {
        /* Left-side probe: a left-moving player is already pressing into
         * this side and only clears the bit; otherwise move one pixel left. */
        clear_mask = 0xfe;
        if ((int8_t)g_Player_X_Speed < 0) {
            g_Player_CollisionBits &= clear_mask;
            return;
        }
        delta = 0xff;
        high_adder = 0xff;
    } else {
        /* Right-side probe: a right-moving player is already pressing into
         * this side and only clears the bit; otherwise move one pixel right. */
        clear_mask = 0xfd;
        /* ImpedePlayerMove's `cpy #$01 / bpl ExIPM` treats zero as
         * contact with the right side, so the $01 correction and timer
         * reload still occur when speed is exactly zero. */
        if ((int8_t)g_Player_X_Speed > 0) {
            g_Player_CollisionBits &= clear_mask;
            return;
        }
        delta = 0x01;
        high_adder = 0;
    }

    g_SideCollisionTimer = 0x10;
    g_Player_X_Speed = 0;
    {
        uint16_t sum = (uint16_t)g_Player_X_Position + delta;
        g_Player_X_Position = (uint8_t)sum;
        g_Player_PageLoc = (uint8_t)(g_Player_PageLoc + high_adder +
                                     (uint8_t)(sum >> 8));
    }
    g_Player_CollisionBits &= clear_mask;

}
static void spr_object_gravity(uint8_t* y_high, uint8_t* position,
                               uint8_t* speed, uint8_t* move_force,
                               uint8_t* ymf_dummy, uint8_t downward,
                               uint8_t upward, uint8_t max_speed,
                               uint8_t correct_upward) {
    SprObjectView object = {
        0, 0, y_high, position, 0, 0, speed, move_force, ymf_dummy,
        0, 0, 0
    };
    SprObject_ImposeGravity(&object, downward, upward, max_speed,
                            correct_upward);
}

/* HandleCoinMetatile/ErACM (main.asm:9370-9388): clear the producer's
 * metatile and update the display state; jumping-coin creation belongs only
 * to CheckTopOfBlock in the block-bump path. */
static void collect_coin_probe(const LevelBlockBufferProbe *probe) {
    g_Square2SoundQueue = Sfx_CoinGrab;
    Level_QueueCoinRemoval(probe->buffer_column,
                           (uint8_t)(probe->aligned_y >> 4));
    /* HandleCoinMetatile increments CoinTallyFor1Ups before GiveOneCoin. */
    Score_IncrementAreaCoinTally();
    Score_GiveOneCoin();
}

static void update_block_metatiles(void) {
    int slot;

    /* BlockObjMT_Updater (main.asm:4491-4513) starts at offset 1 and
     * decrements through offset 0.  A non-empty VRAM buffer defers both
     * records, exactly as the original branch does. */
    for (slot = BLOCK_SLOT_COUNT - 1; slot >= 0; slot--) {
        BlockObjectState *block = &s_BlockObjects[slot];
        if (g_VRAM_Buffer1[0] != 0) continue;
        if (!block->restore_pending) continue;
        if (block->metatile == 0)
            Level_QueueBlankMetatile(block->buffer_col, block->buffer_row);
        else
            Level_QueueMetatile(block->buffer_col, block->buffer_row,
                                block->metatile);
        /* ReplaceBlockMetatile (main.asm:772-776) increments the residual
         * byte after WriteBlockMetatile, before the replacement flag is
         * decremented.  The counter has no gameplay consumer in this ROM,
         * but it is still original RAM state and must remain object-update
         * owned rather than being synthesized from framebuffer output. */
        g_BlockResidualCounter++;
        block->restore_pending = 0;
    }
}

static void move_brick_chunks(BlockObjectState *block) {
    SprObjectView top = {
        &block->page, &block->x, &block->y_high, &block->y,
        &block->x_speed, &block->x_move_force, &block->y_speed,
        &block->y_move_force, &block->y_mf_dummy,
        &block->rel_x, &block->rel_y, &block->offscreen_bits
    };
    SprObjectView bottom = {
        &block->lower_chunk.page, &block->lower_chunk.x,
        &block->lower_chunk.y_high, &block->lower_chunk.y,
        &block->lower_chunk.x_speed, &block->lower_chunk.x_move_force,
        &block->lower_chunk.y_speed, &block->lower_chunk.y_move_force,
        &block->lower_chunk.y_mf_dummy,
        &block->lower_chunk.rel_x, &block->lower_chunk.rel_y,
        &block->lower_chunk.offscreen_bits
    };

    /* BlockObjectsCore's chunk branch is two complete SprObject updates.
     * SpawnBrickChunks initializes the Y forces and speeds but deliberately
     * leaves the X fractional bytes as persistent parallel RAM fields. */
    /* ImposeGravityBlock loads $00=$50 before entering the shared
     * ImposeGravity routine; this is not the zero-force path used by
     * callers that enter ImposeGravity directly. */
    SprObject_ImposeGravity(&top, 0x50, 0x00, MaxSpdBlockData[1], 0);
    SprObject_MoveHorizontally(&top);
    SprObject_ImposeGravity(&bottom, 0x50, 0x00, MaxSpdBlockData[1], 0);
    SprObject_MoveHorizontally(&bottom);

    {
        SprScreenEdges edges;
        SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                                 g_ScreenLeft_X_Pos);
        SprObject_GetRelativePosition(&top, g_ScreenLeft_X_Pos);
        SprObject_GetOffscreenBits(&top, &edges);
        SprObject_GetRelativePosition(&bottom, g_ScreenLeft_X_Pos);
        /* GetBlockOffscreenBits is called once for the shared block record;
         * preserve that top-chunk result for DrawBrickChunks masking. */
        block->lower_chunk.offscreen_bits = block->offscreen_bits;
    }
}

static uint8_t brick_chunk_second_x(uint8_t original_rel, uint8_t current_rel) {
    uint8_t difference = (uint8_t)(original_rel - current_rel);
    uint8_t carry = original_rel >= current_rel ? 1 : 0;

    /* DrawBrickChunks performs SBC current, ADC original, ADC #$06. */
    return (uint8_t)(difference + original_rel + carry + 0x06);
}

static void mask_chunk_y(uint16_t sprite_base, uint8_t sprite_number) {
    g_SpriteData[sprite_base + sprite_number * 4] = 0xf8;
}

static void draw_brick_chunks(uint8_t slot, BlockObjectState *block) {
    uint16_t sprite_base = SpriteOffset_Block(slot);
    uint8_t original_rel = (uint8_t)(block->original_x -
                                     g_ScreenLeft_X_Pos);
    uint8_t bottom_right = brick_chunk_second_x(original_rel,
                                                block->lower_chunk.rel_x);
    uint8_t top_right = brick_chunk_second_x(original_rel, block->rel_x);
    uint8_t tile = g_GameEngineSubroutine == 0x05 ? 0x75 : 0x84;
    uint8_t palette = g_GameEngineSubroutine == 0x05 ? 0x02 : 0x03;
    uint8_t attributes = (uint8_t)(((g_FrameCounter << 4) & 0xc0) |
                                   palette);
    uint8_t *o;

    /* DrawBrickChunks owns one four-sprite OAM record: top chunk, its
     * companion, bottom chunk, its companion. */
    o = &g_SpriteData[sprite_base + 0];
    o[0] = block->rel_y;
    o[1] = tile;
    o[2] = attributes;
    o[3] = block->rel_x;
    o = &g_SpriteData[sprite_base + 4];
    o[0] = block->rel_y;
    o[1] = tile;
    o[2] = attributes;
    o[3] = top_right;
    o = &g_SpriteData[sprite_base + 8];
    o[0] = block->lower_chunk.rel_y;
    o[1] = tile;
    o[2] = attributes;
    o[3] = block->lower_chunk.rel_x;
    o = &g_SpriteData[sprite_base + 12];
    o[0] = block->lower_chunk.rel_y;
    o[1] = tile;
    o[2] = attributes;
    o[3] = bottom_right;

    /* DrawBrickChunks calls ChkLeftCo, not DrawBlock's two-column mask:
     * the ROM tests only bit $08 here, so a right-column ($04) edge does
     * not erase the second chunk.  Bit $80 is handled by the explicit
     * top-pair branch below (main.asm:11563-11569). */
    SprObject_MaskBlockOAM(g_SpriteData, sprite_base / 4,
                           (uint8_t)(block->offscreen_bits & 0x08));
    if (block->offscreen_bits & 0x80) {
        mask_chunk_y(sprite_base, 0);
        mask_chunk_y(sprite_base, 1);
    }
    if ((original_rel & 0x80) && block->rel_x >= top_right) {
        mask_chunk_y(sprite_base, 1);
        mask_chunk_y(sprite_base, 3);
    }
}

static void draw_brick_chunk_slot(uint8_t slot, BlockObjectState *block) {
    move_brick_chunks(block);
    draw_brick_chunks(slot, block);

    /* BlockObjectsCore's branch order is material here (main.asm:4460-4484):
     * a zero top Y-high byte goes directly to UpdSte, preserving the saved
     * state and skipping both the lower-chunk clamp and the top-Y test.  Only
     * a nonzero top Y-high byte reaches ChkTop/KillBlock. */
    if (block->y_high != 0) {
        if (block->lower_chunk.y > 0xf0)
            block->lower_chunk.y = 0xf0;
        if (block->y >= 0xf0) {
            block->active = 0;
            block->state = 0;
        }
    }
}

static void draw_block_slot(uint8_t slot) {
    extern uint8_t g_SpriteData[256];
    BlockObjectState *block = &s_BlockObjects[slot];
    uint8_t x;
    uint8_t y;
    uint8_t tiles[4];
    uint8_t attrs[4] = {0x03, 0x03, 0x03, 0x03};
    uint16_t sprite_base;
    int i;

    if (!block->active) return;
    memcpy(tiles, DefaultBlockObjTiles, sizeof(tiles));

    /* BlockObjectsCore dispatches state $11 to BouncingBlockHandler and
     * state $12 to the paired brick-chunk branch. */
    if ((block->state & 0x0f) == 2) {
        draw_brick_chunk_slot(slot, block);
        /* BlockObjectsCore masks the constructor's $12 state before the
         * branch and stores the low nibble at UpdSte (main.asm:4430-4484).
         * KillBlock has already changed it to zero when applicable. */
        block->state &= 0x0f;
        return;
    }
    if ((block->state & 0x0f) != 1) return;
    /* BlockObjectsCore → BouncingBlockHandler → ImposeGravityBlock
     * (main.asm:4429-4474, 4659-4664) uses downward force $50 and
     * MaxSpdBlockData[1]=$08. */
    spr_object_gravity(&block->y_high, &block->y, &block->y_speed,
                       &block->y_move_force, &block->y_mf_dummy,
                       0x50, 0x00, MaxSpdBlockData[1], 0);
    {
        SprObjectView object = {
            &block->page, &block->x, &block->y_high, &block->y,
            0, 0, 0, 0, 0, &block->rel_x, &block->rel_y,
            &block->offscreen_bits
        };
        SprScreenEdges edges;
        SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                                 g_ScreenLeft_X_Pos);
        SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
        SprObject_GetOffscreenBits(&object, &edges);
    }

    /* DrawBlock uses DefaultBlockObjTiles.  Non-ground areas remove the
     * brick-line tile; used blocks use the four original flip combinations. */
    if (g_AreaType != AREA_TYPE_GROUND) {
        tiles[0] = 0x86;
        tiles[1] = 0x86;
    }
    if (block->metatile == 0xc4) {
        tiles[0] = tiles[1] = tiles[2] = tiles[3] = 0x87;
        /* DrawBlock's SetBFlip halves the palette bits outside ground
         * areas (main.asm:11469-11489): AreaType 1 keeps $03, while
         * underground/water/castle used blocks use $01. */
        attrs[0] = (g_AreaType == AREA_TYPE_GROUND) ? 0x03 : 0x01;
        attrs[1] = (uint8_t)(attrs[0] | 0x40);
        attrs[2] = (uint8_t)((attrs[1] | 0x80) & 0x83);
        attrs[3] = (uint8_t)(attrs[1] | 0x80);
    }
    x = block->rel_x;
    y = block->rel_y;
    sprite_base = SpriteOffset_Block(slot);
    for (i = 0; i < 4; i++) {
        uint8_t* o = &g_SpriteData[sprite_base + i * 4];
        o[0] = (uint8_t)(y + (i >= 2 ? 8 : 0));
        o[1] = tiles[i];
        o[2] = attrs[i];
        o[3] = (uint8_t)(x + (i & 1 ? 8 : 0));
    }
    /* SprObjectOffscrChk/MoveESprColOffscreen masks only the affected
     * columns by writing Y=$f8; wrapped X remains object-derived OAM. */
        SprObject_MaskBlockOAM(g_SpriteData, sprite_base / 4,
                               block->offscreen_bits);
    /* BouncingBlockHandler sets Block_RepFlag and clears Block_State once
     * the low Y nibble wraps below five. */
    if ((block->y & 0x0f) < 0x05) {
        block->active = 0;
        block->state = 0;
        block->restore_pending = 1;
    } else {
        /* The stack-held low nibble is the value written by UpdSte. */
        block->state &= 0x0f;
    }
}

static void draw_block_objects(void) {
    /* BlockObjMT_Updater is before BlockObjectsCore in GameEngine. */
    update_block_metatiles();
    /* BlockObjectsCore visits ObjectOffset 1, then 0. */
    for (int slot = BLOCK_SLOT_COUNT - 1; slot >= 0; slot--)
        draw_block_slot((uint8_t)slot);

    /* MiscObjectsCore (main.asm:3973-4030) scans slots $08 down to $05;
     * each slot owns its state, fractional gravity, world position, and OAM
     * offset. */
    for (int slot = MISC_SLOT_COUNT - 1; slot >= 0; slot--) {
        /* ProcJumpCoin leaves the expired record's page/X/Y/speed bytes in
         * physical RAM after writing Misc_State=0.  It is no longer visited
         * by MiscObjectsCore, but those bytes remain the slot's owner until
         * a later constructor reuses it (main.asm:3989-4006). */
        if (!s_CoinActive[slot] || s_CoinState[slot] == 0) continue;
        uint8_t coin_x;
        uint8_t coin_page = (uint8_t)(s_CoinWorldX[slot] >> 8);
        uint8_t coin_pos = (uint8_t)s_CoinWorldX[slot];
        SprObjectView coin_object = {
            &coin_page, &coin_pos, &s_CoinYHigh[slot], &s_CoinY[slot],
            0, 0, 0, 0, 0, &s_CoinRelX[slot], &s_CoinRelY[slot],
            &s_CoinOffscreenBits[slot]
        };
        SprScreenEdges coin_edges;
        SprObject_SetScreenEdges(&coin_edges, g_ScreenLeft_PageLoc,
                                 g_ScreenLeft_X_Pos);
        uint8_t* o = &g_SpriteData[SpriteOffset_Misc((uint8_t)slot)];

        uint8_t promoted = 0;
        if (s_CoinState[slot] < 2) {
            /* JCoinRun is reached in the same MiscObjectsCore pass that
             * observes state $01; there is no frame-wide creation delay. */
            spr_object_gravity(&s_CoinYHigh[slot], &s_CoinY[slot],
                               &s_CoinSpeed[slot],
                               &s_MiscYMoveForce[slot + 5 +
                                                   MISC_PARALLEL_SLOT_OFFSET],
                               &s_MiscYmfDummy[slot + 5 +
                                                   MISC_PARALLEL_SLOT_OFFSET],
                               0x50, 0x03, 0x06, 0);
            /* MiscObjectsCore/JCoinRun (main.asm:3994-4008) promotes the
             * object before RunJCSubs calls JCoinGfxHandler. */
            if (s_CoinSpeed[slot] == 0x05) {
                s_CoinState[slot] = 2;
                promoted = 1;
            }
        }
        if (s_CoinState[slot] >= 2) {
            /* ProcJumpCoin/JCoinGfxHandler (main.asm:3988-4029,
             * 10740-10800): floatey numbers advance their state and rise
             * on even FrameCounter values, then expire at state $30. */
            if (!promoted) {
                s_CoinState[slot]++;
                /* ProcJumpCoin adds ScrollAmount to Misc_X_Position and
                 * propagates its carry into Misc_PageLoc before drawing. */
                s_CoinWorldX[slot] += g_ScrollAmount;
                /* RelativeMiscPosition then derives the screen-relative
                 * X from the updated page/position pair. */
            }
            if (s_CoinState[slot] == 0x30) {
                /* ProcJumpCoin executes only `sta Misc_State,x` here; it does
                 * not clear the parallel Misc_* position/speed fields. */
                s_CoinState[slot] = 0;
                continue;
            }
            coin_page = (uint8_t)(s_CoinWorldX[slot] >> 8);
            coin_pos = (uint8_t)s_CoinWorldX[slot];
            SprObject_GetRelativePosition(&coin_object, g_ScreenLeft_X_Pos);
            SprObject_GetOffscreenBits(&coin_object, &coin_edges);
            coin_x = s_CoinRelX[slot];
            if ((g_FrameCounter & 1) == 0) s_CoinY[slot]--;
            o[0] = s_CoinY[slot];
            o[1] = 0xf7;
            o[2] = 0x02;
            o[3] = coin_x;
            o[4] = s_CoinY[slot];
            o[5] = 0xfb;
            o[6] = 0x02;
            o[7] = (uint8_t)(coin_x + 8);
        } else {
            SprObject_GetRelativePosition(&coin_object, g_ScreenLeft_X_Pos);
            SprObject_GetOffscreenBits(&coin_object, &coin_edges);
            coin_x = s_CoinRelX[slot];
            o[0] = s_CoinY[slot];
            o[1] = JumpingCoinTiles[(g_FrameCounter >> 1) & 0x03];
            o[2] = 0x02;
            o[3] = coin_x;
            o[4] = (uint8_t)(s_CoinY[slot] + 8);
            o[5] = o[1];
            o[6] = 0x82;
            o[7] = coin_x;
        }
    }
}

/* FindEmptyMiscSlot (main.asm:3958-3968).  The ROM checks raw misc slots
 * $08, $07, and $06; raw slot $05 is reserved for the other misc-object
 * producers.  The carry left by CPY #$05 is consumed by the constructor's
 * ADC/SBC immediately after this subroutine returns, so expose it as part of
 * the translated routine contract instead of dropping it at the C boundary.
 */
static uint8_t find_empty_misc_slot(uint8_t carry_in, uint8_t *carry_out) {
    uint8_t raw_slot = 0x08;
    uint8_t carry = (uint8_t)(carry_in & 0x01);

    for (;;) {
        if (!misc_slot_occupied(raw_slot)) {
            *carry_out = carry;
            return raw_slot;
        }
        raw_slot--;
        carry = 1; /* CPY #$05: $07, $06, and the terminal $05 compare. */
        if (raw_slot == 0x05) {
            raw_slot = 0x08; /* no empty slot: the ROM reuses raw slot $08 */
            *carry_out = carry;
            return raw_slot;
        }
    }
}

/* SetupJumpCoin/JCoinC (main.asm:3932-3955), after the caller has selected
 * the raw slot and applied the caller-specific carry arithmetic. */
static void setup_jumping_coin(uint8_t raw_slot, uint16_t world_x, uint8_t y) {
    uint8_t slot;

    if (raw_slot < 5 || raw_slot > 8) return;
    slot = (uint8_t)(raw_slot - 5);
    s_Hammers[raw_slot].state = 0;
    s_CoinActive[slot] = 1;
    s_CoinWorldX[slot] = world_x;
    s_CoinY[slot] = y;
    /* SetupJumpCoin writes Misc_Y_HighPos=$01 for both the CheckTopOfBlock
     * and CoinBlock callers. */
    s_CoinYHigh[slot] = 0x01;
    s_CoinSpeed[slot] = 0xfb;
    g_Square2SoundQueue = Sfx_CoinGrab;
    /* SetupJumpCoin/JCoinC (main.asm:3932-3951) does not touch the
     * persistent SprObject_Y_MoveForce or SprObject_YMF_Dummy bytes.  The
     * next JCoinRun enters ImposeGravity through the +13 parallel-object
     * view, so reused misc slots retain those fractional fields. */
    s_CoinState[slot] = 1;
}

/* BlockBumpedChk (main.asm:4344-4362).  This table is intentionally kept
 * separate from the solid-metatile table: a metatile can be solid without
 * being an item/question block handled by BumpBlock. */
static uint8_t block_bumped_match(uint8_t metatile) {
    unsigned i;
    for (i = 0; i < sizeof(brick_qblock_metatiles); i++)
        if (brick_qblock_metatiles[i] == metatile) return 1;
    return 0;
}

/* BumpBlock's JumpEngine target selection (main.asm:4298-4338) after the
 * BlockBumpedChk table and the >=$09 offset reduction.  Coin/vine branches
 * deliberately return 0xff here: their constructors have separate RAM
 * ownership and remain deferred. */
static uint8_t powerup_type_for_metatile(uint8_t metatile) {
    switch (metatile) {
        case 0xc1: /* MushFlowerBlock */
        case 0x55:
        case 0x5a:
            return 0x00;
        case 0x57: /* StarBlock */
        case 0x5c:
            return 0x02;
        case 0x60: /* ExtraLifeMushBlock */
        case 0x59:
        case 0x5e:
            return 0x03;
        default:
            return 0xff;
    }
}

/* BlockBumpedChk entries 5 and 10 dispatch to VineBlock after the
 * five-entry table fold (main.asm:4314, 4334-4337). */
static uint8_t is_vine_block(uint8_t metatile) {
    return metatile == 0x56 || metatile == 0x5b;
}

static void spawn_brick_chunks(BlockObjectState *block) {
    /* SpawnBrickChunks (main.asm:4402-4425).  The top chunk is the block
     * record itself; only the +2 parallel fields are copied into the lower
     * chunk.  X fractional bytes and the lower Y-high byte are intentionally
     * not cleared here because the ROM routine does not write them. */
    BlockChunkState *lower = &block->lower_chunk;
    block->original_x = block->x;
    block->x_speed = 0xf0;
    lower->x_speed = 0xf0;
    block->y_speed = 0xfa;
    lower->y_speed = 0xfc;
    block->y_move_force = 0;
    lower->y_move_force = 0;
    lower->page = block->page;
    lower->x = block->x;
    lower->y = (uint8_t)(block->y + 0x08);
    /* The ROM redundantly writes the top speed a second time. */
    block->y_speed = 0xfa;
}

/* PlayerBGCollision head path and PlayerHeadCollision (main.asm:9160-9240,
 * 4191-4365).  Object identity comes from the level block-buffer model;
 * no screen coordinate or framebuffer slot is used to select a block. */
uint8_t Collision_CheckHead(void) {
    uint8_t col, row, mt, above;
    uint8_t bumped_match;
    uint8_t powerup_type;
    uint16_t block_origin_wx;
    uint8_t block_y_adder;
    uint8_t probe_index;
    LevelBlockBufferProbe probe;

    /* BlockYPosAdderData = {$04,$12}; PlayerSize==0 is big, while
     * CrouchingFlag or small size selects the second entry. */
    /* ChkCollSize (main.asm:9170-9185) branches on CrouchingFlag before
     * testing size or SwimmingFlag.  The selector therefore remains $02
     * ($0e from BlockBufferAdderData) for crouching/small Mario, including
     * crouching Mario in water.  Only upright big Mario decrements once for
     * swimming ($07) or twice for air/ground ($00).  PlayerHeadCollision's
     * independent BlockYPosAdderData selector is $12 for crouching/small
     * and $04 for upright big. */
    if (g_CrouchingFlag || g_PlayerSize == PLAYER_SIZE_SMALL) {
        block_y_adder = BlockYPosAdderData[1];
        probe_index = BlockBufferAdderData[2];
    } else {
        block_y_adder = BlockYPosAdderData[0];
        probe_index = g_SwimmingFlag ? BlockBufferAdderData[1]
                                     : BlockBufferAdderData[0];
    }
    if (g_Player_Y_Position <
        ((g_PlayerSize == PLAYER_SIZE_BIG && !g_CrouchingFlag) ?
         0x20 : 0x10)) return 0;
    mt = Level_BlockBufferCollision(&probe, g_Player_PageLoc,
                                    g_Player_X_Position,
                                    g_Player_Y_Position, probe_index, 0);
    row = (uint8_t)(probe.aligned_y >> 4);
    col = probe.buffer_column;
    if (row >= 13) return 0;
    block_origin_wx = (uint16_t)(((uint16_t)probe.adjusted_page << 8) |
                                 probe.adjusted_x) & 0xfff0;
    if (mt == 0 || mt == 0x24) return 0;

    /* CheckTopOfBlock: a coin directly above the bumped object is removed
     * and becomes a jumping coin before the block response runs. */
    if (metatile_is_coin(mt)) {
        collect_coin_probe(&probe);
        /* HandleCoinMetatile returns through UpdateNumber to the caller of
         * PlayerBGCollision.  It does not fall through to the feet/side
         * probes below (main.asm:9238-9240). */
        return 1;
    }

    /* CheckForCoinMTiles runs before the vertical-speed gate. */
    if (!(g_Player_Y_Speed & 0x80) || probe.coordinate_low < 0x04)
        return 0;

    if (metatile_is_solid(mt)) {
        /* SolidOrClimb: climbable metatiles suppress the bump sound but
         * still nullify upward speed in this supported boundary. */
        if (!metatile_is_climbable(mt)) {
            g_Square1SoundQueue = Sfx_Bump;
        }
        g_Player_Y_Speed = 0x01;
        return 0;
    }

    if (g_AreaType == AREA_TYPE_WATER || g_BlockBounceTimer != 0) {
        g_Player_Y_Speed = 0x01;
        return 0;
    }
    /* PlayerHeadCollision owns the supported block lifecycle below. */

    bumped_match = block_bumped_match(mt);
    powerup_type = powerup_type_for_metatile(mt);

    /* PlayerHeadCollision leaves the raw $23 marker in the block buffer so
     * EnemyToBGCollisionDet can run KillEnemyAboveBlock on the next object
     * pass before BlockObjMT_Updater installs the replacement metatile. */
    Level_QueueDestroyedBlockMetatile(col, row);
    {
        uint8_t control = SpriteOffsets_GetControl();
        BlockObjectState *block = &s_BlockObjects[control];
        uint16_t bump_y;

        block->active = 1;
        /* PlayerHeadCollision starts at $11 for small Mario and $12 for
         * big Mario.  A matched BrickQBlockMetatile is normalized to the
         * unbreakable $11 path; an unmatched big brick reaches BrickShatter
         * with its replacement metatile set to zero. */
        block->state = (!bumped_match && g_PlayerSize == PLAYER_SIZE_BIG)
            ? 0x12 : 0x11;
        block->metatile = bumped_match ? 0xc4 :
                          (g_PlayerSize == PLAYER_SIZE_BIG ? 0x00 : mt);
        if (bumped_match && (mt == 0x58 || mt == 0x5d)) {
            /* BrickWithCoins/PlayerHeadCollision (main.asm:2970-2993,
             * 4220-4231) leaves the original coin-brick metatile in the
             * block object while BrickCoinTimer is nonzero.  The shared
             * interval timer is loaded only on the first hit after the
             * parser-owned $06BC flag was cleared. */
            if (g_BrickCoinTimerFlag == 0) {
                g_BrickCoinTimer = 0x0b;
                g_BrickCoinTimerFlag = 1;
            }
            if (g_BrickCoinTimer != 0)
                block->metatile = mt;
        }
        block->page = (uint8_t)(block_origin_wx >> 8);
        block->page2 = block->page;
        block->x = (uint8_t)block_origin_wx;
        bump_y = (uint16_t)g_Player_Y_Position + block_y_adder;
        block->y_high = (uint8_t)(g_Player_Y_HighPos + (bump_y >> 8));
        block->y = (uint8_t)bump_y & 0xf0;
        block->x_speed = 0;
        block->y_speed = 0xfe;
        block->y_move_force = 0;
        /* BumpBlock (main.asm:4288-4298) initializes Block_X_Speed,
         * Block_Y_MoveForce, and Block_Y_Speed, but does not touch
         * Block_Orig_XPos ($03f1) or the shared SprObject_YMF_Dummy byte.
         * SpawnBrickChunks owns the former only for the breakable $12 path;
         * preserve both RAM owners across an unbreakable block reuse. */
        block->buffer_col = col;
        block->buffer_row = row;
        block->orig_y = probe.aligned_y;
        block->bbuf_low = probe.buffer_address_low;
        block->restore_pending = 0;

        /* CheckTopOfBlock (main.asm:4380-4400) changes scratch $02 to the
         * block-buffer row immediately above the bumped block, then
         * SetupJumpCoin adds $20 to that scratch byte.  It does not use the
         * block sprite's current Y, which has already been initialized for
         * the bounce. */
        if (row != 0) {
            above = Level_GetBlockMetatile(col, (uint8_t)(row - 1));
            if (above == 0xc2) {
                uint8_t carry_out;
                uint8_t raw_slot;
                uint8_t setup_carry;

                Level_QueueCoinRemoval(col, (uint8_t)(row - 1));
                raw_slot = find_empty_misc_slot(0, &carry_out);
                /* SetupJumpCoin's X calculation is four ASLs of $06,
                 * followed by ORA/STA/LDA, so the carry consumed by its
                 * ADC #$20 is the final ASL carry: bit 4 of the original
                 * block-buffer pointer low byte.  It is independent of
                 * FindEmptyMiscSlot's CPY carry. */
                setup_carry = (uint8_t)((probe.buffer_address_low >> 4) & 1);
                setup_jumping_coin(raw_slot,
                                   ((uint16_t)block->page << 8) |
                                   (uint8_t)(block->x | 0x05),
                                   (uint8_t)(probe.aligned_y - 0x10 +
                                              0x20 + setup_carry));
                Score_GiveOneCoin();
                /* JCoinC increments the area-local hidden tally after the
                 * onscreen coin/score update. */
                Score_IncrementAreaCoinTally();
            }
        }

        if (block->state == 0x12) {
            /* BrickShatter: queue the replacement through BlockObjMT_Updater,
             * launch the two object-owned chunks, and award 50 points. */
            block->restore_pending = 1;
            spawn_brick_chunks(block);
            g_NoiseSoundQueue = Sfx_BrickShatter;
            g_Player_Y_Speed = 0xfe;
            Score_AwardPoints(5, 5);
        } else {
            /* BumpBlock initializes the top chunk and dispatches the
             * supported coin/power-up paths here.  VineBlock's Setup_Vine
             * enemy allocation and the dynamic jumpspring enemy record stay
             * with their later object owners; PlayerBGCollision owns the
             * pipe/axe/jumpspring contact branches below. */
            g_Square1SoundQueue = Sfx_Bump;
            block->y_speed = 0xfe;
            g_Player_Y_Speed = 0;
            if (mt == 0xc0 || mt == 0x5f || mt == 0x58 || mt == 0x5d) {
                uint8_t carry_out;
                uint8_t raw_slot;

                /* CoinBlock's `sbc #$10` (main.asm:3919-3930) runs after
                 * JumpEngine's final pointer-high LDA.  The 6502 carry is
                 * live constructor state: this event's reference trace has
                 * carry clear, so its effective subtraction is $11.
                 * BrickWithCoins
                 * follows this same CoinBlock target while its timer leaves
                 * the source metatile in Block_Metatile. */
                raw_slot = find_empty_misc_slot(0, &carry_out);
                setup_jumping_coin(raw_slot,
                                   ((uint16_t)block->page << 8) |
                                   (uint8_t)(block->x | 0x05),
                                   (uint8_t)(block->y - 0x11 + carry_out));
                Score_GiveOneCoin();
                Score_IncrementAreaCoinTally();
            }
            if (powerup_type != 0xff) {
                /* SetupPowerUp receives Block_PageLoc/X/Y from the selected
                 * persistent block slot, then subtracts eight from Y as the
                 * 6502 constructor does. */
                Enemy_SetupPowerUp(block->page, block->x, block->y,
                                   powerup_type);
            }
            if (is_vine_block(mt)) {
                /* VineBlock passes the same SprDataOffset_Ctrl-selected
                 * block record to Setup_Vine.  The enemy constructor owns
                 * the reserved slot and VineFlagOffset/VineObjOffset state;
                 * no screen coordinate is used as a substitute. */
                Enemy_SetupVineFromBlock(block->page, block->x, block->y);
            }
        }
        SpriteOffsets_SetControl((uint8_t)(control ^ 1));
    }
    g_BlockBounceTimer = 0x10;
    return 0;
}

void Collision_DrawBlockObjects(void) {
    draw_block_objects();
}

/* Check if a tile at world coordinates is solid.
 * world_x: player world X (page << 8 | x_pos)
 * pixel_y: screen Y position
 * Returns 1 if the tile at (world_x/8, pixel_y/8) is solid.
 */
/* ========================================================================
 * GROUND COLLISION
 *
 * Checks the tiles at the player's feet position.
 * If a solid tile is found below the feet, snaps the player to rest
 * on top of it.
 *
 * Player bounding box (small Mario):
 *   Width: 8px (checked at left and right edges)
 *   Height: ~24px (feet at Y_Position + 0x17)
 * Player bounding box (big Mario):
 *   Width: 8px
 *   Height: ~32px (feet at Y_Position + 0x1F)
 * ======================================================================== */

/* ChkCollSize (main.asm:9169-9188) selects one of the three
 * BlockBufferAdderData bases before PlayerBGCollision dispatches head,
 * feet, and side probes.  C callers must retain the same priority: crouch
 * and small-player state use $0e, swimming uses $07, and only an upright
 * non-swimming big player uses $00. */
static uint8_t player_block_buffer_adder_base(void) {
    if (g_CrouchingFlag || g_PlayerSize == PLAYER_SIZE_SMALL)
        return 0x0e;
    if (g_SwimmingFlag)
        return 0x07;
    return 0x00;
}

uint8_t Collision_CheckGround(void) {
    /* Ground collision runs during JUMP and FALL states.
     * Also check during GROUND state to handle walking off edges (Phase 5+).
     */
    if (g_Player_State != PLAYER_STATE_JUMP &&
        g_Player_State != PLAYER_STATE_FALL &&
        g_Player_State != PLAYER_STATE_GROUND) return 0;

    /* Player feet Y position.
     * Small Mario: visible sprites at Y_Position+16 and Y_Position+24,
     *   each 8px tall → feet at Y_Position + 31 (0x1F)
     * Big Mario: visible sprites at Y_Position+0 through Y_Position+24,
     *   each 8px tall → feet at Y_Position + 31 (0x1F)
     * Both sizes have feet at Y_Position + 0x1F.
     */
    uint8_t feet_base = player_block_buffer_adder_base();
    LevelBlockBufferProbe left_probe;
    LevelBlockBufferProbe right_probe;
    uint8_t left_below_collision;
    uint8_t right_below_collision;
    uint8_t landing_metatile;
    Level_BlockBufferCollision(&left_probe, g_Player_PageLoc,
                               g_Player_X_Position, g_Player_Y_Position,
                               (uint8_t)(feet_base + 1), 0);
    Level_BlockBufferCollision(&right_probe, g_Player_PageLoc,
                               g_Player_X_Position, g_Player_Y_Position,
                               (uint8_t)(feet_base + 2), 0);
    if (metatile_is_coin(left_probe.metatile)) {
        collect_coin_probe(&left_probe);
        return 1;
    }
    if (left_probe.metatile == 0 && metatile_is_coin(right_probe.metatile)) {
        collect_coin_probe(&right_probe);
        return 1;
    }
    /* ChkFootMTile does not call CheckForSolidMTiles: any nonzero raw
     * metatile reaches its special-branch filters and can land the player. */
    left_below_collision = left_probe.metatile != 0;
    right_below_collision = right_probe.metatile != 0;
    if (g_Player_State == PLAYER_STATE_GROUND) {
        /* While walking, check if ground disappeared below feet.
         * If neither left nor right has solid ground below, start falling.
         */
        if (!left_below_collision && !right_below_collision) {
            g_Player_State = PLAYER_STATE_FALL;
        }
    } else {
        /* JUMP or FALL: check for landing on solid ground.
         * If the tile below the feet is solid and player is moving down,
         * snap to the top of that tile.
         */
        if (left_below_collision || right_below_collision) {
            /* Only land when moving downward or at rest */
            if ((int8_t)g_Player_Y_Speed >= 0) {
                landing_metatile = left_below_collision ? left_probe.metatile :
                                   right_probe.metatile;
                if (metatile_is_climbable(landing_metatile) ||
                    metatile_is_hidden(landing_metatile)) {
                    /* CheckForClimbMTiles/ChkInvisibleMTiles branch to the
                     * side/special consumer rather than LandPlyr. */
                    return 0;
                }
                if (landing_metatile == 0xc5) {
                    /* ChkFootMTile tests the axe before the invisible and
                     * jumpspring branches.  HandleAxeMetatile then reaches
                     * ErACM, which consumes the $06/$02 address left by the
                     * second BlockBufferColli_Feet call.  Preserve the
                     * selected metatile value in A while using that right
                     * probe's address, as the 6502 does not retain the first
                     * probe's pointer. */
                    LevelBlockBufferProbe axe_probe = right_probe;
                    axe_probe.metatile = landing_metatile;
                    handle_axe_metatile(&axe_probe);
                    return 1;
                }
                /* An active spring takes InitSteP before
                 * ChkForLandJumpSpring, regardless of which eligible raw
                 * foot metatile caused this pass. */
                if (g_JumpspringAnimCtrl != 0) {
                    g_Player_State = PLAYER_STATE_GROUND;
                    return 0;
                }
                if (metatile_is_jumpspring(landing_metatile))
                    /* ChkForLandJumpSpring initializes an idle spring and
                     * then falls through LandPlyr. */
                    start_jumpspring();
                /* LandPlyr (main.asm:9257-9275) tests the low nibble
                 * returned by BlockBufferColli_Feet and then masks the
                 * player's Y position to the metatile boundary.  The C
                 * block-buffer probe has the same Y low nibble as the
                 * object coordinate, so retain that routine boundary here
                 * instead of deriving a separate snap point. */
                if (left_probe.coordinate_low < 0x05 ||
                    right_probe.coordinate_low < 0x05) {
                    g_Player_Y_Position &= 0xf0;
                    g_Player_Y_Speed = 0;
                    g_Player_Y_MoveForce = 0;
                    g_Player_State = PLAYER_STATE_GROUND;
                    Enemy_ResetStompChain();
                    handle_vertical_pipe_entry(left_probe.metatile,
                                               right_probe.metatile);
                } else {
                    Collision_ImpededByPlatform(g_Player_MovingDir);
                    /* The 6502 reaches ImpedePlayerMove through a JMP, so
                     * its RTS returns from PlayerBGCollision itself and
                     * skips the following side-probe loop. */
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* CheckSideMTiles (main.asm:9313-9370).  A nonzero bottom/side result
 * normally exits the whole consumer after its handler.  The top half has
 * two deliberate fall-throughs: sideways-pipe tops and climbable tiles
 * defer to the lower half of that same side pair. */
static int collision_handle_side_contact(const LevelBlockBufferProbe *probe,
                                         uint8_t top_half,
                                         uint8_t side_direction) {
    uint8_t metatile = probe->metatile;

    if (metatile == 0) return 0;
    if (top_half && (metatile == 0x1c || metatile == 0x6b)) return 0;
    if (metatile_is_hidden(metatile)) return 1;
    if (metatile_is_climbable(metatile)) {
        if (top_half) return 0;
        (void)handle_climbing(probe);
        return 1;
    }
    if (metatile_is_coin(metatile)) {
        collect_coin_probe(probe);
        return 1;
    }
    if (metatile_is_jumpspring(metatile)) {
        if (g_JumpspringAnimCtrl == 0)
            Collision_ImpededByPlatform(side_direction);
        return 1;
    }
    if (handle_side_pipe_entry(metatile)) return 1;
    Collision_ImpededByPlatform(side_direction);
    return 1;
}

/* DoPlayerSideCheck/SideCheckLoop (main.asm:9283-9335).  Starting at the
 * selected BlockBufferAdderData base, the ROM visits two vertical probes on
 * the left half and then two on the right half: base+3, +4, +5, +6.  The
 * latter pair uses the $0d X adders and is essential at a page boundary. */
static void collision_check_side(uint8_t feet_base) {
    uint8_t pair;

    for (pair = 0; pair < 2; pair++) {
        uint8_t top_index = (uint8_t)(feet_base + 3 + (pair << 1));
        uint8_t bottom_index = (uint8_t)(top_index + 1);
        /* SideCheckLoop's $00 is 2 for the first pair (X+$02, the
         * player's right side) and 1 for the second pair (X+$0d, the
         * player's left side).  The handler consumes that side identity,
         * not the player's current movement direction. */
        uint8_t side_direction = (pair == 0) ? BTN_LEFT : BTN_RIGHT;
        LevelBlockBufferProbe probe;

        if (g_Player_Y_Position >= 0xe4) return;
        if (g_Player_Y_Position >= 0x20) {
            probe.metatile = Level_BlockBufferCollision(
                &probe, g_Player_PageLoc, g_Player_X_Position,
                g_Player_Y_Position, top_index, 1);
            if (collision_handle_side_contact(&probe, 1, side_direction)) return;
        }

        if (g_Player_Y_Position < 0x08 ||
            g_Player_Y_Position >= 0xd0) return;
        probe.metatile = Level_BlockBufferCollision(
            &probe, g_Player_PageLoc, g_Player_X_Position,
            g_Player_Y_Position, bottom_index, 1);
        if (collision_handle_side_contact(&probe, 0, side_direction)) return;
    }
}

/* PlayerBGCollision (main.asm:9140-9340).  This is the single consumer
 * boundary used by PlayerCtrlRoutine; the older public head/ground helpers
 * remain callable for focused object tests but are no longer separate frame
 * stages. */
void Collision_PlayerBG(void) {
    uint8_t feet_base;

    /* PlayerBGCollision's first gate is the persistent $0716 byte, followed
     * by the routine-number exclusions.  Entrance/vine control owns this
     * byte; it is not equivalent to a framebuffer or current-route test. */
    if (g_DisableCollisionDet != 0 ||
        g_GameEngineSubroutine == 0x0b || g_GameEngineSubroutine < 0x04)
        return;

    if (g_SwimmingFlag) {
        g_Player_State = PLAYER_STATE_JUMP;
    } else if (g_Player_State == PLAYER_STATE_GROUND ||
               g_Player_State == PLAYER_STATE_CLIMB) {
        g_Player_State = PLAYER_STATE_FALL;
    }
    if (g_Player_Y_HighPos != 0x01 || g_Player_Y_Position >= 0xcf) return;

    g_Player_CollisionBits = 0xff;
    feet_base = player_block_buffer_adder_base();
    if (Collision_CheckHead()) return;
    if (Collision_CheckGround()) return;
    collision_check_side(feet_base);
}
