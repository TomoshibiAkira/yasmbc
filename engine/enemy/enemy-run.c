#include <stdint.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "level/level.h"
#include "sprite-offsets.h"
#include "score.h"
#include "screen/routine/colors.h"
#include "assets.h"

/* Enemy run handlers (vine, platforms, Bowser, etc.). */

/* VineObjectHandler (main.asm:3642-3685).  The entrance path uses the
 * reserved slot 5; later multi-vine allocation remains represented by the
 * same VineFlagOffset/VineObjOffset RAM and can add slots without changing
 * this call boundary. */

static uint8_t VineHeightData[2];
static uint8_t jumpspring_y_pos_data[4];
static uint8_t flame_y[4];

int Enemy_LoadRunTables(void) {
    if (Assets_Copy("tables/vine_height.bin", VineHeightData,
                    sizeof(VineHeightData)) ||
        Assets_Copy("tables/jumpspring_ypos.bin", jumpspring_y_pos_data,
                    sizeof(jumpspring_y_pos_data)) ||
        Assets_Copy("tables/flame_ypos.bin", flame_y, sizeof(flame_y)))
        return -1;
    return 0;
}

void run_vine(EnemySlot* e, uint8_t slot) {
    LevelBlockBufferProbe probe;
    uint8_t target;
    uint8_t i;

    if (slot != POWERUP_SLOT || g_VineFlagOffset == 0) return;
    target = VineHeightData[(g_VineFlagOffset - 1) & 1];

    /* FrameCounter >> 2 bit 0 is the exact LSR/LSR carry gate. */
    if (g_VineHeight != target && (g_FrameCounter & 0x02) != 0) {
        e->y--;
        g_VineHeight++;
    }
    if (g_VineHeight < 0x08) return;

    {
        SprObjectView object = {
            &e->page, &e->x, &e->y_high, &e->y,
            0, 0, 0, 0, 0, &e->rel_x, &e->rel_y, &e->offscreen_bits
        };
        SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    }
    e->offscreen_bits = enemy_offscreen_bits(e);
    for (i = 0; i < g_VineFlagOffset && i < sizeof(g_VineObjOffset); i++)
        draw_vine_segment(e, g_VineObjOffset[i], i);

    if ((e->offscreen_bits & 0x0c) != 0) {
        /* KillVine erases each owner listed by VineObjOffset, then clears
         * both persistent vine counters. */
        for (i = 0; i < g_VineFlagOffset && i < sizeof(g_VineObjOffset); i++) {
            uint8_t owner = g_VineObjOffset[i];
            if (owner < ENEMY_SLOT_COUNT) erase_enemy(&enemies[owner]);
        }
        g_VineFlagOffset = 0;
        g_VineHeight = 0;
        return;
    }

    if (g_VineHeight < 0x20) return;
    /* Once the vine is tall enough, WrCMTile writes the climbable $26 only
     * into an empty block-buffer cell selected by the shared producer. */
    if (Level_BlockBufferCollision(&probe, e->page, e->x, e->y,
                                   0x1b, 1) == 0 &&
        probe.aligned_y < 0xd0) {
        /* main.asm:3685-3695 performs STA ($06),Y.  It does not queue a
         * nametable write here; the parser/NMI owner will expose the raw
         * block-buffer byte on its normal schedule. */
        Level_WriteBlockBufferProbe(&probe, 0x26);
    }
}

/* RunFireworks (main.asm:7587-7616).  The per-object timer is intentionally
 * not routed through DecTimers: the 6502 decrements ExplosionTimerCounter
 * ($a0+x, the object's Y-speed alias) directly, advances the
 * ExplosionGfxCounter ($58+x) every eight frames, then awards 500 points and
 * disables only the current enemy flag. */
void run_fireworks(EnemySlot* e, uint8_t slot) {
    SprObjectView object;

    e->y_speed--;
    if (e->y_speed == 0) {
        e->y_speed = 0x08;
        e->x_speed++;
        if (e->x_speed >= 0x03) {
            e->flag = 0;
            g_Square2SoundQueue = Sfx_Blast;
            Score_AwardPoints(4, 5);
            return;
        }
    }

    object.page_loc = &e->page;
    object.x_position = &e->x;
    object.y_high_pos = &e->y_high;
    object.y_position = &e->y;
    object.x_speed = 0;
    object.x_move_force = 0;
    object.y_speed = 0;
    object.y_move_force = 0;
    object.y_mf_dummy = 0;
    object.rel_x = &e->rel_x;
    object.rel_y = &e->rel_y;
    object.offscreen_bits = 0;
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    draw_fireworks_explosion(e, slot, e->x_speed);
}

/* RunStarFlagObj/GameTimerFireworks/AwardGameTimerPoints/
 * RaiseFlagSetoffFWorks/DelayToAreaEnd (main.asm:7627-7753). */
void run_star_flag(EnemySlot* e, uint8_t slot) {
    SprObjectView object;
    uint8_t task = g_StarFlagTaskControl;

    g_EnemyFrenzyBuffer = 0;
    if (task >= 0x05) return;

    if (task == 0) return;

    if (task == 1) {
        uint8_t timer_lsd = g_DisplayDigits[35];
        uint8_t state = 5;

        if (timer_lsd == 1) {
            g_FireworksCounter = 1;
        } else {
            state = 3;
            if (timer_lsd == 3) {
                g_FireworksCounter = 3;
            } else {
                state = 0;
                if (timer_lsd == 6)
                    g_FireworksCounter = 6;
                else
                    g_FireworksCounter = 0xff;
            }
        }
        e->state = state;
        g_StarFlagTaskControl++;
        return;
    }

    if (task == 2) {
        if ((uint8_t)(g_DisplayDigits[33] | g_DisplayDigits[34] |
                      g_DisplayDigits[35]) == 0) {
            g_StarFlagTaskControl++;
            return;
        }
        if ((g_FrameCounter & 0x04) != 0)
            g_Square2SoundQueue = Sfx_TimerTick;
        Score_SetDigitModifier(5, 0xff);
        Score_DigitsMath(0x23);
        Score_SetDigitModifier(5, 5);
        Score_AwardGameTimerPoints();
        return;
    }

    if (task == 3) {
        if (e->y >= 0x72) {
            e->y--;
        } else if (g_FireworksCounter != 0 &&
                   (g_FireworksCounter & 0x80) == 0) {
            g_EnemyFrenzyBuffer = Fireworks;
        } else {
            draw_star_flag(e, slot);
            set_enemy_interval_timer(e, 0x06);
            g_StarFlagTaskControl++;
            return;
        }
    } else if (task == 4) {
        if (g_Timers[TIMER_ENEMY_INTERVAL_BASE + slot] == 0 &&
            g_EventMusicBuffer == 0)
            g_StarFlagTaskControl++;
    }

    object.page_loc = &e->page;
    object.x_position = &e->x;
    object.y_high_pos = &e->y_high;
    object.y_position = &e->y;
    object.x_speed = 0;
    object.x_move_force = 0;
    object.y_speed = 0;
    object.y_move_force = 0;
    object.y_mf_dummy = 0;
    object.rel_x = &e->rel_x;
    object.rel_y = &e->rel_y;
    object.offscreen_bits = 0;
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    draw_star_flag(e, slot);
}

/* JumpspringHandler (main.asm:3556-3609) is the $32 enemy-object consumer.
 * Its player movement, fixed-Y update, A-edge bounce force, graphics call,
 * offscreen bounds check, and timer/animation advance remain in this one
 * ObjectOffset pass, before PlayerGfxHandler in GameEngine. */
void run_jumpspring_enemy(EnemySlot* e, uint8_t slot) {
    uint8_t frame;
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };

    /* GetEnemyOffscreenBits precedes the animation branch in the ROM. */
    e->offscreen_bits = enemy_offscreen_bits(e);
    if (g_TimerControl == 0 && g_JumpspringAnimCtrl != 0) {
        frame = (uint8_t)(g_JumpspringAnimCtrl - 1);

        /* The valid animation controls are 1..4; the ROM indexes this table
         * directly, with no alternate state or coordinate source. */
        if ((frame & 0x02) == 0)
            g_Player_Y_Position = (uint8_t)(g_Player_Y_Position + 2);
        else
            g_Player_Y_Position = (uint8_t)(g_Player_Y_Position - 2);
        e->y = (uint8_t)(e->x_speed + jumpspring_y_pos_data[frame]);

        if (frame >= 1 && (g_A_B_Buttons & BTN_A) != 0 &&
            (g_PreviousA_B_Buttons & BTN_A) == 0)
            g_JumpspringForce = 0xf4;

        if (frame == 3) {
            g_Player_Y_Speed = g_JumpspringForce;
            g_JumpspringAnimCtrl = 0;
        }
    }

    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    draw_special_enemy(e, slot);
    enemy_offscreen_bounds(e);

    /* The timer gate is after OffscreenBoundsCheck and is independent of
     * TimerControl, matching the final JumpspringHandler branch. */
    if (g_JumpspringAnimCtrl == 0 || g_Timers[TIMER_JUMPSPRING] != 0)
        return;
    g_Timers[TIMER_JUMPSPRING] = 0x04;
    g_JumpspringAnimCtrl++;
}

void run_large_platform(EnemySlot *e, uint8_t slot) {
    SprObjectView object = platform_object(e);

    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    platform_get_bound_box(e, 0);
    platform_collision(e, 0);
    if (g_TimerControl == 0) {
        switch (e->id) {
        case ENTITY_BAL_PLATFORM:
            balance_platform(e, slot);
            break;
        case ENTITY_VERT_PLATFORM:
            vertical_platform(e);
            break;
        case ENTITY_LARGE_LIFT_UP:
        case ENTITY_LARGE_LIFT_DOWN:
            move_lift_platforms(e);
            if (e->platform_collision_flag != 0xff)
                position_player_on_vplat(e, e->y);
            break;
        case ENTITY_HORI_PLATFORM:
            horizontal_platform(e, 0);
            break;
        case ENTITY_DROP_PLATFORM:
            drop_platform(e);
            break;
        case ENTITY_LARGE_LIFT_UP_2:
            right_platform(e);
            break;
        default:
            break;
        }
    }
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    draw_large_platform(e, slot);
    enemy_offscreen_bounds(e);
}

void run_small_platform(EnemySlot *e, uint8_t slot) {
    SprObjectView object = platform_object(e);

    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    platform_get_bound_box(e, 1);
    platform_collision(e, 1);
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    draw_small_platform(e, slot);
    if (g_TimerControl == 0) move_lift_platforms(e);
    if (e->platform_collision_flag != 0)
        position_player_on_splat(e, e->platform_collision_flag);
    enemy_offscreen_bounds(e);
}

/* OffscreenBoundsCheck (main.asm:8208-8268) for the power-up slot.  The
 * object is retained in the extended 0x48-pixel horizontal window and is
 * erased from its owning slot only after the page/X comparison crosses the
 * selected boundary. */
void powerup_offscreen_bounds(EnemySlot* e) {
    uint16_t position = ((uint16_t)e->page << 8) | e->x;
    uint16_t left = (uint16_t)(
        (((uint16_t)g_ScreenLeft_PageLoc << 8) | g_ScreenLeft_X_Pos) -
        0x48);
    uint16_t right = (uint16_t)(screen_right() + 0x48);
    uint16_t delta;

    /* Same CMP/SBC negative-bit contract as OffscreenBoundsCheck above. */
    delta = (uint16_t)(position - left);
    if ((delta & 0x8000) != 0) {
        erase_enemy(e);
        return;
    }
    delta = (uint16_t)(position - right);
    if ((delta & 0x8000) == 0) erase_enemy(e);
}

/* BoundingBoxCore/PlayerCollisionCore (main.asm:10155-10293).  The
 * comparisons intentionally use 8-bit coordinates: the original routine
 * treats a wrapped bottom/right edge as an overlap rather than converting
 * the object into a host-width world rectangle. */
uint8_t powerup_boxes_overlap(const EnemySlot* e) {
    CollisionBox powerup_box;
    CollisionBox player_box;

    powerup_box = enemy_collision_box(e);
    player_box = player_collision_box();
    return collision_boxes_overlap(&powerup_box, &player_box);
}

/* SetupFloateyNumber (main.asm:8760-8770).  The fields survive
 * EraseEnemyObject because they are the same slot-owned floaty record. */
void setup_floaty_number(EnemySlot* e, uint8_t control) {
    e->floaty_control = control;
    e->floaty_timer = 0x30;
    e->floaty_y = e->y;
    e->floaty_x = g_Enemy_Rel_XPos;
}

/* HandlePowerUpCollision (main.asm:8451-8497).  Sound/APU playback remains
 * excluded; the gameplay state and music queue writes are retained. */
void handle_powerup_collision(EnemySlot* e) {
    uint8_t type = s_PowerUpType;

    erase_enemy(e);
    setup_floaty_number(e, type == 0x03 ? 0x0b : 0x06);
    g_Square2SoundQueue = Sfx_PowerUpGrab;

    if (type == 0x03) {
        return; /* SetFor1Up changes only floaty control; the later
                 * FloateyNumbersRoutine owns Sfx_ExtraLife. */
    }
    if (type >= 0x02) {
        g_StarInvincibleTimer = 0x23;
        g_AreaMusicQueue = StarPowerMusic;
        return;
    }

    if (g_PlayerStatus == PLAYER_STATUS_SMALL) {
        g_PlayerStatus = PLAYER_STATUS_BIG;
        g_GameEngineSubroutine = 0x09; /* PlayerChangeSize */
        g_Player_State = PLAYER_STATE_GROUND;
        g_TimerControl = 0xff;
        g_ScrollAmount = 0;
    } else if (g_PlayerStatus == PLAYER_STATUS_BIG) {
        g_PlayerStatus = PLAYER_STATUS_FIRE;
        GetPlayerColors();
        g_GameEngineSubroutine = 0x0c; /* PlayerFireFlower */
        g_Player_State = PLAYER_STATE_GROUND;
        g_TimerControl = 0xff;
        g_ScrollAmount = 0;
    }
}

/* PlayerEnemyCollision (main.asm:8508-8536) restricted to the power-up
 * owner.  The call is made after DrawPowerUp and before OffscreenBoundsCheck
 * in PowerUpObjHandler, on even FrameCounter values only. */
void powerup_player_collision(EnemySlot* e) {
    if ((g_FrameCounter & 1) != 0) return;
    if (!player_collision_vertical_ok()) return;
    if (g_GameEngineSubroutine != 0x08) return;
    if (e->state & 0x20) return;
    if (e->offscreen_masked != 0) return;

    if (powerup_boxes_overlap(e)) handle_powerup_collision(e);
}

/* PowerUpObjHandler (main.asm:4127-4178).  Rising state $01..$11 is
 * advanced on FrameCounter&3; state $80 then dispatches the mushroom/1-up
 * horizontal path or the star gravity/jump path.  PlayerEnemyCollision and
 * HandlePowerUpCollision remain a separate roadmap cluster, so this routine
 * deliberately stops at the draw/offscreen boundary. */
void process_powerup(EnemySlot* e) {
    SprObjectView object;

    if (!e->state) return;
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
    if (!(e->state & 0x80)) {
        if ((g_FrameCounter & 0x03) == 0) {
            uint8_t old_state = e->state;
            e->y--;
            e->state++;
            if (old_state >= 0x11) {
                e->x_speed = 0x10;
                e->state = 0x80;
                e->spr_attrib = 0x00;
                e->moving_dir = 0x01;
            }
        }
        if (e->state < 0x06) return;
    } else if (g_TimerControl == 0) {
        if (s_PowerUpType == 0x02) {
            /* MoveJumpingEnemy → EnemyJump: the shared gravity table is
             * $1c/$03, then EnemyJump owns the falling-only block probe and
             * restarts a supported landing at $fd.  It must not use
             * EnemyToBGCollisionDet: that routine's no-background branch
             * changes a d7 object to d7|d6, which the 6502 does not do for
             * this handler. */
            SprObject_ImposeGravity(&object, 0x1c, 0x00, 0x03, 0);
            (void)SprObject_MoveHorizontally(&object);
            enemy_jumping_bg_check(e);
        }
        if (s_PowerUpType == 0x00 || s_PowerUpType == 0x03) {
            /* PowerUpObjHandler -> ShroomM calls MoveNormalEnemy only for
             * regular and 1-up mushrooms.  Its state-$40 path is
             * MoveD_EnemyVertically followed by SteadM (main.asm:4139-4147,
             * 6330-6410), then EnemyToBGCollisionDet owns the landing/state
             * transition. */
            move_normal_enemy(e);
            enemy_to_bg_collision_det(e);
        }
    }

    /* RunPUSubs: RelativeEnemyPosition → GetEnemyOffscreenBits →
     * GetEnemyBoundBox → DrawPowerUp → PlayerEnemyCollision →
     * OffscreenBoundsCheck.  DrawPowerUp is a tail call to
     * SprObjectOffscrChk (main.asm:10820-10866, 11360-11403), so retain
     * that control-flow edge before the collision call.  It uses the
     * object-owned offscreen mask ($03d1+x), Enemy_ID ($16+x),
     * Enemy_Y_HighPos ($b6+x), and EraseEnemyObject's
     * EnemyFrameTimer alias ($078a+x); it is not a screen- or slot-specific
     * cleanup. */
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    g_Enemy_Rel_XPos = e->rel_x;
    e->offscreen_bits = enemy_offscreen_bits(e);
    enemy_get_bound_box(e);
    draw_powerup(e);
    enemy_graphics_offscreen_cleanup(e);
    powerup_player_collision(e);
    powerup_offscreen_bounds(e);
}

void run_firebar(EnemySlot* e, uint8_t slot) {
    uint8_t segment_count = (e->id >= 0x1f) ? 11 : 5;
    uint8_t segment;
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t duplicate = linked_duplicate_slot(slot);

    /* ProcFirebar (main.asm:6808-6871) owns the offscreen gate, spin-state
     * arithmetic, GetFirebarPosition tables, and the duplicate OAM handoff at
     * segment five of a long bar. */
    e->offscreen_bits = enemy_offscreen_bits(e);
    if (e->offscreen_bits & 0x08) return;
    if (g_TimerControl == 0) firebar_spin(e);
    e->y_speed &= 0x1f;
    /* ProcFirebar avoids the horizontal phase at $08/$18 for long bars.
     * This is a state-table branch after the five-bit spin mask, not a
     * renderer adjustment: the corrected high phase remains the owner in
     * Enemy_Y_Speed/$00a0 for both lookup and the next tick. */
    if (e->id >= 0x1f &&
        (e->y_speed == 0x08 || e->y_speed == 0x18))
        e->y_speed++;
    /* ProcFirebar's SetupGFB stores its post-adjustment spin high byte in
     * $ef before the shared firebar position lookups (main.asm:6833). */
    g_ZeroPageScratchEF = e->y_speed;
    SprObject_GetRelativePosition(&(SprObjectView){
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    draw_firebar_segment(e, base, 0xff, e->rel_x, e->rel_y);
    for (segment = 0; segment < segment_count; segment++) {
        uint8_t segment_base = base;
        /* The ROM switches to DuplicateObj_Offset after drawing the
         * segment whose counter is $04.  The next segment ($05) is the
         * first one written in the linked slot; its OAM offset starts at
         * that slot's base, not at a renderer-local fixed position. */
        if (e->id >= 0x1f && segment >= 5 && duplicate != 0xff) {
            segment_base = (uint8_t)(SpriteOffset_Enemy(duplicate) +
                                     (segment - 5) * 4);
        } else {
            segment_base = (uint8_t)(base + 4 + segment * 4);
        }
        /* DrawFbar starts its counter at zero after the center sprite; do
         * not skip the table's row-zero arm on the first iteration. */
        draw_firebar_segment(e, segment_base, segment,
                             e->rel_x, e->rel_y);
    }
}

void run_bowser_flame(EnemySlot* e, uint8_t slot) {
    uint8_t target = flame_y[e->y_dummy & 3];

    /* ProcBowserFlame (main.asm:7491-7583) uses the flame's own $0401/$0434
     * force bytes and its $0417 random target before the graphics writer. */
    if (g_TimerControl == 0) {
        uint8_t amount = g_SecondaryHardMode ? 0x60 : 0x40;
        uint8_t borrow = e->x_mf < amount;
        e->x_mf = (uint8_t)(e->x_mf - amount);
        {
            uint16_t position = (uint16_t)(((uint16_t)e->page << 8) | e->x);
            position = (uint16_t)(position - 1 - borrow);
            e->page = (uint8_t)(position >> 8);
            e->x = (uint8_t)position;
        }
        if (e->y != target) e->y = (uint8_t)(e->y + e->y_mf);
    }
    SprObject_GetRelativePosition(&(SprObjectView){
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    e->offscreen_bits = enemy_offscreen_bits(e);
    if (e->state == 0) draw_bowser_flame(e, slot);
    enemy_get_bound_box(e);
    player_enemy_collision(e);
}

void run_bowser_half(EnemySlot* e, uint8_t slot,
                            uint8_t graphics_flag) {
    /* ProcessBowserHalf (main.asm:7459-7467) owns this transient selector;
     * it remains live through the half's EnemyGfxHandler and collision call. */
    s_Bowser.gfx_flag = graphics_flag;
    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&(SprObjectView){
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    draw_special_enemy(e, slot);
    if (e->state == 0) {
        e->bbox_ctrl = 0x0a;
        enemy_get_bound_box(e);
        player_enemy_collision(e);
    }
}

void run_bowser(EnemySlot* e, uint8_t slot) {
    uint8_t rear = linked_duplicate_slot(slot);
    uint8_t set_timer_from_height = 0;

    /* RunBowser/BowserGfxHandler (main.asm:7283-7472).  Bridge removal and
     * level-end presentation remain separate Victory-mode boundaries.  The
     * world-6+ HammerChk producer is kept here because SpawnHammerObj stores
     * this Bowser ObjectOffset for the later MiscObjectsCore pass. */
    if ((e->state & 0x20) != 0) {
        if (e->y < 0xe0) {
            move_enemy_gravity(e, 0x0f, 0x00, 0x02);
        } else {
            /* RunBowser's defeated path enters KillAllEnemies once the
             * falling front reaches $e0.  That routine erases the ordinary
             * enemy slots $00-$04 and clears EnemyFrenzyBuffer; it is not a
             * Bowser-only erase of the front/rear pair. */
            Enemy_KillAllForLoop();
            return;
        }
    } else {
        g_EnemyFrenzyBuffer = 0;
        if (g_TimerControl == 0) {
        if (s_Bowser.feet_counter != 0) s_Bowser.feet_counter--;
        if (s_Bowser.feet_counter == 0) {
            s_Bowser.feet_counter = 0x20;
            s_Bowser.body_controls ^= 1;
        }
        if ((g_FrameCounter & 0x0f) == 0) e->moving_dir = 0x02;
        if (g_Timers[TIMER_ENEMY_FRAME_BASE + slot] != 0) {
            uint8_t negative;
            (void)enemy_player_diff(e, &negative);
            if (negative) {
                e->moving_dir = 0x01;
                s_Bowser.movement_speed = 0x02;
                set_enemy_frame_timer(e, 0x20);
                g_BowserFireBreathTimer = 0x20;
            }
        }
        /* GetPRCmp follows the timer/direction branch in RunBowser; a
         * nonzero EnemyFrameTimer does not skip the every-fourth-frame
         * origin/range movement check (main.asm:7326-7371). */
        if ((g_FrameCounter & 0x03) == 0) {
            if (e->x == s_Bowser.orig_x)
                s_Bowser.max_range = s_BowserPRandomRange[
                    g_PseudoRandomBitReg[slot] & 3];
            e->x = (uint8_t)(e->x + s_Bowser.movement_speed);
            if (e->moving_dir != 0x01) {
                uint8_t difference = byte_abs_delta(e->x, s_Bowser.orig_x);
                if (difference >= s_Bowser.max_range)
                    s_Bowser.movement_speed = (e->x >= s_Bowser.orig_x) ?
                        0xff : 0x01;
            }
        }
        if (g_Timers[TIMER_ENEMY_FRAME_BASE + slot] == 1) {
            e->y--;
            e->y_speed = 0xfe;
            e->y_mf = 0;
        } else if (g_Timers[TIMER_ENEMY_FRAME_BASE + slot] == 0) {
            move_enemy_gravity(e, 0x0f, 0x00, 0x02);
            set_timer_from_height = 1;
        }
        /* HammerChk: only an expired EnemyFrameTimer reaches MoveEnemySlowVert
         * and the every-fourth-frame SpawnHammerObj call.  The ROM enables
         * this producer for worlds 6 and 7; TimerControl and the defeated
         * state have already been gated above. */
        if (g_Timers[TIMER_ENEMY_FRAME_BASE + slot] == 0 &&
            g_WorldNumber >= WORLD_6 &&
            (g_FrameCounter & 0x03) == 0)
            (void)enemy_try_spawn_hammer(slot);
        /* SetHmrTmr (main.asm:7383-7391) is reached only from the
         * expired-timer path above.  Once MoveEnemySlowVert leaves Bowser at
         * or below $80, the ROM indexes PRandomRange with the object's raw
         * pseudo-random byte and overwrites EnemyFrameTimer ($078a+x). */
        if (set_timer_from_height && e->y >= 0x80)
            set_enemy_frame_timer(e, s_BowserPRandomRange[
                g_PseudoRandomBitReg[slot] & 0x03]);
        /* ChkFireB: flames run for worlds 1-5 and world 8; worlds 6-7
         * branch directly to BowserGfxHandler. */
            if ((g_WorldNumber < WORLD_6 || g_WorldNumber == WORLD_8) &&
                g_BowserFireBreathTimer == 0) {
                g_BowserFireBreathTimer = 0x20;
                s_Bowser.body_controls ^= 0x80;
                if ((s_Bowser.body_controls & 0x80) == 0) {
                    g_EnemyFrenzyQueue = BowserFlame;
                    g_BowserFireBreathTimer = next_bowser_flame_timer();
                    if (g_SecondaryHardMode)
                        g_BowserFireBreathTimer = (uint8_t)(
                            g_BowserFireBreathTimer - 0x10);
                }
            }
        }
    }
    run_bowser_half(e, slot, 1);
    rear = linked_duplicate_slot(slot);
    if (rear != 0xff) {
        EnemySlot *rear_object = &enemies[rear];
        uint16_t x = (uint16_t)e->x + ((e->moving_dir & 1) ? 0xf0 : 0x10);
        /* BowserGfxHandler (main.asm:7450-7457) stores only the low X byte
         * in the duplicate slot.  Its page byte is deliberately not part
         * of CopyFToR, so the rear half uses the duplicate's existing
         * Enemy_PageLoc during GetEnemyOffscreenBits. */
        rear_object->x = (uint8_t)x;
        rear_object->y = (uint8_t)(e->y + 8);
        rear_object->state = e->state;
        rear_object->moving_dir = e->moving_dir;
        /* BowserGfxHandler writes the rear object's ID before
         * ProcessBowserHalf (main.asm:7460-7467). */
        rear_object->id = Bowser;
        run_bowser_half(rear_object, rear, 2);
    }
    s_Bowser.gfx_flag = 0;
}

/* RunEnemyObjectsCore (main.asm:6077-6137).  This is the complete ID table,
 * including the 6502's $15 subtraction before JumpEngine.  The supported
 * normal and power-up/retainer boundaries are real below; specialized
 * graphics/motion handlers keep explicit route ownership until their later
 * roadmap clusters. */
const uint8_t s_EnemyRunDispatch[0x36] = {
    /* $00-$14: RunNormalEnemies */
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL, ENEMY_RUN_NORMAL,
    /* $15-$16 */
    ENEMY_RUN_BOWSER_FLAME, ENEMY_RUN_FIREWORKS,
    /* $17-$1a: NoRunCode */
    ENEMY_RUN_NO_CODE, ENEMY_RUN_NO_CODE, ENEMY_RUN_NO_CODE,
    ENEMY_RUN_NO_CODE,
    /* $1b-$22: RunFirebarObj */
    ENEMY_RUN_FIREBAR, ENEMY_RUN_FIREBAR, ENEMY_RUN_FIREBAR,
    ENEMY_RUN_FIREBAR, ENEMY_RUN_FIREBAR, ENEMY_RUN_FIREBAR,
    ENEMY_RUN_FIREBAR, ENEMY_RUN_FIREBAR,
    /* $23: NoRunCode; $24-$2a: RunLargePlatform */
    ENEMY_RUN_NO_CODE,
    ENEMY_RUN_LARGE_PLATFORM, ENEMY_RUN_LARGE_PLATFORM,
    ENEMY_RUN_LARGE_PLATFORM, ENEMY_RUN_LARGE_PLATFORM,
    ENEMY_RUN_LARGE_PLATFORM, ENEMY_RUN_LARGE_PLATFORM,
    ENEMY_RUN_LARGE_PLATFORM,
    /* $2b-$2c: RunSmallPlatform; $2d: RunBowser */
    ENEMY_RUN_SMALL_PLATFORM, ENEMY_RUN_SMALL_PLATFORM,
    ENEMY_RUN_BOWSER,
    /* $2e-$2f */
    ENEMY_RUN_POWERUP, ENEMY_RUN_VINE,
    /* $30-$35 */
    ENEMY_RUN_NO_CODE, ENEMY_RUN_STAR_FLAG, ENEMY_RUN_JUMPSPRING,
    ENEMY_RUN_NO_CODE, ENEMY_RUN_WARP_ZONE, ENEMY_RUN_RETAINER
};

/* RunRetainerObj (main.asm:6133-6136).  The constructor/state owner is
 * already live; the relative/offscreen writes and shared graphics consumer
 * occur at the original call boundary. */
void run_retainer_enemy(EnemySlot* e) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };

    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    /* RunRetainerObj has no movement or collision consumer: its only
     * remaining call is the shared EnemyGfxHandler, including the
     * world-selected princess/retainer table entry above. */
    draw_special_enemy(e, (uint8_t)(e - enemies));
}

/* RunNormalEnemies (main.asm:6139-6155), with the supported Goomba graphics
 * and block-buffer collision consumers.  Every stateful producer/consumer
 * boundary is kept in the original order; unsupported normal IDs retain the
 * same object RAM but defer their type-specific graphics/BG branches. */
void run_normal_enemy(EnemySlot* e, uint8_t slot) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };

    e->spr_attrib = 0;
    e->interval_timer = g_Timers[TIMER_ENEMY_INTERVAL_BASE + slot];

    /* GetEnemyOffscreenBits precedes RelativeEnemyPosition in the ROM. */
    e->offscreen_bits = enemy_offscreen_bits(e);
    SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
    g_Enemy_Rel_XPos = e->rel_x;

    if (e->id == GOOMBA_ID)
        draw_goomba(e, SpriteOffset_Enemy(slot) / 4);
    else if (e->id == Bloober || e->id == BulletBill_FrenzyVar ||
             e->id == GreyCheepCheep || e->id == RedCheepCheep ||
             e->id == Podoboo || e->id == PiranhaPlant ||
             e->id == HammerBro || e->id == Lakitu || e->id == Spiny ||
             e->id == FlyCheepCheepFrenzy)
        draw_special_enemy(e, slot);
    else
        draw_koopa_family(e, slot);

    enemy_graphics_offscreen_cleanup(e);
    enemy_get_bound_box(e);
    enemy_to_bg_collision_det(e);
    enemy_enemy_collision(e, slot);
    player_enemy_collision(e);

    if (g_TimerControl == 0)
        enemy_movement_dispatch(e);

    enemy_offscreen_bounds(e);
}
