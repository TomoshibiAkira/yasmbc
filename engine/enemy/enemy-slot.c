#include <stdint.h>
#include <string.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "collision.h"
#include "assets.h"

/* enemies[6] is the owner for the six SprObject enemy banks. Encoder
 * projection goes through Enemy_GetVerifierState; do not add parallel
 * g_Enemy_*[6] for fields that already live on the slot. Overlay bytes
 * ($00f0-$00ff floatey, Buffer2 vs Bowser, timer macros) stay aliased. */
EnemySlot enemies[ENEMY_SLOT_COUNT];
/* Enemy_OffscreenBits is the absolute shared RAM byte $03d1 written by
 * GetEnemyOffscreenBits/SetOffscrBitsOffset for the current enemy pass
 * (main.asm:12234-12264), not a per-slot array element. */
uint8_t s_Enemy_OffscreenBits;
FireballSlot fireballs[FIREBALL_SLOT_COUNT];
uint8_t s_PowerUpType;
uint8_t s_LakituReappearTimer;
BowserState s_Bowser;

/* FlameTimerData/PRandomRange (main.asm:7279, 7478-7487).  The former is
 * indexed by BowserFlameTimerCtrl ($0367), while BowserFireBreathTimer is the
 * separate interval timer at $0790. */
uint8_t s_BowserFlameTimerData[8];
uint8_t s_BowserPRandomRange[4];

int Enemy_LoadSlotTables(void) {
    if (Assets_Copy("tables/bowser_flame_timer.bin", s_BowserFlameTimerData,
                    sizeof(s_BowserFlameTimerData)) ||
        Assets_Copy("tables/bowser_prandom_range.bin", s_BowserPRandomRange,
                    sizeof(s_BowserPRandomRange)))
        return -1;
    return 0;
}


uint8_t next_bowser_flame_timer(void) {
    uint8_t index = (uint8_t)(s_Bowser.flame_timer_ctrl & 0x07);
    s_Bowser.flame_timer_ctrl = (uint8_t)((index + 1) & 0x07);
    return s_BowserFlameTimerData[index];
}

uint8_t s_StompChainCounter = 0;

void Enemy_SetTitleBufferAliases(const uint8_t *source) {
    unsigned i;

    if (source == NULL) return;
    for (i = 0; i < ENEMY_SLOT_COUNT; ++i) {
        enemies[i].x_mf = source[0x101 + i];
        enemies[i].y_dummy = source[0x117 + i];
        enemies[i].y_mf = source[0x134 + i];
    }
}

void Enemy_ClearTitleBufferAliases(void) {
    unsigned i;

    for (i = 0; i < ENEMY_SLOT_COUNT; ++i) {
        enemies[i].x_mf = 0;
        enemies[i].y_dummy = 0;
        enemies[i].y_mf = 0;
    }
}

void Enemy_GetVerifierState(EnemyVerifierState *out) {
    unsigned i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < ENEMY_SLOT_COUNT; ++i) {
        const EnemySlot *e = &enemies[i];
        out->flag[i] = e->flag; out->id[i] = e->id;
        out->state[i] = e->state; out->moving_dir[i] = e->moving_dir;
        out->page[i] = e->page; out->x[i] = e->x;
        out->y_high[i] = e->y_high; out->y[i] = e->y;
        out->x_speed[i] = e->x_speed; out->y_speed[i] = e->y_speed;
        out->x_mf[i] = e->x_mf; out->y_mf[i] = e->y_mf;
        out->y_dummy[i] = e->y_dummy;
        out->collision_bits[i] = e->collision_bits;
        out->bbox_ctrl[i] = e->bbox_ctrl; out->spr_attrib[i] = e->spr_attrib;
        out->offscreen_masked[i] = e->offscreen_masked;
        out->firebar_spin_speed[i] = e->firebar_spin_speed;
        out->firebar_spin_direction[i] = e->firebar_spin_direction;
        out->hammer_throw_timer[i] = e->hammer_throw_timer;
        out->shell_chain[i] = e->shell_chain_counter;
        out->hammer_jump_timer[i] = e->hammer_jump_timer;
        out->floaty_control[i] = e->floaty_control;
        out->floaty_timer[i] = e->floaty_timer;
        out->floaty_x[i] = e->floaty_x; out->floaty_y[i] = e->floaty_y;
        out->bbox[i][0] = e->bbox_ul_x; out->bbox[i][1] = e->bbox_ul_y;
        out->bbox[i][2] = e->bbox_lr_x; out->bbox[i][3] = e->bbox_lr_y;
    }
    for (i = 0; i < FIREBALL_SLOT_COUNT; ++i) {
        const FireballSlot *f = &fireballs[i];
        out->fireball_state[i] = f->state; out->fireball_page[i] = f->page;
        out->fireball_x[i] = f->x; out->fireball_y_high[i] = f->y_high;
        out->fireball_y[i] = f->y; out->fireball_x_speed[i] = f->x_speed;
        out->fireball_y_speed[i] = f->y_speed;
        out->fireball_bbox_ctrl[i] = f->bbox_ctrl;
        out->fireball_bouncing[i] = f->bouncing;
    }
    out->current_rel_x = g_Enemy_Rel_XPos;
    out->current_offscreen_bits = s_Enemy_OffscreenBits;
    out->power_up_type = s_PowerUpType;
    out->lakitu_reappear_timer = s_LakituReappearTimer;
    out->stomp_chain_counter = s_StompChainCounter;
    out->bowser[0] = s_Bowser.body_controls; out->bowser[1] = s_Bowser.feet_counter;
    out->bowser[2] = s_Bowser.movement_speed; out->bowser[3] = s_Bowser.orig_x;
    out->bowser[4] = s_Bowser.flame_timer_ctrl; out->bowser[5] = s_Bowser.front_slot;
    out->bowser[6] = s_Bowser.bridge_offset; out->bowser[7] = s_Bowser.gfx_flag;
    out->bowser[8] = s_Bowser.hit_points; out->bowser[9] = s_Bowser.max_range;
}

uint8_t enemy_slot_index(const EnemySlot* e) {
    return (uint8_t)(e - enemies);
}

/* FirebarSpinDirection,x overlays zero-page bytes $0034-$0038.  The first
 * two bytes have named C owners because the same addresses are used by the
 * victory walk; use those physical aliases whenever the corresponding
 * firebar object is active.  The remaining unlabelled bytes stay in the
 * slot-local representation until their other ASM writers are translated. */
uint8_t firebar_direction_value(const EnemySlot* e) {
    switch (enemy_slot_index(e)) {
    case 0:
        return g_DestinationPageLoc;  /* $0034 */
    case 1:
        return g_VictoryWalkControl; /* $0035 */
    default:
        return e->firebar_spin_direction;
    }
}

/* SpawnHammerObj (main.asm:3817-3848) receives the current ObjectOffset from
 * the producer, while its HammerEnemyOfsData table is only an occupancy probe.
 * Keep the seven-byte Enemy_Flag view in the enemy owner and let collision.c
 * retain the resulting Misc_State/$06ae record. */
uint8_t enemy_try_spawn_hammer(uint8_t object_offset) {
    uint8_t enemy_flags[7] = {0};
    uint8_t i;

    for (i = 0; i < ENEMY_SLOT_COUNT; i++)
        enemy_flags[i] = enemies[i].flag;
    return Collision_TrySpawnHammer(enemy_flags, 7, object_offset);
}

/* DuplicateEnemyObj (main.asm:5567-5587).  The high flag stores the owner
 * slot, so the duplicate is never independently dispatched by the six
 * ObjectOffset passes; its owner explicitly visits it when the ASM routine
 * requires a second object.  The ROM copies only the page/X and vertical
 * position fields: ID, state, direction, and the overlaid floaty record are
 * intentionally left as the destination slot's existing RAM. */
uint8_t duplicate_enemy_slot(uint8_t owner_slot) {
    uint8_t slot;
    EnemySlot *owner;

    if (owner_slot >= ENEMY_ALLOC_COUNT) return 0xff;
    owner = &enemies[owner_slot];
    for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++) {
        if (slot == owner_slot || enemies[slot].flag != 0) continue;
        enemies[slot].flag = (uint8_t)(0x80 | owner_slot);
        enemies[slot].page = owner->page;
        enemies[slot].x = owner->x;
        enemies[slot].y_high = 0x01;
        enemies[slot].y = owner->y;
        return slot;
    }
    return 0xff;
}

uint8_t linked_duplicate_slot(uint8_t owner_slot) {
    uint8_t slot;

    for (slot = 0; slot < ENEMY_ALLOC_COUNT; slot++) {
        if ((enemies[slot].flag & 0xf0) == 0x80 &&
            (enemies[slot].flag & 0x0f) == owner_slot)
            return slot;
    }
    return 0xff;
}

void set_enemy_interval_timer(EnemySlot* e, uint8_t value) {
    uint8_t slot = enemy_slot_index(e);

    e->interval_timer = value;
    g_Timers[TIMER_ENEMY_INTERVAL_BASE + slot] = value;
}

void set_enemy_frame_timer(EnemySlot* e, uint8_t value) {
    g_Timers[TIMER_ENEMY_FRAME_BASE + enemy_slot_index(e)] = value;
}

/* EraseEnemyObject (main.asm:6248-6258). */
void erase_enemy(EnemySlot* e) {
    uint8_t slot = enemy_slot_index(e);
    e->flag = 0;
    e->id = 0;
    e->state = 0;
    e->shell_chain_counter = 0;
    e->floaty_control = 0;
    e->spr_attrib = 0;
    e->interval_timer = 0;
    g_Timers[TIMER_ENEMY_FRAME_BASE + slot] = 0;
    g_Timers[TIMER_ENEMY_INTERVAL_BASE + slot] = 0;
}

void Enemy_Reset(void) {
    memset(enemies, 0, sizeof(enemies));
    memset(fireballs, 0, sizeof(fireballs));
    g_EnemyDataOffset = 0;
    g_EnemyObjectPageLoc = 0;
    g_EnemyObjectPageSel = 0;
    g_EnemyDataLow = 0;
    g_ZeroPageScratch37 = 0;
    g_ZeroPageScratchEF = 0;
    g_ZeroPageScratch07 = 0;
    g_EntranceBubbleYMF = 0;
    g_EnemyFrenzyBuffer = 0;
    g_EnemyFrenzyQueue = 0;
    g_FireworksCounter = 0;
    g_NumberofGroupEnemies = 0;
    g_VineFlagOffset = 0;
    g_VineHeight = 0;
    memset(g_VineObjOffset, 0, sizeof(g_VineObjOffset));
    g_VineStartY = 0;
    s_StompChainCounter = 0;
    s_PowerUpType = 0;
    s_LakituReappearTimer = 0;
    memset(&s_Bowser, 0, sizeof(s_Bowser));
    g_FlagpoleSoundQueue = 0;
    g_FlagpoleCollisionYPos = 0;
    g_FlagpoleScore = 0;
    g_FlagpoleFNumYPos = 0;
    g_FlagpoleFNumYMF_Dummy = 0;
    g_StarFlagTaskControl = 0;
    g_FireballCounter = 0;
    memset(&g_Timers[TIMER_ENEMY_FRAME_BASE], 0, ENEMY_SLOT_COUNT);
    memset(&g_Timers[TIMER_ENEMY_INTERVAL_BASE], 0, ENEMY_SLOT_COUNT);
}

uint8_t Enemy_GetFireballY(uint8_t slot) {
    return slot < FIREBALL_SLOT_COUNT ? fireballs[slot].y : 0;
}

/* ExecGameLoopback (main.asm:4802-4830) owns the parser cursors separately
 * from the object erasure routine.  Keep the enemy stream RAM boundary
 * explicit so the next ObjectOffset pass re-enters ProcessEnemyData from the
 * beginning of the looped section. */
void Enemy_ApplyLoopback(void) {
    g_EnemyDataOffset = 0;
    g_EnemyObjectPageLoc = 0;
    g_EnemyObjectPageSel = 0;
}

/* KillAllEnemies (main.asm:7289-7300) erases slots $00-$04 and the frenzy
 * buffer.  Slot 5 is reserved for the power-up/entrance-vine owner and is
 * not part of this loop, matching the original X=$04 countdown. */
void Enemy_KillAllForLoop(void) {
    int slot;

    for (slot = ENEMY_ALLOC_COUNT - 1; slot >= 0; slot--)
        erase_enemy(&enemies[(uint8_t)slot]);
    g_EnemyFrenzyBuffer = 0;
}

/* KillEnemies (main.asm:2344-2358) scans ordinary slots $04..$00 and clears
 * only Enemy_Flag ($000F+x) on an ID match.  Enemy_ID and Enemy_State are
 * intentionally retained; full EraseEnemyObject belongs to KillAllEnemies
 * and Bowser defeat, not the warp-zone/flagpole kill-ID path. */
void Enemy_KillByID(uint8_t id) {
    int slot;

    for (slot = ENEMY_ALLOC_COUNT - 1; slot >= 0; slot--) {
        if (enemies[slot].id == id)
            enemies[slot].flag = 0;
    }
}

/* FlagpoleSlide (engine/game-mode/routine/flagpole-slide.asm:2-18).
 * Returning zero represents the ROM's NoFPObj branch, which advances the
 * game-engine subroutine rather than fabricating a controller input. */
uint8_t Enemy_FlagpoleSlideInput(uint8_t *input) {
    EnemySlot *e = &enemies[POWERUP_SLOT];

    if (e->id != FlagpoleFlagObject) return 0;
    g_Square1SoundQueue = g_FlagpoleSoundQueue;
    g_FlagpoleSoundQueue = 0;
    *input = (g_Player_Y_Position < 0x9e) ? BTN_DOWN : 0;
    return 1;
}

/* LandPlyr (main.asm:10310-10316) clears StompChainCounter when the player
 * returns to the ground. */
void Enemy_ResetStompChain(void) {
    s_StompChainCounter = 0;
}

/* Screen right edge (16-bit world position) */
uint16_t screen_right(void) {
    return (uint16_t)(((uint16_t)g_ScreenLeft_PageLoc << 8) | g_ScreenLeft_X_Pos) + 255;
}

/* GetEnemyOffscreenBits -> RunOffscrBitsSubs (main.asm:12234-12320). */
uint8_t enemy_offscreen_bits(EnemySlot* e) {
    /* SetOffscrBitsOffset's A=$01 is the byte offset from the generic
     * SprObject position bank ($086/$0b5/$0ce) to the Enemy bank
     * ($087/$0b6/$0cf); the C field pointers are already enemy-owned. */
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    SprScreenEdges edges;
    SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                             g_ScreenLeft_X_Pos);
    s_Enemy_OffscreenBits = SprObject_GetOffscreenBits(&object, &edges);
    return s_Enemy_OffscreenBits;
}
