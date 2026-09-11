#include <stdint.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "enemy/enemy-data.h"
#include "collision.h"
#include "level/level.h"
#include "assets.h"

/* Enemy spawn, frenzy, and ProcessEnemyData. */

/* AreaFrenzy/FrenzyIDData (main.asm:2360-2374).  The area-object decoder
 * supplies parameters $08-$0a; the ROM maps them to $14/$17/$18 and scans
 * regular enemy slots 4 through 0 before writing EnemyFrenzyQueue=$06CD.
 * A duplicate active object deliberately clears the queue. */

static uint8_t frenzy_ids[3];
static uint8_t normal_speed[2];
static uint8_t s_FlyCCXPositionData[16];
static uint8_t s_FlyCCXSpeedData[12];
static uint8_t s_FlyCCTimerData[4];
static uint8_t walking_timer[2];
static uint8_t spin_speed[5];
static uint8_t spin_direction[5];
static uint8_t flame_y[4];
static uint8_t flame_ymf_adder[2];
static uint8_t platform_pos_low[3];
static uint8_t platform_pos_high[3];
static uint8_t s_Bitmasks[8];
static uint8_t s_Enemy17YPosData[8];
static uint8_t s_SwimCCIDData[2];
static uint8_t s_FireworksXPosData[6];
static uint8_t s_FireworksYPosData[6];
uint8_t s_PRandomSubtracterRomWindow[16];
uint8_t s_FlyCCBPriorityRomWindow[16];

int Enemy_LoadSpawnTables(void) {
    if (Assets_Copy("tables/frenzy_id.bin", frenzy_ids, sizeof(frenzy_ids)) ||
        Assets_Copy("tables/normal_x_spd.bin", normal_speed,
                    sizeof(normal_speed)) ||
        Assets_Copy("tables/fly_cc_x_position.bin", s_FlyCCXPositionData,
                    sizeof(s_FlyCCXPositionData)) ||
        Assets_Copy("tables/fly_cc_x_speed.bin", s_FlyCCXSpeedData,
                    sizeof(s_FlyCCXSpeedData)) ||
        Assets_Copy("tables/fly_cc_timer.bin", s_FlyCCTimerData,
                    sizeof(s_FlyCCTimerData)) ||
        Assets_Copy("tables/hbro_walking_timer.bin", walking_timer,
                    sizeof(walking_timer)) ||
        Assets_Copy("tables/firebar_spin_spd.bin", spin_speed,
                    sizeof(spin_speed)) ||
        Assets_Copy("tables/firebar_spin_dir.bin", spin_direction,
                    sizeof(spin_direction)) ||
        Assets_Copy("tables/flame_ypos.bin", flame_y, sizeof(flame_y)) ||
        Assets_Copy("tables/flame_ymf_adder.bin", flame_ymf_adder,
                    sizeof(flame_ymf_adder)) ||
        Assets_Copy("tables/plat_pos_low.bin", platform_pos_low,
                    sizeof(platform_pos_low)) ||
        Assets_Copy("tables/plat_pos_high.bin", platform_pos_high,
                    sizeof(platform_pos_high)) ||
        Assets_Copy("tables/bitmasks.bin", s_Bitmasks, sizeof(s_Bitmasks)) ||
        Assets_Copy("tables/enemy17_ypos.bin", s_Enemy17YPosData,
                    sizeof(s_Enemy17YPosData)) ||
        Assets_Copy("tables/swim_cc_id.bin", s_SwimCCIDData,
                    sizeof(s_SwimCCIDData)) ||
        Assets_Copy("tables/fireworks_xpos.bin", s_FireworksXPosData,
                    sizeof(s_FireworksXPosData)) ||
        Assets_Copy("tables/fireworks_ypos.bin", s_FireworksYPosData,
                    sizeof(s_FireworksYPosData)) ||
        Assets_Copy("compat-rom-windows/prandom_subtracter.bin",
                    s_PRandomSubtracterRomWindow,
                    sizeof(s_PRandomSubtracterRomWindow)) ||
        Assets_Copy("compat-rom-windows/fly_cc_priority.bin",
                    s_FlyCCBPriorityRomWindow,
                    sizeof(s_FlyCCBPriorityRomWindow)))
        return -1;
    return 0;
}

void Enemy_AreaFrenzy(uint8_t area_object_parameter) {
    int slot;
    uint8_t id;

    if (area_object_parameter < 0x08 || area_object_parameter > 0x0a)
        return;
    id = frenzy_ids[area_object_parameter - 0x08];
    for (slot = ENEMY_ALLOC_COUNT - 1; slot >= 0; slot--) {
        if (enemies[slot].id == id) {
            id = 0;
            break;
        }
    }
    g_EnemyFrenzyQueue = id;
}

/* CastleObject (main.asm:2507-2531) uses FindEmptyEnemySlot before
 * constructing the StarFlagObject.  FindEmptyEnemySlot scans $00-$04 and
 * returns $05 when all ordinary slots are occupied; preserve that slot
 * ownership rule instead of introducing a second star-flag record. */
void Enemy_SetupStarFlag(uint8_t page, uint8_t x, uint8_t y) {
    uint8_t slot;
    EnemySlot *e;

    for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++)
        if (enemies[slot].flag == 0) break;
    if (slot >= ENEMY_SLOT_COUNT) return;

    e = &enemies[slot];
    e->x = x;
    e->page = page;
    e->y_high = 0x01;
    e->flag = 0x01;
    e->y = y;
    e->id = StarFlagObject;
}

/* Jumpspring (main.asm:2934-2954) uses FindEmptyEnemySlot's ordinary
 * $00-$04 scan and deliberately falls through to slot $05 when all five
 * ordinary slots are occupied.  Enemy_X_Speed is the ROM's
 * Jumpspring_FixedYPos alias at $58, so the fixed Y is kept in the existing
 * slot union rather than in a renderer-local record. */
void Enemy_SetupJumpspring(uint8_t page, uint8_t x, uint8_t y) {
    uint8_t slot;
    EnemySlot *e;

    for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++)
        if (enemies[slot].flag == 0) break;
    if (slot >= ENEMY_SLOT_COUNT) return;

    e = &enemies[slot];
    e->x = x;
    e->page = page;
    e->y = y;
    e->x_speed = y; /* Jumpspring_FixedYPos / Enemy_X_Speed ($58). */
    e->id = JumpspringObject;
    e->y_high = 0x01;
    e->flag = 0x01;
}

/* VerticalPipe (main.asm:2617-2660) constructs the Piranha Plant while
 * rendering the first residual column.  FindEmptyEnemySlot scans only the
 * five ordinary enemy slots and its carry-set/full path skips construction;
 * the reserved slot 5 is not a substitute for this producer.  InitPiranhaPlant
 * then owns the shared $58/$a0 aliases, pipe endpoints, and bbox control. */
void Enemy_SetupPiranhaPlant(uint8_t page, uint8_t x, uint8_t y) {
    uint8_t slot;
    EnemySlot *e;

    for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++)
        if (enemies[slot].flag == 0) break;
    if (slot >= ENEMY_ALLOC_COUNT) return;

    e = &enemies[slot];
    e->x = x;
    e->page = page;
    e->y_high = 0x01;
    e->flag = 0x01;
    e->y = y;
    e->id = PiranhaPlant;
    e->x_speed = 0x01; /* PiranhaPlant_Y_Speed ($58). */
    e->state = 0;
    e->y_speed = 0;
    e->y_mf = y;       /* PiranhaPlantDownYPos ($0434). */
    e->y_dummy = (uint8_t)(y - 0x18); /* PiranhaPlantUpYPos ($0417). */
    e->bbox_ctrl = 0x09;
}

/* Setup_Vine (main.asm:3613-3631) through the entrance path.  The original
 * first builds block slot 0 with InitBlock_XY_Pos, writes $f0 to its Y byte,
 * then copies only page/X/Y into the reserved vine object.  Keep those
 * coordinates in the enemy slot; no renderer-local vine position is used. */
void Enemy_SetupEntranceVine(void) {
    EnemySlot *e = &enemies[POWERUP_SLOT];
    uint8_t vine_index = g_VineFlagOffset;
    uint8_t page;
    uint8_t x;
    uint8_t y;

    if (vine_index >= sizeof(g_VineObjOffset)) return;

    /* Entrance_GameTimerSetup leaves X=0 and runs InitBlock_XY_Pos before
     * Setup_Vine.  Read that block-owned RAM here exactly as the 6502 does;
     * do not duplicate the coordinate derivation in Enemy. */
    Collision_InitEntranceVineBlock();
    if (!Collision_GetBlockVineSource(0, &page, &x, &y)) return;

    e->id = VineObject;
    e->flag = 0x01;
    e->page = page;
    e->x = x;
    e->y = y;
    if (vine_index == 0) g_VineStartY = e->y;
    g_VineObjOffset[vine_index] = POWERUP_SLOT;
    g_VineFlagOffset++;
    /* Square2SoundQueue is the gameplay queue write; the APU consumer is
     * intentionally outside the SDL implementation. */
    g_Square2SoundQueue = Sfx_GrowVine;
}

/* SetupBubble entered with X=$05 after Setup_Vine (game-timer-setup.asm:64-82).
 * The bubble arrays are shorter than the enemy arrays, so the 6502 writes do
 * not create a fourth bubble record: Bubble_X_Position+5=$a1 aliases
 * Enemy_Y_Speed+1, Bubble_PageLoc+5=$88 aliases Enemy_X_Position+1,
 * Bubble_Y_HighPos+5=$d0 aliases Enemy_Y_Position+1, and
 * Bubble_Y_Position+5=$e9 aliases EnemyDataLow.  The caller supplies the
 * post-MoveBubl Y byte; the fractional byte at $0431 is owned by globals.c. */
void Enemy_SetEntranceBubbleAlias(uint8_t x_position, uint8_t page_loc,
                                  uint8_t y_high_position,
                                  uint8_t y_position) {
    enemies[1].y_speed = x_position; /* $a0+1 = Bubble_X_Position+5 */
    enemies[1].x = page_loc;          /* $87+1 = Bubble_PageLoc+5 */
    enemies[1].y = y_high_position;   /* $cf+1 = Bubble_Y_HighPos+5 */
    g_EnemyDataLow = y_position;      /* $e9 = Bubble_Y_Position+5 */
}

void setup_vine_record(EnemySlot *e, uint8_t page, uint8_t x,
                              uint8_t y) {
    uint8_t vine_index = g_VineFlagOffset;

    if (vine_index >= sizeof(g_VineObjOffset))
        return; /* the ROM has only three VineObjOffset bytes */

    e->id = VineObject;
    e->flag = 0x01;
    e->page = page;
    e->x = x;
    e->y = y;
    if (vine_index == 0)
        g_VineStartY = y;
    g_VineObjOffset[vine_index] = enemy_slot_index(e);
    g_VineFlagOffset++;
    g_Square2SoundQueue = Sfx_GrowVine;
}

/* VineBlock -> Setup_Vine (main.asm:4334-4337, 3613-3633).  The bumped
 * block slot is the producer of these coordinates; the constructor copies
 * only page/X/Y into the reserved enemy slot and leaves the slot's other
 * bytes at their existing RAM lifetime values. */
void Enemy_SetupVineFromBlock(uint8_t page, uint8_t x, uint8_t y) {
    EnemySlot *e = &enemies[POWERUP_SLOT];

    setup_vine_record(e, page, x, y);
}

/* Setup_Vine's parser/frenzy entry reads the block parallel arrays with the
 * Y index left by ProcessEnemyData.  Only the modeled $76/$8f/$d7 primary
 * and +2 brick-chunk records are accepted here; higher indices would read
 * adjacent zero-page objects in the ROM and remain an explicit deferred
 * alias rather than becoming guessed coordinates. */
void init_vine_from_parser_source(EnemySlot *e, uint8_t source_index) {
    uint8_t page;
    uint8_t x;
    uint8_t y;

    if (!Collision_GetBlockVineSource(source_index, &page, &x, &y)) {
        return;
    }
    setup_vine_record(e, page, x, y);
}

/* CheckpointEnemyID -> JumpEngine -> Setup_Vine (main.asm:5073-5139,
 * engine/jump-engine.asm:7-20).  For IDs at or above $15 the jump engine
 * receives the ID unchanged, doubles it, and advances past the return
 * address; Setup_Vine therefore reads its parallel source arrays at
 * (Enemy_ID << 1)+2, not at the parser cursor in EnemyDataOffset. */
void init_vine_from_jump_engine(EnemySlot *e) {
    uint8_t jump_index = (uint8_t)((e->id << 1) + 2);

    init_vine_from_parser_source(e, jump_index);
}

/* SetupPowerUp (main.asm:4090-4122) owns the reserved sixth enemy slot.
 * PlayerHeadCollision supplies the block object's persistent page/X/Y state;
 * the constructor writes only the fields listed by the 6502 routine.  In
 * particular, unspecified fractional movement bytes are not blanket-cleared
 * on reuse, matching the slot-lifetime rule used by the original RAM. */
void Enemy_SetupPowerUp(uint8_t page, uint8_t x, uint8_t y,
                        uint8_t requested_type) {
    EnemySlot* e = &enemies[POWERUP_SLOT];

    e->id = PowerUpObject;
    e->page = page;
    e->x = x;
    e->y_high = 0x01;
    e->y = (uint8_t)(y - 0x08);
    e->state = 0x01;
    e->flag = 0x01;
    e->bbox_ctrl = 0x03;

    /* MushFlowerBlock writes zero before SetupPowerUp.  SetupPowerUp then
     * replaces it with PlayerStatus, shifting fiery Mario to flower type;
     * StarBlock and ExtraLifeMushBlock pass their fixed types. */
    if (requested_type < 0x02) {
        s_PowerUpType = g_PlayerStatus;
        if (s_PowerUpType >= 0x02) s_PowerUpType >>= 1;
    } else {
        s_PowerUpType = requested_type;
    }
    e->spr_attrib = 0x20; /* PutBehind: background priority bit. */
    g_Square2SoundQueue = Sfx_GrowPowerUp;
}

/* The CheckpointEnemyID jump table (main.asm:5073-5147). */
static const uint8_t s_EnemyInitDispatch[0x37] = {
    /* 00-0f: InitNormalEnemy/Goomba family and first flying enemies. */
    ENEMY_INIT_NORMAL,          ENEMY_INIT_NORMAL,
    ENEMY_INIT_NORMAL,          ENEMY_INIT_RED_KOOPA,
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_HAMMER,
    ENEMY_INIT_GOOMBA,          ENEMY_INIT_BLOOBER,
    ENEMY_INIT_BULLET_BILL,     ENEMY_INIT_NO_CODE,
    ENEMY_INIT_CHEEP,           ENEMY_INIT_CHEEP,
    ENEMY_INIT_PODOBOO,         ENEMY_INIT_PIRANHA,
    ENEMY_INIT_JUMPING_TROOPA,  ENEMY_INIT_RED_PARATROOPA,
    /* 10-1f: InitHorizFlySwim, Lakitu/frenzy, flame, and firebars. */
    ENEMY_INIT_HORIZ_FLY_SWIM,  ENEMY_INIT_LAKITU,
    ENEMY_INIT_FRENZY,          ENEMY_INIT_NO_CODE,
    ENEMY_INIT_FRENZY,          ENEMY_INIT_FRENZY,
    ENEMY_INIT_FRENZY,          ENEMY_INIT_FRENZY,
    ENEMY_INIT_END_FRENZY,      ENEMY_INIT_NO_CODE,
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_FIREBAR_SHORT,
    ENEMY_INIT_FIREBAR_SHORT,   ENEMY_INIT_FIREBAR_SHORT,
    ENEMY_INIT_FIREBAR_SHORT,   ENEMY_INIT_FIREBAR_LONG,
    /* 20-2f: platforms, Bowser, power-up, and vine constructors. */
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_NO_CODE,
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_NO_CODE,
    ENEMY_INIT_BAL_PLATFORM,    ENEMY_INIT_VERT_PLATFORM,
    ENEMY_INIT_LARGE_LIFT_UP,   ENEMY_INIT_LARGE_LIFT_DOWN,
    ENEMY_INIT_HORI_PLATFORM,   ENEMY_INIT_DROP_PLATFORM,
    ENEMY_INIT_HORI_PLATFORM,   ENEMY_INIT_SMALL_LIFT_UP,
    ENEMY_INIT_SMALL_LIFT_DOWN, ENEMY_INIT_BOWSER,
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_VINE,
    /* 30-36: flag objects, jumpspring, cannon, retainer, end marker. */
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_NO_CODE,
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_NO_CODE,
    ENEMY_INIT_NO_CODE,         ENEMY_INIT_RETAINER,
    ENEMY_INIT_NO_CODE
};

void init_normal_enemy(EnemySlot* e) {

    /* InitNormalEnemy/TallBBox/InitVStf (main.asm:5224-5254). */
    e->x_speed = normal_speed[g_PrimaryHardMode ? 1 : 0];
    e->bbox_ctrl = 0x03;
    e->moving_dir = 0x02;
    e->y_speed = 0;
    e->y_mf = 0;
}

void init_small_bbox(EnemySlot* e) {
    /* SmallBBox/SetBBox/InitVStf (main.asm:5166-5170, 5244-5254). */
    e->bbox_ctrl = 0x09;
    e->moving_dir = 0x02;
    e->y_speed = 0;
    e->y_mf = 0;
}

/* InitFlyingCheepCheep (main.asm:5440-5541).  These tables are indexed by
 * the same random/speed selectors as the ROM; the position table is sixteen
 * bytes because the stationary-player branch can use the low nibble from
 * PseudoRandomBitReg+2. */

/* MoveFlyingCheepCheep (main.asm:7047-7053) consumes the five-byte
 * PRandomSubtracter table and then selects FlyCCBPriority.  The ROM performs
 * both indexed reads without a bounds check.  A Y-force high nibble of $05+
 * therefore reads the instruction bytes immediately following the labeled
 * tables; those bytes are part of the reachable address window, not guessed
 * movement data.  Keep every byte reachable by a four-bit index from the
 * exact $ced5/$ceda ROM windows so the 6502 address arithmetic remains the
 * owner of this residual branch. */

void init_flying_cheep_cheep(EnemySlot* e, uint8_t object_offset) {
    uint8_t random_bits;
    uint8_t position_selector;
    uint8_t speed_base = 0;
    uint8_t speed_selector;
    uint8_t timer_selector;
    uint8_t max_slots;
    uint16_t player_position;
    uint16_t position;

    /* The ROM returns before changing the current object when the shared
     * frenzy timer is active. */
    if (g_FrenzyEnemyTimer != 0)
        return;

    init_small_bbox(e);
    timer_selector = (uint8_t)(g_PseudoRandomBitReg[object_offset + 1] & 0x03);
    g_FrenzyEnemyTimer = s_FlyCCTimerData[timer_selector];

    max_slots = g_SecondaryHardMode ? 4 : 3;
    if (object_offset >= max_slots)
        return;
    /* InitFlyingCheepCheep (main.asm:5468-5475) writes the vertical speed
     * only after the object-offset capacity check. */
    e->y_speed = 0xfb;

    random_bits = (uint8_t)(g_PseudoRandomBitReg[object_offset] & 0x03);
    position_selector = random_bits;
    if (g_Player_X_Speed != 0) {
        speed_base = 0x04;
        if (g_Player_X_Speed >= 0x19)
            speed_base = 0x08;
    }

    position_selector = (uint8_t)(position_selector + speed_base);
    if ((g_PseudoRandomBitReg[object_offset + 1] & 0x03) != 0)
        position_selector = (uint8_t)(
            g_PseudoRandomBitReg[object_offset + 2] & 0x0f);

    /* GSeed's speed selector is base + the original low two random bits;
     * it is distinct from the later stationary-player position selector. */
    speed_selector = (uint8_t)(speed_base + random_bits);
    e->x_speed = s_FlyCCXSpeedData[speed_selector];
    e->moving_dir = BTN_RIGHT;
    if (g_Player_X_Speed == 0) {
        if (position_selector & 0x02) {
            e->x_speed = (uint8_t)(0 - e->x_speed);
            e->moving_dir++;
        }
    } else {
        /* With a moving player, Y still contains speed_selector at D2XPos1. */
        position_selector = speed_selector;
    }

    player_position = (uint16_t)(((uint16_t)g_Player_PageLoc << 8) |
                                 g_Player_X_Position);
    if (position_selector & 0x02)
        position = (uint16_t)(player_position +
                              s_FlyCCXPositionData[position_selector]);
    else
        position = (uint16_t)(player_position -
                              s_FlyCCXPositionData[position_selector]);
    e->page = (uint8_t)(position >> 8);
    e->x = (uint8_t)position;
    e->flag = 0x01;
    e->y_high = 0x01;
    e->y = 0xf8;
}

void init_tall_bbox(EnemySlot* e) {
    /* TallBBox/SetBBox/InitVStf (main.asm:5245-5255). */
    e->bbox_ctrl = 0x03;
    e->moving_dir = 0x02;
    e->y_speed = 0;
    e->y_mf = 0;
}

void init_red_paratroopa(EnemySlot* e) {
    uint8_t center_adder;

    /* InitRedPTroopa aliases RedPTroopaOrigXPos with the generic
     * Enemy_X_MoveForce byte at $0401+x, and RedPTroopaCenterYPos with
     * Enemy_X_Speed at $0058+x.  These writes therefore intentionally replace
     * the previous horizontal fixed-point state before TallBBox/InitVStf. */
    e->x_mf = e->y;
    center_adder = (e->y & 0x80) ? 0xe0 : 0x30;
    e->x_speed = (uint8_t)(e->y + center_adder);
    init_tall_bbox(e);
}

void init_hammer_bro(EnemySlot* e) {

    /* InitHammerBro (main.asm:5208-5216) deliberately does not clear the
     * generic fractional bytes or HammerBroJumpTimer ($003c+x).  Only the
     * throw timer ($03a2+x) and horizontal speed are initialized here; the
     * jump timer retains the prior owner of this RAM byte. */
    e->hammer_throw_timer = 0;
    e->x_speed = 0;
    e->interval_timer = walking_timer[g_SecondaryHardMode & 1];
    set_enemy_interval_timer(e, e->interval_timer);
    e->bbox_ctrl = 0x0b;
    e->moving_dir = 0x02;
    e->y_speed = 0;
    e->y_mf = 0;
}

void init_lakitu(EnemySlot* e) {
    /* SetupLakitu -> InitHorizFlySwimEnemy -> TallBBox2. */
    if (g_EnemyFrenzyBuffer != 0) {
        erase_enemy(e);
        return;
    }
    s_LakituReappearTimer = 0;
    e->x_speed = 0;
    e->bbox_ctrl = 0x03;
    e->moving_dir = 0x02;
    e->y_speed = 0;
    e->y_mf = 0;
}

void init_spiny(EnemySlot* e) {
    /* CreateSpiny's supported object state.  The Lakitu scheduler that
     * chooses the PRDiffAdjustData row is kept in the producer branch below;
     * a stream-created Spiny still enters the same egg/movement state. */
    init_small_bbox(e);
    e->x_speed = 0;
    e->moving_dir = 0x02;
    e->y_speed = 0xfd;
    e->state = 0x05;
    e->flag = 0x01;
}

void init_firebar(EnemySlot* e) {
    uint8_t index = (uint8_t)(e->id - 0x1b);
    uint16_t x;

    /* InitShortFirebar (main.asm:5413-5435) clears only the low byte of
     * FirebarSpinState ($58).  The reused slot's high byte ($a0) is an
     * intentionally persistent angular state and is immediately consumed by
     * ProcFirebar; the generic $0401/$0434 force aliases are not touched by
     * this constructor either. */
    e->x_speed = 0;
    e->firebar_spin_speed = spin_speed[index < 5 ? index : 0];
    e->firebar_spin_direction = spin_direction[index < 5 ? index : 0];
    /* FirebarSpinDirection,x is the zero-page overlay at $0034+x.  The
     * active slot-0 constructor therefore owns the canonical $0034 byte
     * currently named DestinationPageLoc in the C projection. */
    if (enemy_slot_index(e) == 0)
        g_DestinationPageLoc = e->firebar_spin_direction;
    else if (enemy_slot_index(e) == 1)
        g_VictoryWalkControl = e->firebar_spin_direction;
    e->y = (uint8_t)(e->y + 0x04);
    x = (uint16_t)e->x + 0x04;
    e->x = (uint8_t)x;
    e->page = (uint8_t)(e->page + (x >> 8));
    e->bbox_ctrl = 0x03; /* TallBBox2; RunFirebar does not use it. */
}

void init_bowser(EnemySlot* e) {
    /* InitBowser (main.asm:5545-5563); DuplicateEnemyObj is performed by
     * init_enemy_slot before this constructor so the linked rear slot owns
     * the copied page/X/Y bytes. */
    s_Bowser.body_controls = 0;
    s_Bowser.bridge_offset = 0;
    s_Bowser.orig_x = e->x;
    s_Bowser.feet_counter = 0x20;
    s_Bowser.movement_speed = 0x02;
    s_Bowser.hit_points = 0x05;
    s_Bowser.max_range = 0;
    g_BowserFireBreathTimer = 0xdf;
    e->moving_dir = 0xdf;
    /* InitBowser (main.asm:5545-5563) does not set Enemy_BoundBoxCtrl.
     * ProcessBowserHalf owns the $0a write on the following object pass. */
    e->state = 0;
    set_enemy_frame_timer(e, 0x20);
}

void init_bowser_flame(EnemySlot* e) {
    uint8_t random_offset = (uint8_t)(g_PseudoRandomBitReg[
        enemy_slot_index(e)] & 0x03);
    uint8_t front = s_Bowser.front_slot;
    uint8_t flame_timer;

    /* InitBowserFlame (main.asm:5597-5655).  A queued flame is positioned
     * from the persistent Bowser mouth state when available; without a
     * Bowser owner the ROM's right-extent fallback remains the supported
     * constructor branch. */
    /* InitBowserFlame (main.asm:5597-5600) branches straight to FlmEx
     * while FrenzyEnemyTimer is active.  It does not erase the ID/flag that
     * InitEnemyObject or CheckFrenzyBuffer just supplied. */
    if (g_FrenzyEnemyTimer != 0) return;
    /* InitBowserFlame (main.asm:5601-5603) queues the noise producer before
     * selecting the mouth/right-extent constructor branch.  This is an OR
     * into the shared NoiseSoundQueue=$00fd, not a replacement of another
     * queued noise request. */
    g_NoiseSoundQueue |= Sfx_BowserFlame;
    e->y_high = 0x01;
    e->y_dummy = random_offset;
    e->state = 0;
    e->x_mf = 0;
    e->y_mf = 0;
    if (front < ENEMY_ALLOC_COUNT && enemies[front].flag != 0 &&
        enemies[front].id == Bowser) {
        EnemySlot *bowser = &enemies[front];
        uint8_t borrow = bowser->x < 0x0e;
        uint8_t x = (uint8_t)(bowser->x - 0x0e);
        e->page = (uint8_t)(bowser->page - borrow);
        e->x = x;
        e->y = (uint8_t)(bowser->y + 0x08);
        e->y_mf = flame_ymf_adder[flame_y[random_offset] < e->y ? 0 : 1];
        g_EnemyFrenzyBuffer = 0;
    } else {
        /* PutAtRightExtent (main.asm:5623-5631) starts from the stored
         * ScreenRight_X_Pos and adds $20 before writing the flame position.
         * screen_right() returns that stored edge as a 16-bit world value;
         * retaining the add here is essential because this is the deferred
         * no-Bowser-owner constructor branch. */
        uint16_t right = (uint16_t)(screen_right() + 0x20);
        flame_timer = (uint8_t)(next_bowser_flame_timer() + 0x20);
        if (g_SecondaryHardMode) flame_timer = (uint8_t)(flame_timer - 0x10);
        g_FrenzyEnemyTimer = flame_timer;
        e->page = (uint8_t)(right >> 8);
        e->x = (uint8_t)right;
        e->y = flame_y[random_offset];
    }
    /* FinishFlame (main.asm:5668-5680) is reached by both spawn branches. */
    e->bbox_ctrl = 0x08;
    e->y_high = 0x01;
    e->flag = 0x01;
    e->x_mf = 0;
    e->state = 0;
}

/* InitEnemyFrenzy/EndFrenzy (main.asm:5892-5931).  The wrapper is a real
 * CheckpointEnemyID entry: it owns EnemyFrenzyBuffer=$06CB and delegates
 * the supported Lakitu/Spiny and Bowser-flame children without pretending
 * that the other frenzy IDs are ordinary enemies. */
void init_enemy_frenzy(EnemySlot* e, uint8_t object_offset) {
    g_EnemyFrenzyBuffer = e->id;
    switch (e->id) {
    case Spiny:
        frenzy_lakitu_and_spiny(e, object_offset);
        break;
    case BowserFlame:
        /* InitBowserFlame is the ID-$15 child of the frenzy jump table. */
        init_bowser_flame(e);
        break;
    case FlyCheepCheepFrenzy:
        init_flying_cheep_cheep(e, object_offset);
        break;
    case BBill_CCheep_Frenzy:
        bullet_bill_cheep_cheep(e, object_offset);
        break;
    case Fireworks:
        init_fireworks(e);
        break;
    default:
        /* ID $13 reaches NoFrenzyCode in the ROM. */
        break;
    }
}

/* EndFrenzy (main.asm:5914-5931): stop-frenzy is a producer-owned object
 * whose cleanup scans all six enemy slots, including the special slot. */
void end_enemy_frenzy(EnemySlot* e) {
    int slot;

    for (slot = ENEMY_SLOT_COUNT - 1; slot >= 0; slot--) {
        if (enemies[slot].id == Lakitu)
            enemies[slot].state = 0x01;
    }
    g_EnemyFrenzyBuffer = 0;
    e->flag = 0;
}

/* PosPlatform (main.asm:6012-6038).  The three-entry low/high tables are
 * signed 16-bit pixel displacements; page carry is part of the object state. */
void position_platform(EnemySlot* e, uint8_t table_index) {
    uint16_t position = (uint16_t)(((uint16_t)e->page << 8) | e->x);
    uint16_t amount;

    if (table_index >= 3) return;
    amount = (uint16_t)(((uint16_t)platform_pos_high[table_index] << 8) |
                        platform_pos_low[table_index]);
    position = (uint16_t)(position + amount);
    e->page = (uint8_t)(position >> 8);
    e->x = (uint8_t)position;
}

void init_platform_common(EnemySlot* e) {
    /* InitVStf (main.asm:5251-5255) clears only Enemy_Y_Speed and
     * Enemy_Y_MoveForce.  Enemy_YMF_Dummy is a shared object byte and is
     * intentionally left to the area/object-memory owner. */
    e->y_speed = 0;
    e->y_mf = 0;
    e->bbox_ctrl = (g_AreaType == AREA_TYPE_CASTLE ||
                    g_SecondaryHardMode) ? 0x05 : 0x06;
}

void init_balance_platform(EnemySlot* e) {
    uint8_t slot = enemy_slot_index(e);

    e->y = (uint8_t)(e->y - 2);
    if (!g_SecondaryHardMode) position_platform(e, 2);
    e->state = g_BalPlatformAlignment;
    if (g_BalPlatformAlignment & 0x80)
        g_BalPlatformAlignment = slot;
    else
        g_BalPlatformAlignment = 0xff;
    e->moving_dir = 0;
    position_platform(e, 0);
    /* InitBalPlatform falls through InitDropPlatform (main.asm:5968-5971),
     * so the balance platform also initializes its indexed collision flag
     * before entering CommonPlatCode. */
    e->platform_collision_flag = 0xff;
    init_platform_common(e);
}

void init_vertical_platform(EnemySlot* e) {
    uint8_t center_adder;

    if (e->y & 0x80) {
        e->y_platform_top = (uint8_t)(0 - e->y);
        center_adder = 0xc0;
    } else {
        e->y_platform_top = e->y;
        center_adder = 0x40;
    }
    e->y_platform_center = (uint8_t)(e->y + center_adder);
    init_platform_common(e);
}

void init_small_lift(EnemySlot* e, uint8_t upwards) {
    e->y_mf = upwards ? 0x10 : 0xf0;
    e->y_speed = upwards ? 0xff : 0x00;
    position_platform(e, 1);
    e->bbox_ctrl = 0x04;
}

/* LargeLiftUp/LargeLiftDown (main.asm:6008-6059) run PlatLiftUp or
 * PlatLiftDown through CommonSmallLift, then overwrite only the bounding-box
 * control with SPBBox.  They therefore retain the lift force/speed and the
 * +$0c PosPlatform displacement; routing them through InitVStf would erase
 * the motion fields before the first RunLargePlatform pass. */
void init_large_lift(EnemySlot* e, uint8_t upwards) {
    e->y_mf = upwards ? 0x10 : 0xf0;
    e->y_speed = upwards ? 0xff : 0x00;
    position_platform(e, 1);
    e->bbox_ctrl = (g_AreaType == AREA_TYPE_CASTLE ||
                    g_SecondaryHardMode) ? 0x05 : 0x06;
}

void init_platform(EnemySlot* e, EnemyInitKind kind) {
    switch (kind) {
    case ENEMY_INIT_BAL_PLATFORM:
        init_balance_platform(e);
        break;
    case ENEMY_INIT_VERT_PLATFORM:
        init_vertical_platform(e);
        break;
    case ENEMY_INIT_LARGE_LIFT_UP:
        init_large_lift(e, 1);
        break;
    case ENEMY_INIT_LARGE_LIFT_DOWN:
        init_large_lift(e, 0);
        break;
    case ENEMY_INIT_HORI_PLATFORM:
        e->x_move_secondary_counter = 0;
        init_platform_common(e);
        break;
    case ENEMY_INIT_DROP_PLATFORM:
        e->platform_collision_flag = 0xff;
        init_platform_common(e);
        break;
    case ENEMY_INIT_SMALL_LIFT_UP:
        init_small_lift(e, 1);
        break;
    case ENEMY_INIT_SMALL_LIFT_DOWN:
        init_small_lift(e, 0);
        break;
    default:
        break;
    }
}

void init_checkpointed_enemy(EnemySlot* e) {
    uint8_t kind;

    /* CheckpointEnemyID adds eight only to IDs below $15 and sets the
     * masked-offscreen latch before entering the ID jump table. */
    if (e->id < 0x15) {
        e->y = (uint8_t)(e->y + 0x08);
        e->offscreen_masked = 0x01;
    }
    if (e->id >= sizeof(s_EnemyInitDispatch)) return;
    kind = s_EnemyInitDispatch[e->id];
    switch (kind) {
    case ENEMY_INIT_NORMAL:
        init_normal_enemy(e);
        break;
    case ENEMY_INIT_RED_KOOPA:
        init_normal_enemy(e);
        e->state = 0x01; /* InitRedKoopa */
        break;
    case ENEMY_INIT_GOOMBA:
        init_normal_enemy(e);
        init_small_bbox(e); /* InitGoomba -> SmallBBox */
        break;
    case ENEMY_INIT_BLOOBER:
        e->x_speed = 0;
        init_small_bbox(e);
        break;
    case ENEMY_INIT_BULLET_BILL:
        /* InitBulletBill writes only direction and bounding-box control. */
        e->moving_dir = 0x02;
        e->bbox_ctrl = 0x09;
        break;
    case ENEMY_INIT_CHEEP:
        init_small_bbox(e);
        e->x_speed = g_PseudoRandomBitReg[enemy_slot_index(e)] & 0x10;
        /* CheepCheepMoveMFlag=$58 and CheepCheepOrigYPos=$0434 are
         * constructor-owned aliases, not generic speed/force state. */
        e->y_mf = e->y;
        break;
    case ENEMY_INIT_PODOBOO:
        /* InitPodoboo overwrites the parser Y position before SmallBBox. */
        e->y_high = 0x02;
        e->y = 0x02;
        e->interval_timer = 0x01;
        g_Timers[TIMER_ENEMY_INTERVAL_BASE + enemy_slot_index(e)] = 0x01;
        e->state = 0;
        init_small_bbox(e);
        break;
    case ENEMY_INIT_PIRANHA:
        /* InitPiranhaPlant (main.asm:5876-5891) uses the shared $58/$a0
         * bytes as its signed vertical speed and move flag, while $0417 and
         * $0434 retain the two pipe endpoints.  It deliberately does not
         * call InitVStf or overwrite moving direction. */
        e->x_speed = 0x01;
        e->state = 0;
        e->y_speed = 0;
        e->y_mf = e->y;
        e->y_dummy = (uint8_t)(e->y - 0x18);
        e->bbox_ctrl = 0x09;
        break;
    case ENEMY_INIT_JUMPING_TROOPA:
        e->moving_dir = 0x02;
        e->x_speed = 0xf8;
        e->bbox_ctrl = 0x03;
        break;
    case ENEMY_INIT_RED_PARATROOPA:
        init_red_paratroopa(e);
        break;
    case ENEMY_INIT_HORIZ_FLY_SWIM:
        e->x_speed = 0;
        e->bbox_ctrl = 0x03;
        e->moving_dir = 0x02;
        e->y_speed = 0;
        e->y_mf = 0;
        break;
    case ENEMY_INIT_HAMMER:
        init_hammer_bro(e);
        break;
    case ENEMY_INIT_LAKITU:
        init_lakitu(e);
        break;
    case ENEMY_INIT_FRENZY:
        init_enemy_frenzy(e, enemy_slot_index(e));
        break;
    case ENEMY_INIT_END_FRENZY:
        end_enemy_frenzy(e);
        break;
    case ENEMY_INIT_VINE:
        init_vine_from_jump_engine(e);
        break;
    case ENEMY_INIT_FIREBAR_SHORT:
    case ENEMY_INIT_FIREBAR_LONG:
        init_firebar(e);
        break;
    case ENEMY_INIT_BOWSER:
        init_bowser(e);
        break;
    case ENEMY_INIT_BOWSER_FLAME:
        init_bowser_flame(e);
        break;
    case ENEMY_INIT_RETAINER:
        e->y = 0xb8;
        break;
    case ENEMY_INIT_BAL_PLATFORM:
    case ENEMY_INIT_VERT_PLATFORM:
    case ENEMY_INIT_LARGE_LIFT_UP:
    case ENEMY_INIT_LARGE_LIFT_DOWN:
    case ENEMY_INIT_HORI_PLATFORM:
    case ENEMY_INIT_DROP_PLATFORM:
    case ENEMY_INIT_SMALL_LIFT_UP:
    case ENEMY_INIT_SMALL_LIFT_DOWN:
        init_platform(e, (EnemyInitKind)kind);
        break;
    case ENEMY_INIT_NO_CODE:
        /* NoInitCode is a real RTS branch in the ROM. */
        break;
    case ENEMY_INIT_DEFERRED:
        /* The ID is retained in the slot.  RunEnemyObjectsCore and the
         * constructor-specific RAM fields remain deferred to P5. */
        break;
    }
}

void init_enemy_slot(uint8_t slot, uint8_t id, uint8_t page,
                            uint8_t x, uint8_t y, uint8_t reset_state) {
    EnemySlot* e;
    if (slot >= ENEMY_SLOT_COUNT) return;
    e = &enemies[slot];
    e->flag = 0x01;
    e->id = id;
    e->page = page;
    e->x = x;
    e->y_high = 0x01;
    e->y = y;
    e->duplicate_slot = 0xff;
    if (id == 0x1f || id == Bowser) {
        e->duplicate_slot = duplicate_enemy_slot(slot);
        if (id == Bowser) {
            s_Bowser.front_slot = slot;
            s_Bowser.rear_slot = e->duplicate_slot;
        }
    }
    if (reset_state) e->state = 0;
    init_checkpointed_enemy(e);
}

void spawn_group(uint8_t diff) {
    uint8_t count = (uint8_t)(2 + (diff & 1));
    uint8_t id = (diff < 4)
        ? (g_PrimaryHardMode ? BuzzyBeetle : GOOMBA_ID)
        : GreenKoopa;
    uint8_t y = (diff & 2) ? 0x70 : 0xb0;
    uint16_t right = screen_right();
    uint8_t page = (uint8_t)(right >> 8);
    uint8_t x = (uint8_t)right;

    g_NumberofGroupEnemies = count;
    while (g_NumberofGroupEnemies != 0) {
        int slot;
        for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++) {
            if (!enemies[slot].flag) break;
        }
        if (slot >= ENEMY_ALLOC_COUNT) break;
        /* HandleGroupEnemies does not clear state; free slots are already
         * owned by EraseEnemyObject/InitializeArea. */
        init_enemy_slot((uint8_t)slot, id, page, x, y, 0);
        x = (uint8_t)(x + 0x18);
        if (x < 0x18) page++;
        g_NumberofGroupEnemies--;
    }
}

/* CheckFrenzyBuffer (main.asm:5016-5032).  This is intentionally separate
 * from the ordinary parser: the frenzy buffer wins over the vine fallback,
 * and the current empty ObjectOffset is passed through InitEnemyObject.  No
 * flag is invented here; the child constructor owns whether the object is
 * activated, exactly as the 6502 does. */
void check_frenzy_buffer(uint8_t object_offset) {
    uint8_t id = g_EnemyFrenzyBuffer;

    if (id == 0) {
        if (g_VineFlagOffset != 1)
            return;
        id = VineObject;
    }
    if (object_offset >= ENEMY_SLOT_COUNT)
        return;
    enemies[object_offset].id = id;
    enemies[object_offset].state = 0;
    init_checkpointed_enemy(&enemies[object_offset]);
}

/* ParseRow0e (main.asm:5036-5055) consumes an area-transition record even
 * when its X position is behind the current screen boundary.  The caller
 * deliberately owns the three-byte cursor advance and page-select reset so
 * the behind-camera and in-range paths share the exact same state transition. */
int parse_row0e(const uint8_t *stream, uint16_t stream_len,
                       uint16_t cursor) {
    uint8_t world_byte;

    if (cursor + 2 >= stream_len)
        return 0;

    world_byte = stream[cursor + 2];
    if ((world_byte >> 5) == g_WorldNumber) {
        g_AreaPointer = stream[cursor + 1];
        g_EntrancePage = world_byte & 0x1f;
    }
    g_EnemyDataOffset = (uint8_t)(g_EnemyDataOffset + 3);
    g_EnemyObjectPageSel = 0;
    return 1;
}

/* Parse the enemy stream (ProcessEnemyData, main.asm:4895-5060).
 * The parser owns EnemyDataOffset=$0739, EnemyObjectPageLoc=$073A, and
 * EnemyObjectPageSel=$073B.  It consumes control records in its own loop;
 * after a regular object or group it returns to the current ObjectOffset
 * pass exactly as the 6502 does. */
void parse_stream(uint8_t object_offset) {
    int guard = 64;
    uint16_t stream_len = 0;
    const uint8_t *stream =
        EnemyData_GetStream(Level_EnemyDataIndex(), &stream_len);

    /* GetAreaDataAddrs owns the pointer selection.  An invalid table index
     * is an empty parser source, never an implicit 1-1 fallback. */
    if (!stream) {
        check_frenzy_buffer(object_offset);
        return;
    }

    /* ProcessEnemyData keeps the sixth ObjectOffset out of the regular
     * parser path, except for the ROM's explicit $2E check
     * (main.asm:4905-4913). */
    while (guard--) {
        uint16_t cursor = g_EnemyDataOffset;
        uint8_t b1;
        uint8_t row;
        uint8_t b2, ex, epage;
        uint16_t epos, right, ext;

        if (cursor >= stream_len) {
            check_frenzy_buffer(object_offset);
            return;
        }
        b1 = stream[cursor];
        row = b1 & 0x0F;
        if (b1 == 0xFF) {
            check_frenzy_buffer(object_offset);
            return;
        }
        if (cursor + 1 >= stream_len) {
            check_frenzy_buffer(object_offset);
            return;
        }
        b2 = stream[cursor + 1];

        if (object_offset >= ENEMY_ALLOC_COUNT && row != 0x0e &&
            (b2 & 0x3f) != PowerUpObject) return;

        if ((b2 & 0x80) && !g_EnemyObjectPageSel) {
            g_EnemyObjectPageLoc++;
            g_EnemyObjectPageSel = 1;
        }

        if (row == 0x0F && !g_EnemyObjectPageSel) {
            g_EnemyObjectPageLoc = b2 & 0x3F;
            g_EnemyObjectPageSel = 1;
            g_EnemyDataOffset = (uint8_t)(g_EnemyDataOffset + 2);
            continue;
        }

        ex = b1 & 0xF0;
        epage = g_EnemyObjectPageLoc;
        epos = (uint16_t)((epage << 8) | ex);
        enemies[object_offset].page = epage;
        enemies[object_offset].x = ex;
        right = screen_right();
        ext = (right + 0x30) & 0xFFF0;

        if (epos < right) {
            if (row == 0x0e) {
                (void)parse_row0e(stream, stream_len, cursor);
                return;
            }
            g_EnemyDataOffset = (uint8_t)(g_EnemyDataOffset + 2);
            g_EnemyObjectPageSel = 0;
            return;
        }
        if (epos > ext) {
            check_frenzy_buffer(object_offset);
            return;
        }

        /* CheckRightExtBounds (main.asm:4970-4985) publishes the accepted
         * object's vertical state before the row-$0e and hard-mode branches.
         * StrID/InitEnemyObject owns activation; even a skipped object keeps
         * these staged coordinates in its physical slot. */
        enemies[object_offset].y_high = 1;
        enemies[object_offset].y = (uint8_t)(row << 4);

        if (row == 0x0E) {
            if (!parse_row0e(stream, stream_len, cursor)) return;
            return;
        }

        {
            uint8_t id = b2 & 0x3F;
            if ((b2 & 0x40) && !g_SecondaryHardMode)
            {
                /* ProcessEnemyData's hard-mode skip branches directly to
                 * Inc2B (main.asm:4987-4991), before any constructor. */
                g_EnemyDataOffset = (uint8_t)(g_EnemyDataOffset + 2);
                g_EnemyObjectPageSel = 0;
                return;
            }
            if (id >= 0x37 && id < 0x3F) {
                spawn_group(id - 0x37);
                /* HandleGroupEnemies ends at Inc2B (main.asm:5872), after
                 * the group constructors have run. */
                g_EnemyDataOffset = (uint8_t)(g_EnemyDataOffset + 2);
                g_EnemyObjectPageSel = 0;
                return;
            }
            if (id == GOOMBA_ID && g_PrimaryHardMode)
                id = BuzzyBeetle;
            if (id < 0x37) {
                /* StrID calls InitEnemyObject before testing Enemy_Flag,x;
                 * only the still-live constructor path reaches Inc2B
                 * (main.asm:5007-5014).  InitLakitu may enter
                 * EraseEnemyObject, so advancing the parser before this
                 * test loses the ROM's intentional retry of this record. */
                init_enemy_slot(object_offset, id, epage, ex,
                                (uint8_t)(row << 4), 1);
                if (enemies[object_offset].flag == 0)
                    return;
            }
            g_EnemyDataOffset = (uint8_t)(g_EnemyDataOffset + 2);
            g_EnemyObjectPageSel = 0;
            return;
        }
    }

}

/* BulletBillCheepCheep (main.asm:5732-5810).  The water branch uses the
 * shared BitMFilter=$06dd mask so the eight vertical spawn rows are consumed
 * without duplicating a per-slot selector.  The non-water branch reuses the
 * normal frenzy Bullet Bill object ID and sound-queue owner. */

/* Set17ID through CheckpointEnemyID (main.asm:5773-5809).  Both the water
 * Cheep-Cheep branch and the non-water Bullet-Bill branch consume this common
 * constructor tail; PutAtRightExtent and YMF_Dummy are not water-only. */
void set_17_id_and_spawn(EnemySlot* e, uint8_t object_offset) {
    uint8_t row;

    if (g_BitMFilter == 0xff)
        g_BitMFilter = 0;
    row = (uint8_t)(g_PseudoRandomBitReg[object_offset] & 0x07);
    while ((s_Bitmasks[row] & g_BitMFilter) != 0)
        row = (uint8_t)((row + 1) & 0x07);
    g_BitMFilter |= s_Bitmasks[row];

    put_enemy_at_right_extent(e, s_Enemy17YPosData[row]);
    e->y_dummy = 0;
    g_FrenzyEnemyTimer = 0x20;
    init_checkpointed_enemy(e);
}

void bullet_bill_cheep_cheep(EnemySlot* e, uint8_t object_offset) {
    uint8_t selector;
    int slot;

    if (g_FrenzyEnemyTimer != 0)
        return;

    if (g_AreaType != AREA_TYPE_WATER) {
        /* DoBulletBills scans the ordinary five slots before creating a new
         * frenzy Bullet Bill.  The current queued slot retains its original
         * object state if a Bullet Bill is already present. */
        for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++) {
            if (enemies[slot].flag != 0 &&
                enemies[slot].id == BulletBill_FrenzyVar)
                return;
        }
        g_Square2SoundQueue |= Sfx_Blast;
        e->id = BulletBill_FrenzyVar;
        set_17_id_and_spawn(e, object_offset);
        return;
    }

    if (object_offset >= 3)
        return;

    /* The two-bit selector is the OR of the PRNG threshold and the
     * world-number branch, then reduced to the SwimCC_IDData index. */
    selector = 0;
    if (g_PseudoRandomBitReg[object_offset] >= 0xaa)
        selector++;
    if (g_WorldNumber != WORLD_2)
        selector++;
    selector &= 0x01;
    e->id = s_SwimCCIDData[selector];
    set_17_id_and_spawn(e, object_offset);
}

/* FireworksXPosData/FireworksYPosData and InitFireworks
 * (main.asm:5673-5718).  ExplosionGfxCounter=$58 aliases the ordinary
 * per-object X-speed byte, while ExplosionTimerCounter=$a0 aliases the
 * ordinary per-object Y-speed byte.  The constructor does not touch the
 * neighboring Enemy_X_MoveForce/$0401+x byte. */

void init_fireworks(EnemySlot* e) {
    int star_slot;
    EnemySlot *star = 0;
    uint16_t star_x;
    uint8_t table_index;
    uint16_t position;

    if (g_FrenzyEnemyTimer != 0) return;
    g_FrenzyEnemyTimer = 0x20;
    g_FireworksCounter--;

    /* StarFChk scans object offsets 5 through 0.  The ROM assumes that a
     * StarFlagObject exists; returning without activating the current slot
     * is the safe equivalent of the impossible fall-through when the
     * producer/consumer contract is violated. */
    for (star_slot = ENEMY_SLOT_COUNT - 1; star_slot >= 0; star_slot--) {
        if (enemies[star_slot].id == StarFlagObject) {
            star = &enemies[star_slot];
            break;
        }
    }
    if (star == 0) return;

    star_x = (uint16_t)(((uint16_t)star->page << 8) | star->x);
    position = (uint16_t)(star_x - 0x30);
    table_index = (uint8_t)(g_FireworksCounter + star->state);
    if (table_index >= sizeof(s_FireworksXPosData)) return;

    position = (uint16_t)(position + s_FireworksXPosData[table_index]);
    e->x = (uint8_t)position;
    e->page = (uint8_t)(position >> 8);
    e->y = s_FireworksYPosData[table_index];
    e->y_high = 0x01;
    e->flag = 0x01;
    e->x_speed = 0; /* ExplosionGfxCounter=$58+x */
    e->y_speed = 0x08; /* ExplosionTimerCounter=$a0+x */
}

/* LakituAndSpinyHandler/CreateSpiny (main.asm:5300-5396).  This routine is
 * entered by InitEnemyFrenzy for the current ObjectOffset.  A Lakitu is
 * allocated in a genuinely free enemy slot; when one already exists, the
 * current slot becomes the Spiny egg using the Lakitu's persistent page/X/Y
 * fields.  It is never spawned directly from MoveLakitu. */
void frenzy_lakitu_and_spiny(EnemySlot* current,
                                    uint8_t current_offset) {
    int lakitu_slot = -1;
    int slot;

    if (g_FrenzyEnemyTimer != 0 || current_offset >= ENEMY_ALLOC_COUNT)
        return;
    g_FrenzyEnemyTimer = 0x80;

    for (slot = ENEMY_ALLOC_COUNT - 1; slot >= 0; slot--) {
        if (enemies[slot].id == Lakitu) {
            lakitu_slot = slot;
            break;
        }
    }
    if (lakitu_slot >= 0) {
        EnemySlot* lakitu = &enemies[lakitu_slot];

        if (g_Player_Y_Position < 0x2c || lakitu->state != 0)
            return;
        current->page = lakitu->page;
        current->x = lakitu->x;
        current->y_high = 0x01;
        current->y = (uint8_t)(lakitu->y - 0x08);
        current->id = Spiny;
        /* CreateSpiny fills zero-page $01-$03 from PRDiffAdjustData before
         * calling PlayerLakituDiff.  The table is local scratch, not an
         * additional per-object trajectory state. */
        {
            uint8_t random = (uint8_t)(
                g_PseudoRandomBitReg[current_offset] & 0x03);
            uint8_t scratch[3] = {
                s_PRDiffAdjustData[0][random],
                s_PRDiffAdjustData[1][random],
                s_PRDiffAdjustData[2][random]
            };
            (void)player_lakitu_diff_with_adjust(current, scratch);
        }
        /* SmallBBox/SpinyRte then leaves X speed at zero and selects right. */
        init_spiny(current);
        current->moving_dir = BTN_RIGHT;
        return;
    }

    s_LakituReappearTimer++;
    if (s_LakituReappearTimer < 0x07)
        return;
    for (slot = ENEMY_ALLOC_COUNT - 1; slot >= 0; slot--) {
        EnemySlot* lakitu;
        if (enemies[slot].flag != 0)
            continue;
        lakitu = &enemies[slot];
        lakitu->state = 0;
        lakitu->id = Lakitu;
        /* CreateL calls SetupLakitu, then PutAtRightExtent; the latter owns
         * the active flag/high-Y/bbox writes shared with the flame setup. */
        lakitu->x_speed = 0;
        lakitu->bbox_ctrl = 0x03;
        lakitu->moving_dir = BTN_LEFT;
        lakitu->y_speed = 0;
        lakitu->y_mf = 0;
        put_enemy_at_right_extent(lakitu, 0x20);
        s_LakituReappearTimer = 0;
        return;
    }
}
