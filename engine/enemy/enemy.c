#include <stdint.h>
#include <stddef.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "collision.h"
#include "level/level.h"
#include "sprite-offsets.h"
#include "misc.h"
#include "score.h"
#include "assets.h"

/* Enemy core: fireballs, flagpole, hammers, and per-frame process. */

/* FlagpoleObject (main.asm:2744-2762) writes the dynamic flag into the
 * reserved sixth enemy slot.  Only the named constructor fields are written;
 * fractional and state bytes retain the slot's RAM lifetime. */

static uint8_t s_FireballXSpdIndexed[4];
static uint8_t cannon_bitmasks[2];
static uint8_t BulletBillXSpdData[2];
uint8_t s_ExplosionTiles[3];

int Enemy_LoadCoreTables(void) {
    if (Assets_Copy("tables/explosion_tiles.bin", s_ExplosionTiles,
                    sizeof(s_ExplosionTiles)) ||
        Assets_Copy("compat-rom-windows/fireball_x_speed.bin", s_FireballXSpdIndexed,
                    sizeof(s_FireballXSpdIndexed)) ||
        Assets_Copy("tables/cannon_bitmasks.bin", cannon_bitmasks,
                    sizeof(cannon_bitmasks)) ||
        Assets_Copy("tables/bullet_bill_x_spd.bin", BulletBillXSpdData,
                    sizeof(BulletBillXSpdData)))
        return -1;
    return 0;
}

int Enemy_LoadSlotTables(void);
int Enemy_LoadSpawnTables(void);
int Enemy_LoadRunTables(void);
int Enemy_LoadMoveTables(void);
int Enemy_LoadCollideTables(void);
int Enemy_LoadDrawTables(void);

int Enemy_LoadRomTables(void) {
    if (Enemy_LoadSlotTables() != 0) return -1;
    if (Enemy_LoadSpawnTables() != 0) return -1;
    if (Enemy_LoadRunTables() != 0) return -1;
    if (Enemy_LoadMoveTables() != 0) return -1;
    if (Enemy_LoadCollideTables() != 0) return -1;
    if (Enemy_LoadDrawTables() != 0) return -1;
    return Enemy_LoadCoreTables();
}

void Enemy_SetupFlagpole(uint8_t page, uint8_t x) {
    EnemySlot *e = &enemies[POWERUP_SLOT];

    e->flag = 1;
    e->id = FlagpoleFlagObject;
    e->page = page;
    e->x = x;
    /* FlagpoleObject (main.asm:2744-2762) writes X/page, Y, ID, and
     * Enemy_Flag only.  Enemy_Y_HighPos+5 is an InitializeMemory-owned
     * byte and must retain its RAM value for RelativeEnemyPosition and
     * FlagpoleGfxHandler's offscreen branch. */
    e->y = 0x30;
    g_FlagpoleFNumYPos = 0xb0;
}

/* DrawFireball (main.asm:11584-11611). */
void draw_fireball(const FireballSlot* f, uint8_t slot) {
    uint8_t base = SpriteOffset_Fireball(slot);
    uint8_t frame_quarter = (uint8_t)(g_FrameCounter >> 2);

    g_SpriteData[base + 0] = f->rel_y;
    g_SpriteData[base + 1] = (uint8_t)((frame_quarter & 0x01) ^ 0x64);
    g_SpriteData[base + 2] = (uint8_t)(0x02 |
        ((g_FrameCounter & 0x08) ? 0xc0 : 0x00));
    g_SpriteData[base + 3] = f->rel_x;
}

/* DrawExplosion_Fireball (main.asm:11614-11662). */
void draw_fireball_explosion(FireballSlot* f, uint8_t slot) {
    uint8_t old_state = f->state;
    uint8_t animation = (uint8_t)((old_state >> 1) & 0x07);
    uint8_t base = SpriteOffset_FireballExplosion(slot);
    uint8_t tile;
    uint8_t *top_left;

    f->state++;
    if (animation >= 3) {
        f->state = 0;
        return;
    }

    tile = s_ExplosionTiles[animation];
    top_left = &g_SpriteData[base];
    top_left[0] = (uint8_t)(f->rel_y - 4);
    top_left[1] = tile;
    top_left[2] = 0x02;
    top_left[3] = (uint8_t)(f->rel_x - 4);
    top_left[4] = (uint8_t)(f->rel_y + 4);
    top_left[5] = tile;
    top_left[6] = 0x82;
    top_left[7] = (uint8_t)(f->rel_x - 4);
    top_left[8] = (uint8_t)(f->rel_y - 4);
    top_left[9] = tile;
    top_left[10] = 0x42;
    top_left[11] = (uint8_t)(f->rel_x + 4);
    top_left[12] = (uint8_t)(f->rel_y + 4);
    top_left[13] = tile;
    top_left[14] = 0xc2;
    top_left[15] = (uint8_t)(f->rel_x + 4);
}

/* FireballObjCore (main.asm:3238-3291). */
/* FireballObjCore (main.asm:3257-3260) does ldy PlayerFacingDir / dey /
 * lda FireballXSpdData,y with no clamp.  s_FireballXSpdIndexed is indexed by
 * PlayerFacingDir so [0]=table+$FF ($A9 at $B786), [1]=$40, [2]=$C0,
 * [3]=the overflow byte immediately after the table. */

void fireball_process(uint8_t slot) {
    FireballSlot* f = &fireballs[slot];
    SprObjectView object;

    if ((f->state & 0x80) != 0) {
        object = fireball_object(f);
        SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
        draw_fireball_explosion(f, slot);
        return;
    }
    if (f->state == 0) return;

    if (f->state > 1) {
        uint16_t x_sum = (uint16_t)g_Player_X_Position + 4;
        f->x = (uint8_t)x_sum;
        f->page = (uint8_t)(g_Player_PageLoc + (x_sum >> 8));
        f->y = g_Player_Y_Position;
        f->y_high = 1;
        f->x_speed = s_FireballXSpdIndexed[g_PlayerFacingDir];
        f->y_speed = 0x04;
        f->bbox_ctrl = 0x07;
        f->state--;
    }

    object = fireball_object(f);
    SprObject_ImposeGravity(&object, 0x50, 0x00, 0x03, 0);
    (void)SprObject_MoveHorizontally(&object);
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    {
        SprScreenEdges edges;
        SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                                 g_ScreenLeft_X_Pos);
        f->offscreen_bits = SprObject_GetOffscreenBits(&object, &edges);
    }
    fireball_bg_collision(f);
    if ((f->offscreen_bits & 0xcc) != 0) {
        f->state = 0;
        return;
    }
    fireball_enemy_collision(f, slot);
    draw_fireball(f, slot);
}

/* FlagpoleRoutine (main.asm:3506-3552).  This is called from GameEngine
 * after ProcessWhirlpools, before RunGameTimer, exactly at the original
 * special-slot boundary. */
void Enemy_RunFlagpoleRoutine(void) {
    EnemySlot *e = &enemies[POWERUP_SLOT];
    SprObjectView object;

    if (e->id != FlagpoleFlagObject) return;

    if (g_GameEngineSubroutine == 0x04 &&
        g_Player_State == PLAYER_STATE_CLIMB) {
        if (e->y >= 0xaa || g_Player_Y_Position >= 0xa2) {
            uint8_t score = g_FlagpoleScore < 5 ? g_FlagpoleScore : 4;
            Score_AwardPoints(s_FlagpoleScoreDigits[score],
                              s_FlagpoleScoreMods[score]);
            g_GameEngineSubroutine = 0x05;
        } else {
            uint8_t old_mf = e->y_dummy;
            uint16_t mf_sum = (uint16_t)old_mf + 0xff;
            uint8_t old_floaty_mf = g_FlagpoleFNumYMF_Dummy;
            uint8_t floaty_carry = (old_floaty_mf == 0xff);

            e->y_dummy = (uint8_t)mf_sum;
            e->y = (uint8_t)(e->y + 1 + (mf_sum > 0xff));
            g_FlagpoleFNumYMF_Dummy = (uint8_t)(old_floaty_mf + 1);
            g_FlagpoleFNumYPos = (uint8_t)(g_FlagpoleFNumYPos - 1 -
                                           (floaty_carry ? 0 : 1));
        }
    }

    /* GetEnemyOffscreenBits -> RelativeEnemyPosition precedes the dedicated
     * graphics handler, just as it does for a normal enemy. */
    e->offscreen_bits = enemy_offscreen_bits(e);
    object.page_loc = &e->page;
    object.x_position = &e->x;
    object.y_high_pos = &e->y_high;
    object.y_position = &e->y;
    object.x_speed = &e->x_speed;
    object.x_move_force = &e->x_mf;
    object.y_speed = &e->y_speed;
    object.y_move_force = &e->y_mf;
    object.y_mf_dummy = &e->y_dummy;
    object.rel_x = &e->rel_x;
    object.rel_y = &e->rel_y;
    object.offscreen_bits = &e->offscreen_bits;
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    draw_flagpole(e);
}

/* BridgeCollapse (main.asm:7214-7300).  The no-Bowser branch and the
 * defeated-Bowser fall are live.  Castle bridge removal uses the parser-owned
 * object location and the original low-byte table; the princess/toad and
 * full castle-character presentation branches remain explicitly deferred. */
void Enemy_BridgeCollapse(void) {
    uint8_t slot = s_Bowser.front_slot;
    EnemySlot *e;

    if (slot >= ENEMY_ALLOC_COUNT || enemies[slot].id != Bowser) {
        g_EventMusicQueue = Silence;
        g_OperMode_Task++;
        Enemy_KillAllForLoop();
        return;
    }
    e = &enemies[slot];

    if ((e->state & 0x40) != 0) {
        if (e->y >= 0xe0) {
            g_EventMusicQueue = Silence;
            g_OperMode_Task++;
            Enemy_KillAllForLoop();
            return;
        }
        move_slow_special_enemy(e);
    } else if (e->state == 0) {
        if (s_Bowser.feet_counter != 0) s_Bowser.feet_counter--;
        if (s_Bowser.feet_counter == 0) {
            s_Bowser.feet_counter = 4;
            s_Bowser.body_controls ^= 1;
            if (s_Bowser.bridge_offset < sizeof(s_BridgeCollapseData)) {
                Level_CollapseCastleBridge(
                    s_BridgeCollapseData[s_Bowser.bridge_offset]);
                s_Bowser.bridge_offset++;
                g_Square2SoundQueue = Sfx_Blast;
                g_NoiseSoundQueue = Sfx_BrickShatter;
                if (s_Bowser.bridge_offset ==
                    sizeof(s_BridgeCollapseData)) {
                    e->y_speed = 0;
                    e->y_mf = 0;
                    e->state = 0x40;
                    g_Square2SoundQueue = Sfx_BowserFall;
                }
            }
        }
    } else {
        g_EventMusicQueue = Silence;
        g_OperMode_Task++;
        Enemy_KillAllForLoop();
        return;
    }

    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&(SprObjectView){
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    draw_bowser_bridge_pair(e, slot);
}

/* BulletBillHandler (main.asm:3770-3813).  ProcessCannons is the sole
 * producer for ID $33, so this handler is intentionally outside the normal
 * RunEnemyObjectsCore table.  The low-byte distance test preserves the
 * carry left by PlayerEnemyDiff's page SBC: the original ADC #$28 is not an
 * absolute-distance comparison and has a different boundary on the right. */
void run_cannon_bullet(EnemySlot* e, uint8_t slot) {
    uint8_t enemy_left;
    uint8_t difference;
    uint8_t no_borrow;

    e->offscreen_bits = enemy_offscreen_bits(e);
    if (g_TimerControl == 0 && e->state == 0) {
        if ((e->offscreen_bits & 0x0c) == 0x0c) {
            erase_enemy(e);
            return;
        }

        difference = enemy_player_diff_with_carry(e, &enemy_left, &no_borrow);
        e->moving_dir = enemy_left ? BTN_RIGHT : BTN_LEFT;
        e->x_speed = BulletBillXSpdData[enemy_left ? 0 : 1];
        /* PlayerEnemyDiff leaves the page-SBC carry live.  Model
         * `lda $00 / adc #$28 / cmp #$50 / bcc KillBB` as 8-bit arithmetic. */
        if ((uint8_t)(difference + 0x28 + no_borrow) < 0x50) {
            erase_enemy(e);
            return;
        }

        e->state = 0x01;
        set_enemy_frame_timer(e, 0x0a);
        g_Square2SoundQueue = Sfx_Blast;
    }

    if (g_TimerControl == 0) {
        if ((e->state & 0x20) != 0)
            move_enemy_vertically(e);
        move_enemy_x(e);
    }

    /* GetEnemyOffscreenBits -> RelativeEnemyPosition -> GetEnemyBoundBox ->
     * PlayerEnemyCollision -> EnemyGfxHandler is the exact handler order. */
    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&(SprObjectView){
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    /* RelativeEnemyPosition writes Enemy_Rel_XPos ($03ae), shared scratch
     * consumed by GetEnemyBoundBox and SetupFloateyNumber. */
    g_Enemy_Rel_XPos = e->rel_x;
    enemy_get_bound_box(e);
    player_enemy_collision(e);
    draw_special_enemy(e, slot);
    /* BulletBillHandler jumps to the complete EnemyGfxHandler tail.  Its
     * SprObjectOffscrChk hides all OAM rows for offscreen bit 7 and erases an
     * object in Y high page $02 (except Podoboo) at main.asm:11360-11403. */
    enemy_graphics_offscreen_cleanup(e);
}

/* WarpZoneObject (main.asm:3397-3405, reached by the $34 entry in
 * RunEnemyObjectsCore at main.asm:6077-6124).  The enemy stream's no-init
 * entry intentionally leaves this object in its parser-owned slot; this
 * handler is the consumer of the shared ScrollLock/WarpZoneControl state.
 * The AND is kept as an 8-bit operation: on the success path its zero result
 * is exactly what the 6502 stores back to ScrollLock before incrementing the
 * warp selector and erasing the current object. */
void run_warp_zone(EnemySlot* e) {
    if (g_ScrollLock == 0)
        return;
    if ((uint8_t)(g_Player_Y_Position & g_Player_Y_HighPos) != 0)
        return;

    g_ScrollLock = 0;
    g_WarpZoneControl++;
    erase_enemy(e);
}

/* ProcessCannons (main.asm:3705-3763).  Cannon records are the shared
 * MiscCannonRecord bank at $046A-$0482; enemy slots 2,1,0 are the only
 * possible Bullet Bill consumers. */
void Enemy_ProcessCannons(void) {
    int slot;

    if (g_AreaType == AREA_TYPE_WATER)
        return;

    for (slot = 2; slot >= 0; slot--) {
        EnemySlot *enemy = &enemies[slot];

        if (enemy->flag == 0) {
            uint8_t mask = cannon_bitmasks[g_SecondaryHardMode ? 1 : 0];
            uint8_t cannon_slot =
                g_PseudoRandomBitReg[1 + slot] & mask;
            const MiscCannonRecord *cannon;

            /* ProcessCannons branches to Chk_BB for every failed spawn
             * predicate; an empty flag does not skip the stale Enemy_ID
             * cleanup below. */
            if (cannon_slot < 6) {
                cannon = Misc_GetCannon(cannon_slot);
                if (cannon != NULL && cannon->page != 0) {
                    if (cannon->timer != 0) {
                        /* The ROM reaches Chk_BB after this SBC #$00. */
                        Misc_SetCannonTimer(cannon_slot,
                                            (uint8_t)(cannon->timer - 1));
                    } else if (g_TimerControl == 0) {
                        /* FireCannon jumps directly to Next3Slt after
                         * filling the physical enemy slot. */
                        Misc_SetCannonTimer(cannon_slot, 0x0e);
                        enemy->page = cannon->page;
                        enemy->x = cannon->x_or_left;
                        enemy->y = (uint8_t)(cannon->y_or_length - 0x08);
                        enemy->y_high = 0x01;
                        enemy->flag = 0x01;
                        enemy->state = 0;
                        enemy->bbox_ctrl = 0x09;
                        enemy->id = BulletBill_CannonVar;
                        continue;
                    }
                }
            }
        }

        /* Chk_BB is the common target after both an occupied slot and every
         * unsuccessful empty-slot spawn branch (main.asm:3723-3759).  This
         * lets OffscreenBoundsCheck erase a stale cannon ID after a
         * flag-only KillEnemies call. */
        if (enemy->id != BulletBill_CannonVar)
            continue;
        enemy_offscreen_bounds(enemy);
        if (enemy->flag != 0)
            run_cannon_bullet(enemy, (uint8_t)slot);
    }

}

void Enemy_ProcessObjectPass(uint8_t i) {
    uint8_t was_empty;

    if (i >= ENEMY_SLOT_COUNT) return;
    /* EnemiesAndLoopsCore's high-flag branch is the duplicate/Bowser
     * ownership check (main.asm:4766-4788), not an ordinary active object. */
    if (enemies[i].flag & 0x80) {
        uint8_t linked_slot = enemies[i].flag & 0x0f;
        if (linked_slot < ENEMY_SLOT_COUNT &&
            enemies[linked_slot].flag != 0) return;
        enemies[i].flag = 0;
        return;
    }
    /* GameEngine/ProcELoop (engine/game-mode/core.asm:12-24) makes six
     * ObjectOffset passes ($00..$05).  ProcessEnemyData rejects regular
     * parser work at slot 5, while HandleGroupEnemies allocates slots 0..4. */
    was_empty = (uint8_t)!enemies[i].flag;
    if (was_empty) {
        /* Empty slots skip parser work during AreaParserTaskNum&7 == 7,
         * matching the ExitELCore branch before ProcLoopCommand. */
        if ((g_AreaParserTaskNum & 0x07) == 0x07) return;
        /* ProcLoopCommand precedes ChkEnemyFrenzy in EnemiesAndLoopsCore;
         * a successful loopback can rewrite the parser cursors and erase
         * ordinary slots before the queued frenzy object is consumed. */
        if (g_LoopCommand != 0)
            Level_ProcessLoopCommand();
        if (g_EnemyFrenzyQueue != 0) {
            enemies[i].id = g_EnemyFrenzyQueue;
            enemies[i].flag = 1;
            enemies[i].state = 0;
            g_EnemyFrenzyQueue = 0;
            init_checkpointed_enemy(&enemies[i]);
            return;
        }
        parse_stream(i);
        /* A newly initialized current slot returns from the parser path; it
         * is first run on the next ProcELoop pass.  Group members in later
         * slots are already active when their pass is reached. */
        if (was_empty) return;
    }
    /* ProcessEnemyData -> CheckpointEnemyID initializes an empty slot and
     * returns from EnemiesAndLoopsCore; RunEnemyObjectsCore is not entered
     * until the next ObjectOffset pass. */
    if (enemies[i].id < sizeof(s_EnemyRunDispatch)) {
        switch (s_EnemyRunDispatch[enemies[i].id]) {
        case ENEMY_RUN_NORMAL:
            run_normal_enemy(&enemies[i], i);
            break;
        case ENEMY_RUN_POWERUP:
            process_powerup(&enemies[i]);
            break;
        case ENEMY_RUN_RETAINER:
            run_retainer_enemy(&enemies[i]);
            break;
        case ENEMY_RUN_BOWSER_FLAME:
            run_bowser_flame(&enemies[i], i);
            enemy_offscreen_bounds(&enemies[i]);
            break;
        case ENEMY_RUN_FIREBAR:
            run_firebar(&enemies[i], i);
            enemy_offscreen_bounds(&enemies[i]);
            break;
        case ENEMY_RUN_BOWSER:
            run_bowser(&enemies[i], i);
            enemy_offscreen_bounds(&enemies[i]);
            break;
        case ENEMY_RUN_FIREWORKS:
            run_fireworks(&enemies[i], i);
            break;
        case ENEMY_RUN_STAR_FLAG:
            run_star_flag(&enemies[i], i);
            break;
        case ENEMY_RUN_LARGE_PLATFORM:
            run_large_platform(&enemies[i], i);
            break;
        case ENEMY_RUN_SMALL_PLATFORM:
            run_small_platform(&enemies[i], i);
            break;
        case ENEMY_RUN_VINE:
            run_vine(&enemies[i], i);
            break;
        case ENEMY_RUN_WARP_ZONE:
            run_warp_zone(&enemies[i]);
            break;
        case ENEMY_RUN_JUMPSPRING:
            run_jumpspring_enemy(&enemies[i], i);
            break;
        case ENEMY_RUN_NO_CODE:
        default:
            /* NoRunCode entries remain intentional no-ops. */
            break;
        }
    }
}

void Enemy_ProcessFloateyNumbersPass(uint8_t i) {
    uint8_t oam_byte_offset;

    if (i >= ENEMY_SLOT_COUNT) return;
    /* FloateyNumbersRoutine is called immediately after each
     * EnemiesAndLoopsCore pass (core.asm:16), including empty passes. */
    oam_byte_offset = floaty_oam_offset(&enemies[i], i);
    draw_floaty(&enemies[i], oam_byte_offset);
}

void Enemies_Core(void) {
    uint8_t i;
    for (i = 0; i < ENEMY_SLOT_COUNT; i++) {
        Enemy_ProcessObjectPass(i);
        Enemy_ProcessFloateyNumbersPass(i);
    }
}

/* MiscObjectsCore needs the spawning enemy's live parallel fields while the
 * Misc_State records remain owned by collision.c.  Snapshot only the
 * producer-owned RAM needed by ProcHammerObj; no position is synthesized for
 * a missing source slot. */
void Enemy_ProcessHammers(void) {
    CollisionHammerSource sources[7] = {{0}};
    uint8_t clear_mask;
    uint8_t i;

    for (i = 0; i < ENEMY_SLOT_COUNT; i++) {
        sources[i].state = enemies[i].state;
        sources[i].moving_dir = enemies[i].moving_dir;
        sources[i].page = enemies[i].page;
        sources[i].x = enemies[i].x;
        sources[i].y = enemies[i].y;
    }
    /* HammerEnemyOfsData contains a seventh, overlaid/residual source value
     * ($06).  It is represented by the zero-initialized final record until
     * that ROM alias is independently identified. */
    clear_mask = Collision_ProcessHammers(sources, 7);
    for (i = 0; i < ENEMY_SLOT_COUNT; i++) {
        if (clear_mask & (uint8_t)(1u << i))
            enemies[i].state &= 0xf7;
    }
}

/* ProcFireball_Bubble (main.asm:3182-3231).  Fireballs are now real object
 * slots; air-bubble creation/movement remains the next P4 cluster. */
void Enemy_ProcFireballBubble(void) {
    if (g_PlayerStatus == PLAYER_STATUS_FIRE &&
        (g_A_B_Buttons & BTN_B) != 0 &&
        (g_PreviousA_B_Buttons & BTN_B) == 0) {
        uint8_t slot = (uint8_t)(g_FireballCounter & 0x01);
        FireballSlot* f = &fireballs[slot];

        if (f->state == 0 && g_Player_Y_HighPos == 1 &&
            g_CrouchingFlag == 0 && g_Player_State != PLAYER_STATE_CLIMB) {
            g_Square1SoundQueue = Sfx_Fireball;
            f->state = 0x02;
            g_FireballThrowingTimer = g_PlayerAnimTimerSet;
            g_PlayerAnimTimer = (uint8_t)(g_PlayerAnimTimerSet - 1);
            g_FireballCounter++;
        }
    }

    fireball_process(0);
    fireball_process(1);
    Misc_ProcAirBubbles();
}
