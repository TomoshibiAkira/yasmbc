/* engine/collision.h - Collision detection interface */

#ifndef SMB_COLLISION_H
#define SMB_COLLISION_H

#include "constants/types.h"

int Collision_LoadRom(void);

/* Check ground collision (feet) and snap player to ground */
uint8_t Collision_CheckGround(void);
uint8_t Collision_CheckHead(void);
void Collision_PlayerBG(void);
/* BoundingBoxCore for the player (main.asm:10155-10191).  The four bytes
 * remain persistent RAM consumed by PlayerCollisionCore during the later
 * enemy/platform passes. */
void Collision_UpdatePlayerBoundingBox(void);
void Collision_ResetBlockObjects(void);
void Collision_DrawBlockObjects(void);
/* Entrance_GameTimerSetup's InitBlock_XY_Pos/Block_Y_Position producer. */
void Collision_InitEntranceVineBlock(void);

typedef struct CollisionVerifierState {
    uint8_t block_state[4], block_page[4], block_x[4];
    uint8_t block_y_high[4], block_y[4], block_x_speed[4];
    uint8_t block_y_speed[4], block_y_force[4];
    /* PlayerHeadCollision/BlockObjMT_Updater metadata.  These are the two
     * SprDataOffset_Ctrl-selected records at $03e4-$03f2, not a copy of the
     * block buffer or a renderer-only coordinate. */
    uint8_t block_orig_y[2], block_bbuf_low[2];
    uint8_t block_metatile[2], block_page2[2], block_rep_flag[2];
    uint8_t block_orig_x[2], block_residual_counter;
    uint8_t misc_state[9], misc_page[9], misc_x[9];
    uint8_t misc_y_high[9], misc_y[9], misc_x_speed[9], misc_y_speed[9];
    uint8_t misc_bbox_ctrl[9], hammer_source[9], misc_collision[9];
} CollisionVerifierState;
void Collision_GetVerifierState(CollisionVerifierState *state);

/* Misc_State is a nine-entry raw RAM bank ($2a-$32).  Hammer objects use
 * the same slots as jumping coins; the owner below keeps the slot state and
 * object arithmetic in one place while Enemy supplies the spawning enemy's
 * live fields. */
typedef struct CollisionHammerSource {
    uint8_t state;
    uint8_t moving_dir;
    uint8_t page;
    uint8_t x;
    uint8_t y;
} CollisionHammerSource;

/* SpawnHammerObj (main.asm:3817-3848).  The source flags are the parallel
 * Enemy_Flag bytes, including the ROM's seventh/overlaid byte.  The current
 * ObjectOffset is stored separately in HammerEnemyOffset; it is not the
 * table slot probed for occupancy. */
uint8_t Collision_TrySpawnHammer(const uint8_t *enemy_flags,
                                 uint8_t enemy_flag_count,
                                 uint8_t object_offset);

/* The first byte of HammerEnemyOffset ($06ae) is also the one-byte overrun
 * immediately following MetatileBuffer ($06a1-$06ad).  RenderUnderPart can
 * legitimately address that physical alias when a row-$0b ledge reaches
 * buffer index $0d; expose the RAM owner without constructing a hammer. */
uint8_t Collision_GetHammerEnemyOffset(uint8_t raw_slot);
void Collision_SetHammerEnemyOffset(uint8_t raw_slot, uint8_t value);

/* MiscObjectsCore/ProcHammerObj/DrawHammer (main.asm:3856-3909,
 * 3973-3985, 10511-10562).  Returns a bit mask for source enemy state d3
 * clears requested by SetHSpd. */
uint8_t Collision_ProcessHammers(const CollisionHammerSource *sources,
                                 uint8_t source_count);
/* Setup_Vine reads the indexed Block_PageLoc/Block_X_Position/
 * Block_Y_Position arrays.  Expose that RAM-owned view to the parser-side
 * enemy constructor without making the enemy code own a second copy. */
uint8_t Collision_GetBlockVineSource(uint8_t index, uint8_t *page,
                                     uint8_t *x, uint8_t *y);
void Collision_ImpededByPlatform(uint8_t direction);

#endif /* SMB_COLLISION_H */
