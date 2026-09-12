#include <stdint.h>
#include <stddef.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "collision.h"
#include "level/level.h"
#include "screen/routine/colors.h"
#include "assets.h"

/* Enemy/player collision. */

/* OffscreenBoundsCheck (main.asm:8208-8268), Goomba subset.  Normal enemies
 * are retained in an extended horizontal window: 72 pixels beyond each
 * screen edge.  The object page/X pair is the state compared by the 6502;
 * this is not a framebuffer or fixed-coordinate visibility rule. */

static uint8_t s_EnemyBGCStateData[6];
static uint8_t demoted_speed[4];
static uint8_t KickedShellXSpdData[2];
static uint8_t KickedShellPtsData[3];
static uint8_t s_EnemySetBitsMask[7];
static uint8_t s_EnemyClearBitsMask[7];
static uint8_t bowser_identities[8];
static uint8_t player_pos_splat_data[2];

int Enemy_LoadCollideTables(void) {
    if (Assets_Copy("tables/enemy_bgc_state.bin", s_EnemyBGCStateData,
                    sizeof(s_EnemyBGCStateData)) ||
        Assets_Copy("tables/revived_x_spd.bin", demoted_speed,
                    sizeof(demoted_speed)) ||
        Assets_Copy("tables/kicked_shell_x_spd.bin", KickedShellXSpdData,
                    sizeof(KickedShellXSpdData)) ||
        Assets_Copy("tables/kicked_shell_pts.bin", KickedShellPtsData,
                    sizeof(KickedShellPtsData)) ||
        Assets_Copy("tables/set_bits_mask.bin", s_EnemySetBitsMask,
                    sizeof(s_EnemySetBitsMask)) ||
        Assets_Copy("tables/clear_bits_mask.bin", s_EnemyClearBitsMask,
                    sizeof(s_EnemyClearBitsMask)) ||
        Assets_Copy("tables/bowser_identities.bin", bowser_identities,
                    sizeof(bowser_identities)) ||
        Assets_Copy("tables/player_pos_splat.bin", player_pos_splat_data,
                    sizeof(player_pos_splat_data)))
        return -1;
    return 0;
}



void enemy_offscreen_bounds(EnemySlot* e) {
    uint8_t left_x = g_ScreenLeft_X_Pos;
    uint8_t left_page = g_ScreenLeft_PageLoc;
    uint8_t right_x;
    uint8_t right_page;
    uint8_t carry;
    uint8_t subtract;
    uint16_t sum;

    /* OffscreenBoundsCheck exits before either comparison for the flying
     * Cheep-Cheep object.  Hammer Bro/Piranha use the shortened left window
     * produced by the $38 pre-add; every other object uses the common $48
     * extension. */
    if (e->id == FlyingCheepCheep) return;

    /* OffscreenBoundsCheck (main.asm:8208-8254) deliberately carries the
     * flags from CPY through ADC/SBC.  Reproduce those 8-bit operations so a
     * boundary at a page wrap has the same N flag as the 6502. */
    if (e->id == HammerBro || e->id == PiranhaPlant) {
        sum = (uint16_t)left_x + 0x38 + 1;
        left_x = (uint8_t)sum;
        carry = (uint8_t)(sum > 0xff);
    } else {
        /* The second CPY is the last flag producer on the common path. */
        carry = (uint8_t)(e->id >= PiranhaPlant);
    }
    subtract = carry ? 0x48 : 0x49;
    carry = (uint8_t)(left_x >= subtract); /* SBC #$48, no-borrow flag */
    left_x = (uint8_t)(left_x - subtract);
    subtract = carry ? 0 : 1;
    carry = (uint8_t)(left_page >= subtract); /* SBC #$00 */
    left_page = (uint8_t)(left_page - subtract);

    right_x = g_ScreenRight_X_Pos;
    sum = (uint16_t)right_x + 0x48 + carry; /* ADC #$48 */
    right_x = (uint8_t)sum;
    right_page = (uint8_t)(g_ScreenRight_PageLoc + (sum > 0xff));

    /* CMP X followed by SBC page, then BMI, is the ROM's signed page test. */
    carry = (uint8_t)(e->x >= left_x);
    sum = (uint8_t)(e->page - left_page - (carry ? 0 : 1));
    if ((sum & 0x80) != 0) {
        erase_enemy(e);
        return;
    }
    carry = (uint8_t)(e->x >= right_x);
    sum = (uint8_t)(e->page - right_page - (carry ? 0 : 1));
    if ((sum & 0x80) == 0) {
        /* The ROM compares Enemy_State with HammerBro at this point, then
         * tests the still-live ID in Y for the remaining right-edge keeps. */
        if (e->state == HammerBro || e->id == PiranhaPlant ||
            e->id == FlagpoleFlagObject || e->id == StarFlagObject ||
            e->id == JumpspringObject) return;
        erase_enemy(e);
    }
}

/* DoEnemySideCheck/ChkForBump_HammerBroJ/RXSpd (main.asm:9885-9928).
 * The side probe uses BlockBuffer_X_Adder[$16/$17] = {0, $10} and
 * BlockBuffer_Y_Adder[$16/$17] = {$14, $14}.  Its caller is not the normal
 * enemy loop itself: EnemyToBGCollisionDet reaches it through
 * ChkUnderEnemy/LandEnemyProperly for a grounded normal Goomba. */
void check_enemy_side(EnemySlot* e) {
    LevelBlockBufferProbe probe;
    /* DoEnemySideCheck starts with $eb=$02 and Y=$16.  The first compare
     * therefore selects probe $16 for MovingDir=$02 (left side); after
     * DEC $eb/INY it selects $17 for MovingDir=$01 (right side). */
    uint8_t probe_index = e->moving_dir == 0x01 ? 0x17 : 0x16;

    /* DoEnemySideCheck exits before probing the status-bar rows. */
    if (e->y < 0x20) return;
    Level_BlockBufferCollision(&probe, e->page, e->x, e->y,
                               probe_index, 1);
    /* ChkForNonSolids classifies the raw BlockBufferCollision result.  The
     * ASM does not ask a second, derived solidity map here; that would lose
     * the coin/hidden-block exceptions and make the side result depend on a
     * renderer-owned cache. */
    if (probe.metatile == 0 || probe.metatile == 0x26 ||
        probe.metatile == 0xc2 || probe.metatile == 0xc3 ||
        probe.metatile == 0x5f || probe.metatile == 0x60) return;

    /* ChkForBump_HammerBroJ (main.asm:9910-9917) queues Sfx_Bump only
     * for an object outside the special misc slot and only when the
     * object-state high bit is set.  This is the shared producer reached
     * after a solid DoEnemySideCheck probe; it precedes RXSpd. */
    if (enemy_slot_index(e) != 5 && (e->state & 0x80) != 0)
        g_Square1SoundQueue = Sfx_Bump;

    /* RXSpd: two's-complement speed and invert the 1/2 direction encoding. */
    e->x_speed = (uint8_t)(0 - e->x_speed);
    e->moving_dir ^= 0x03;
}

/* EnemyBGCStateData (main.asm:9670) is indexed by the low state values used
 * by the normal Goomba path after LandEnemyProperly. */

/* ChkForBump_HammerBroJ/RXSpd is defined with the enemy collision helpers
 * below, but ChkForRedKoopa reaches it from EnemyToBGCollisionDet. */
void enemy_turn_around(EnemySlot* e);

/* ChkForRedKoopa/Chk2MSBSt (main.asm:9862-9879).  This is deliberately
 * separate from the landing path: an empty support probe still advances a
 * normal object into its falling state, while a normal-state red Koopa takes
 * the RXSpd direction-reversal branch. */
void enemy_apply_no_bg_state(EnemySlot* e) {
    if (e->id == RedKoopa && e->state == 0) {
        enemy_turn_around(e);
        return;
    }
    if (e->state & 0x80) {
        e->state |= 0x40;
    } else if (e->state < sizeof(s_EnemyBGCStateData)) {
        e->state = s_EnemyBGCStateData[e->state];
    }
    /* ChkForRedKoopa falls through SetD6Ste into DoEnemySideCheck;
     * this is a contiguous ASM tail, not a return boundary. */
    check_enemy_side(e);
}

/* EnemyLanding/InitVStf (main.asm:9944-9951).  The fractional Y dummy is
 * intentionally left alone: InitVStf clears only Enemy_Y_Speed and
 * Enemy_Y_MoveForce, while the landing routine snaps the low Y nibble. */
void enemy_landing(EnemySlot* e) {
    e->y_speed = 0;
    e->y_mf = 0;
    e->y = (uint8_t)((e->y & 0xf0) | 0x08);
}

/* ProcEnemyDirection/LandEnemyInitState (main.asm:9809-9859).  This is the
 * landing-time direction owner for a normal enemy state $01/$05.  The
 * direction test uses PlayerEnemyDiff's page/X sign, and an already-facing
 * object enters ChkForBump_HammerBroJ/RXSpd instead of merely being snapped
 * to the ground. */
void enemy_process_direction(EnemySlot *e) {
    uint8_t desired_direction;
    uint8_t enemy_left_of_player;

    if (e->id == GOOMBA_ID) {
        enemy_landing(e);
    } else {
        if (e->id == Spiny) {
            e->moving_dir = BTN_RIGHT;
            e->x_speed = 0x08;
            if ((g_FrameCounter & 0x07) == 0) {
                enemy_landing(e);
                goto settle;
            }
        }

        /* PlayerEnemyDiff is Enemy_X/Page minus Player_X/Page.  The 6502
         * starts with direction $01 and selects $02 only for an object to
         * the left of the player. */
        desired_direction = BTN_RIGHT;
        (void)enemy_player_diff(e, &enemy_left_of_player);
        if (enemy_left_of_player)
            desired_direction = BTN_LEFT;

        if (desired_direction != e->moving_dir) {
            enemy_landing(e);
        } else {
            /* ChkForBump_HammerBroJ queues the bump SFX for a d7 object;
             * Audio_SoundEngine consumes it after gameplay.  The Hammer Bro
             * jump path is only for Enemy_ID=$05.  All other supported callers use
             * RXSpd, whose object-owned fields are shared here. */
            if (e->id == HammerBro) {
                e->y_speed = 0xfa;
                e->state |= 0x01;
            } else {
                enemy_turn_around(e);
            }
            /* ProcEnemyDirection falls through LandEnemyInitState after
             * ChkForBump_HammerBroJ returns (main.asm:9843-9851).  The
             * RXSpd reversal therefore still snaps Y and clears the normal
             * falling state; returning here would leave the object in state
             * $01 for one extra movement pass. */
            enemy_landing(e);
        }
    }

settle:
    /* LandEnemyInitState clears a normal state and clears only d6 when d7
     * marks a shell/falling state. */
    if (e->state & 0x80)
        e->state &= (uint8_t)~0x40;
    else
        e->state = 0;
}

/* ChkUnderEnemy -> LandEnemyProperly/ChkForRedKoopa
 * (main.asm:9678-9818).  BlockBufferChk_Enemy uses X adder $08 (table index
 * $15), Y adder $18, and the block-buffer metatile rather than a
 * screen-coordinate substitute.  EnemyToBGCollisionDet owns the common
 * d5/Y/ID gates before this ordinary tail. */
uint8_t enemy_ground_side_check(EnemySlot* e) {
    LevelBlockBufferProbe probe;
    uint8_t metatile;
    uint8_t low_delta;

    metatile = Level_BlockBufferCollision(&probe, e->page, e->x, e->y,
                                           0x15, 0);
    /* HandleEToBGCollision (main.asm:9798-9820) consumes the raw $23 marker
     * written by PlayerHeadCollision.  The marker is distinct from ordinary
     * empty $00 so a Goomba above a newly-bumped block is defeated by the
     * shared ShellOrBlockDefeat owner before the block replacement pass. */
    if (metatile == 0x23) {
        Level_ClearBlockBufferCell(probe.buffer_column,
                                   (uint8_t)(probe.aligned_y >> 4));
        if (e->id < 0x15) {
            if (e->id == GOOMBA_ID) {
                /* KillEnemyAboveBlock calls ShellOrBlockDefeat, then stores
                 * $fc before the fall-through GiveOEPoints/ChkToStun path. */
                shell_or_block_defeat(e);
                e->y_speed = 0xfc;
            }
            /* GiveOEPoints loads control $01.  SetupFloateyNumber leaves
             * Enemy_Rel_XPos in A, which is the exact scratch consumed by
             * the subsequent ChkToStunEnemies comparisons. */
            setup_floaty_number(e, 0x01);
            check_to_stun_enemy(e, e->rel_x);
        } else {
            /* The $15+ branch reaches ChkToStunEnemies directly with
             * Enemy_ID still in A; it does not run GiveOEPoints. */
            check_to_stun_enemy(e, e->id);
        }
        return 0;
    }
    /* ChkForNonSolids -> NoEToBGCollision -> ChkForRedKoopa: an ordinary
     * blank, coin, or hidden block does not land the enemy, but it does still
     * update the object state before returning to RunNormalEnemies. */
    if (metatile == 0 || metatile == 0x26 ||
        metatile == 0xc2 ||
        metatile == 0xc3 || metatile == 0x5f || metatile == 0x60) {
        enemy_apply_no_bg_state(e);
        return 0;
    }

    /* LandEnemyProperly only falls through to DoEnemySideCheck when
     * (Y low nibble - $08) < $05. */
    low_delta = (uint8_t)((e->y & 0x0f) - 0x08);
    if (low_delta >= 0x05) {
        /* LandEnemyProperly branches to the same ChkForRedKoopa owner. */
        enemy_apply_no_bg_state(e);
        return 1;
    }

    if (e->state & 0x40) {
        enemy_landing(e);
        /* LandEnemyInitState (main.asm:9845-9859) continues after
         * EnemyLanding: a shell/falling object keeps d7 but clears d6,
         * while a non-shell object is returned to state zero. */
        if (e->state & 0x80)
            e->state &= (uint8_t)~0x40;
        else
            e->state = 0;
        return 1;
    }
    if (e->state & 0x80 || e->state == 0) {
        check_enemy_side(e);
        return 1;
    }
    if (e->state == 1 || e->state == 5) {
        /* ProcEnemyDirection sends Goombas straight to landing, while
         * RedKoopa and the other normal IDs perform the direction/RXSpd
         * branch before LandEnemyInitState. */
        enemy_process_direction(e);
        return 1;
    }
    if (e->state == 2) {
        /* ChkLandedEnemyState arms the normal revival interval for a
         * non-Spiny object before EnemyLanding. */
        e->interval_timer = 0x10;
        g_Timers[TIMER_ENEMY_INTERVAL_BASE + enemy_slot_index(e)] =
            e->interval_timer;
        e->state = 0x03;
        enemy_landing(e);
        return 1;
    }
    /* States $03/$04 leave LandEnemyProperly without a new landing write. */
    return 1;
}

/* HammerBroBGColl (main.asm:9980-10003).  Hammer Bro does not use
 * LandEnemyProperly's generic ChkForNonSolids/state table: any nonzero
 * ChkUnderEnemy result is a landing candidate, while the raw $23 marker
 * enters ShellOrBlockDefeat and sets the distinct $fc fall speed. */
void hammer_bro_bg_check(EnemySlot* e) {
    LevelBlockBufferProbe probe;
    uint8_t metatile;

    metatile = Level_BlockBufferCollision(
        &probe, e->page, e->x, e->y, 0x15, 0);

    if (metatile == 0) {
        e->state |= 0x01; /* NoUnderHammerBro */
        return;
    }
    if (metatile == 0x23) {
        shell_or_block_defeat(e); /* KillEnemyAboveBlock */
        e->y_speed = 0xfc;
        return;
    }
    if (g_Timers[TIMER_ENEMY_FRAME_BASE + enemy_slot_index(e)] != 0) {
        e->state |= 0x01; /* NoUnderHammerBro */
        return;
    }

    /* UnderHammerBro preserves d7 and d3, then lands and performs the same
     * two-sided block probe as DoEnemySideCheck. */
    e->state &= 0x88;
    enemy_landing(e);
    check_enemy_side(e);
}

/* EnemyJump (main.asm:9959-9975).  GreenParatroopaJump uses the same
 * BlockBufferChk_Enemy/$15 probe as normal enemies, but only when its current
 * vertical speed is falling; a solid result restarts its jump at $fd before
 * the shared horizontal side check. */
void enemy_jumping_bg_check(EnemySlot* e) {
    LevelBlockBufferProbe probe;
    uint8_t metatile;

    if ((uint8_t)(e->y + 0x3e) < 0x44) {
        check_enemy_side(e);
        return;
    }
    if ((uint8_t)(e->y_speed + 0x02) < 0x03) {
        check_enemy_side(e);
        return;
    }
    metatile = Level_BlockBufferCollision(&probe, e->page, e->x, e->y,
                                           0x15, 0);
    if (metatile == 0 || metatile == 0x26 || metatile == 0xc2 ||
        metatile == 0xc3 || metatile == 0x5f || metatile == 0x60) {
        check_enemy_side(e);
        return;
    }
    enemy_landing(e);
    e->y_speed = 0xfd;
    check_enemy_side(e);
}

/* EnemyToBGCollisionDet (main.asm:9676-9705) owns the common background
 * collision gates and the ID dispatch.  The normal enemy loop and the
 * mushroom/1-up ShroomM path both enter here.  Green Paratroopa and the star
 * power-up intentionally share EnemyJump only after their respective caller
 * paths have made the outer-gate decision; the star's direct caller remains
 * enemy_jumping_bg_check(). */
void enemy_to_bg_collision_det(EnemySlot *e) {
    uint8_t id;

    if ((e->state & 0x20) != 0) return;
    if ((uint8_t)(e->y + 0x3e) < 0x44) return;
    if (e->id == Spiny && e->y < 0x25) return;

    id = e->id;
    if (id == GreenParatroopaJump) {
        enemy_jumping_bg_check(e);
        return;
    }
    if (id == HammerBro) {
        hammer_bro_bg_check(e);
        return;
    }
    if (id < 0x07 || id == Spiny || id == PowerUpObject)
        (void)enemy_ground_side_check(e);
}

/* GetEnemyBoundBox/GetMaskedOffScrBits (main.asm:10098-10153), followed by
 * BoundingBoxCore/CheckRightScreenBBox (main.asm:10155-10231).  The mask and
 * four byte-sized coordinates are persistent per-slot object state; callers
 * must not reconstruct a box from a host-width world rectangle. */
void enemy_get_bound_box(EnemySlot* e) {
    uint8_t rel_x = (uint8_t)(e->x - g_ScreenLeft_X_Pos);
    uint8_t page_delta = (uint8_t)(e->page - g_ScreenLeft_PageLoc -
                                   (e->x < g_ScreenLeft_X_Pos));
    uint8_t left_of_screen = (uint8_t)((page_delta & 0x80) != 0 ||
                                       (page_delta == 0 && rel_x == 0));
    uint8_t mask = left_of_screen ? 0x44 : 0x48;
    uint8_t ctrl = e->bbox_ctrl;
    const uint8_t *data;

    e->offscreen_masked = (uint8_t)(mask & e->offscreen_bits);
    if (e->offscreen_masked != 0 || ctrl >= 12) {
        /* MoveBoundBoxOffscreen writes #$ff to all four coordinates.  The
         * ctrl guard is only the explicit unsupported-constructor boundary;
         * all supported Init* routines select one of the 12 ROM rows. */
        e->bbox_ul_x = 0xff;
        e->bbox_ul_y = 0xff;
        e->bbox_lr_x = 0xff;
        e->bbox_lr_y = 0xff;
        return;
    }

    data = s_BoundBoxCtrlData[ctrl];
    e->bbox_ul_x = (uint8_t)(e->rel_x + data[0]);
    e->bbox_lr_x = (uint8_t)(e->rel_x + data[2]);
    e->bbox_ul_y = (uint8_t)(e->rel_y + data[1]);
    e->bbox_lr_y = (uint8_t)(e->rel_y + data[3]);

    /* CheckRightScreenBBox uses the screen midpoint to decide which wrapped
     * side is being examined.  The compare is the 6502 CMP/SBC byte pair,
     * represented here as an unsigned 16-bit page/X comparison. */
    {
        uint16_t object_wx = ((uint16_t)e->page << 8) | e->x;
        uint16_t middle = (uint16_t)(((uint16_t)g_ScreenLeft_PageLoc << 8) |
                                     g_ScreenLeft_X_Pos) + 0x80;

        if (object_wx >= middle) {
            if ((e->bbox_lr_x & 0x80) == 0) {
                if ((e->bbox_ul_x & 0x80) == 0) e->bbox_ul_x = 0xff;
                e->bbox_lr_x = 0xff;
            }
        } else if ((e->bbox_ul_x & 0x80) != 0 &&
                   e->bbox_ul_x >= 0xa0) {
            if ((e->bbox_lr_x & 0x80) != 0) e->bbox_lr_x = 0x00;
            e->bbox_ul_x = 0x00;
        }
    }

}

CollisionBox enemy_collision_box(const EnemySlot *e) {
    CollisionBox box = {
        e->bbox_ul_x, e->bbox_ul_y, e->bbox_lr_x, e->bbox_lr_y
    };
    return box;
}

/* BoundingBoxCore (main.asm:10155-10191).  Coordinates intentionally remain
 * bytes: CheckRightScreenBBox and PlayerCollisionCore rely on 6502 wrap
 * behavior instead of converting an object into a host-width rectangle. */
void make_collision_box(uint8_t rel_x, uint8_t rel_y,
                               uint8_t bbox_ctrl, CollisionBox* box) {
    const uint8_t* data = s_BoundBoxCtrlData[bbox_ctrl];
    box->ul_x = (uint8_t)(rel_x + data[0]);
    box->lr_x = (uint8_t)(rel_x + data[2]);
    box->ul_y = (uint8_t)(rel_y + data[1]);
    box->lr_y = (uint8_t)(rel_y + data[3]);
}

/* One axis of PlayerCollisionCore (main.asm:10245-10286), with the first
 * box corresponding to the Y offset (the current enemy) and the second box
 * to the X offset (the player). */
uint8_t collision_axis(uint8_t first_ul, uint8_t first_lr,
                              uint8_t second_ul, uint8_t second_lr) {
    if (first_ul >= second_ul) {
        if (first_ul == second_ul) return 1;
        if (first_ul < second_lr || first_ul == second_lr) return 1;
        if (first_ul <= first_lr) return 0;
        return (uint8_t)(first_lr >= second_ul);
    }

    if (first_ul < second_lr) {
        if (second_lr < second_ul) return 1;
        return (uint8_t)(first_lr >= second_ul);
    }
    if (first_ul == second_lr) return 1;
    if (first_lr < first_ul) return 1;
    return (uint8_t)(first_lr >= second_ul);
}

uint8_t collision_boxes_overlap(const CollisionBox* enemy,
                                       const CollisionBox* player) {
    return collision_axis(enemy->ul_x, enemy->lr_x,
                          player->ul_x, player->lr_x) &&
           collision_axis(enemy->ul_y, enemy->lr_y,
                          player->ul_y, player->lr_y);
}

/* PlayerCollisionCore always consumes the player box produced by the
 * preceding PlayerCtrlRoutine/BoundingBoxCore pass.  It does not rebuild the
 * box from the current relative position while the enemy passes run. */
CollisionBox player_collision_box(void) {
    CollisionBox box = {
        g_Player_BoundingBox[0], g_Player_BoundingBox[1],
        g_Player_BoundingBox[2], g_Player_BoundingBox[3]
    };
    return box;
}

/* CheckPlayerVertical (main.asm:9104-9114).  The player offscreen byte is
 * produced by PlayerCtrlRoutine before GameEngine's enemy passes. */
uint8_t player_collision_vertical_ok(void) {
    if (g_Player_OffscreenBits >= 0xf0) return 0;
    /* CheckPlayerVertical (main.asm:9104-9114) only branches on carry at
     * its caller.  DEY/BNE for a high byte other than $01 leaves carry from
     * the preceding CMP #$f0 intact, so those values remain eligible here;
     * only the on-screen-page ($01) path applies the Y=$d0 cutoff. */
    if (g_Player_Y_HighPos == 0x01)
        return (uint8_t)(g_Player_Y_Position < 0xd0);
    return 1;
}

/* EnemyFacePlayer/PlayerEnemyDiff (main.asm:8750-8758, 9933-9940). */
uint8_t enemy_face_player(EnemySlot* e) {
    uint16_t enemy_position = (uint16_t)(((uint16_t)e->page << 8) | e->x);
    uint16_t player_position =
        (uint16_t)(((uint16_t)g_Player_PageLoc << 8) |
                   g_Player_X_Position);
    uint16_t difference = (uint16_t)(enemy_position - player_position);

    if ((int16_t)difference < 0) {
        e->moving_dir = BTN_LEFT;
        return 1;
    }
    e->moving_dir = BTN_RIGHT;
    return 0;
}

/* ForceInjury/KillPlayer (main.asm:8623-8658).  Sound and music writes are
 * retained as queue RAM only; the APU consumer is intentionally excluded.
 * The collision caller owns the InjuryTimer guard; the public wrapper is the
 * direct RunGameTimer -> TimeUpOn call and therefore deliberately has none. */
void force_injury(void) {
    if (g_PlayerStatus == PLAYER_STATUS_SMALL) {
        g_Player_X_Speed = 0;
        g_EventMusicQueue = DeathMusic;
        g_Player_Y_Speed = 0xfc;
        g_GameEngineSubroutine = 0x0b; /* PlayerDeath */
    } else {
        g_PlayerStatus = PLAYER_STATUS_SMALL;
        g_InjuryTimer = 0x08;
        g_Square1SoundQueue = Sfx_PipeDown_Injury;
        GetPlayerColors();
        g_GameEngineSubroutine = 0x0a; /* PlayerInjuryBlink */
    }
    g_Player_State = PLAYER_STATE_JUMP;
    g_TimerControl = 0xff;
    g_ScrollAmount = 0;
}

void injure_player(void) {
    if (g_InjuryTimer != 0) return;
    force_injury();
}

/* TimeUpOn stores PlayerStatus=$00 before calling ForceInjury.  Keep this
 * as the routine boundary rather than duplicating the death/injury writes in
 * the game timer consumer. */
void Enemy_ForceInjury(void) {
    force_injury();
}

/* HandleStompedShellE (main.asm:8723-8737).  The fractional and horizontal
 * fields remain untouched: the ROM writes state, stomp/interval timers,
 * score control, and the player's bounce speed here. */
void stomp_shellable_enemy(EnemySlot* e) {
    e->state = 0x04;
    s_StompChainCounter++;
    setup_floaty_number(e, (uint8_t)(s_StompChainCounter +
                                     g_StompTimer));
    g_StompTimer++;
    e->interval_timer = g_PrimaryHardMode ? 0x0b : 0x10;
    g_Timers[TIMER_ENEMY_INTERVAL_BASE + enemy_slot_index(e)] =
        e->interval_timer;
    g_Player_Y_Speed = 0xfc;
    g_Square1SoundQueue = Sfx_EnemyStomp;
}

/* EnemyStomped's StompedEnemyPtsData path (main.asm:8663-8703) is
 * different from HandleStompedShellE.  The shared SetStun consumer first
 * writes the transient state-$02/facing/speed fields, then the caller
 * restores the object's prior MovingDir, clears the vertical fixed-point
 * pair through InitVStf, and commits the d5 defeated state ($20). */
void stomp_special_enemy(EnemySlot* e, uint8_t points) {
    uint8_t saved_direction = e->moving_dir;

    g_Square1SoundQueue = Sfx_EnemyStomp;
    setup_floaty_number(e, points);
    check_to_stun_enemy(e, e->id);
    e->moving_dir = saved_direction;
    e->state = 0x20;
    e->y_speed = 0;
    e->y_mf = 0;
    e->x_speed = 0;
    g_Player_Y_Speed = 0xfd;
}

void demote_paratroopa(EnemySlot* e) {
    uint8_t facing_offset;

    /* ChkForDemoteKoopa (main.asm:8705-8718) turns either paratroopa into
     * its ordinary green/red identity, clears state, initializes only the
     * vertical speed/force, faces it toward the player, and uses the
     * DemotedKoopaXSpdData table. */
    e->id &= 0x01;
    e->state = 0;
    e->y_speed = 0;
    e->y_mf = 0;
    facing_offset = enemy_face_player(e);
    e->x_speed = demoted_speed[facing_offset];
    setup_floaty_number(e, 0x03); /* 400 points */
    g_Player_Y_Speed = 0xfc;
    g_Square1SoundQueue = Sfx_EnemyStomp;
}

/* HandleStompedShellE / EnemyStomped (main.asm:8723). */
void stomp_enemy(EnemySlot* e) {
    if (e->id == GreenParatroopaJump || e->id == RedParatroopa)
        demote_paratroopa(e);
    else if (e->id == FlyingCheepCheep ||
             e->id == BulletBill_FrenzyVar ||
             e->id == BulletBill_CannonVar || e->id == Podoboo)
        stomp_special_enemy(e, 0x02);
    else if (e->id == HammerBro)
        stomp_special_enemy(e, 0x06);
    else if (e->id == Lakitu)
        stomp_special_enemy(e, 0x05);
    else if (e->id == Bloober)
        stomp_special_enemy(e, 0x06);
    else if (e->id == TallEnemy || e->id == GreenParatroopaFly ||
             e->id == GreyCheepCheep || e->id == RedCheepCheep)
        demote_paratroopa(e);
    else
        stomp_shellable_enemy(e);
}

/* ChkToStunEnemies (main.asm:9733-9776).  The accumulator entering this
 * routine is normally Enemy_ID, but ShellOrBlockDefeat's Piranha branch
 * deliberately enters with the post-ADC Y value still in A. */
void check_to_stun_enemy(EnemySlot* e, uint8_t classification) {
    uint8_t enemy_left_of_player;

    /* IDs $09 and $0d-$10 are demoted; all other ranges take SetStun. */
    if (classification >= 0x09 && classification < 0x11 &&
        (classification < 0x0a || classification >= PiranhaPlant)) {
        e->id &= 0x01;
    }

    e->state = (uint8_t)((e->state & 0xf0) | 0x02);
    /* SetStun (main.asm:9752-9753) executes two DEC Enemy_Y_Position
     * instructions: the defeated object moves up by two pixels total. */
    e->y = (uint8_t)(e->y - 1);
    e->y = (uint8_t)(e->y - 1);
    e->y_speed = (e->id == Bloober || g_AreaType == AREA_TYPE_WATER)
        ? 0xff : 0xfd;

    /* PlayerEnemyDiff supplies the same signed page/X result used by the
     * 6502 branch.  Bullet-bill variants retain their direction even though
     * they still receive the selected residual speed. */
    (void)enemy_player_diff(e, &enemy_left_of_player);
    if (e->id != BulletBill_CannonVar && e->id != BulletBill_FrenzyVar) {
        e->moving_dir = enemy_left_of_player ? BTN_LEFT : BTN_RIGHT;
    }
    e->x_speed = enemy_left_of_player ? 0xf0 : 0x10;
}

/* ShellOrBlockDefeat (main.asm:8382-8411).  This is a shared object-state
 * routine: star kills, fireball kills, shell-chain reactions, and block
 * defeats all enter the same constructor-owned state/position/speed path. */
void shell_or_block_defeat(EnemySlot* e) {
    uint8_t classification = e->id;
    uint8_t points = 0x02;

    if (e->id == PiranhaPlant) {
        /* CMP #PiranhaPlant leaves carry set; the original ADC therefore
         * advances the vertical byte by $19, including its 6502 flag state. */
        e->y = (uint8_t)(e->y + 0x19);
        classification = e->y;
    }
    check_to_stun_enemy(e, classification);
    e->state = (uint8_t)((e->state & 0x1f) | 0x20);

    if (e->id == HammerBro) points = 0x06;
    else if (e->id == GOOMBA_ID) points = 0x01;
    setup_floaty_number(e, points);
    g_Square1SoundQueue = Sfx_EnemySmack;
}

/* StarInvincibleTimer reaches ShellOrBlockDefeat through the same routine as
 * the collision chain.  Keep the eligibility gate at PlayerEnemyCollision's
 * ASM boundary, then share all state and floaty writes here. */
void defeat_enemy_by_star(EnemySlot* e) {
    shell_or_block_defeat(e);
}


/* The state>=2 shell branch of HandlePECollisions (main.asm:8572-8592).
 * No Koopa constructor is live in this C subset yet, but the state-owned
 * dispatch is complete for a future shell object entering this boundary. */
void kick_enemy_shell(EnemySlot* e) {
    uint8_t facing_offset;
    uint8_t points;

    e->state |= 0x80;
    facing_offset = enemy_face_player(e);
    e->x_speed = KickedShellXSpdData[facing_offset];
    if (e->interval_timer < 0x03) {
        points = KickedShellPtsData[e->interval_timer];
    } else {
        points = (uint8_t)(s_StompChainCounter + 0x03);
    }
    setup_floaty_number(e, points);
    g_Square1SoundQueue = Sfx_EnemySmack;
}

/* ChkForPlayerInjury (main.asm:8597-8661).  Specialized normal enemies do
 * reach this branch even though they are not shellable Koopa identities. */
void check_player_injury(EnemySlot* e) {
    if ((g_Player_Y_Speed & 0x80) == 0 && g_Player_Y_Speed != 0) {
        stomp_enemy(e);
        return;
    }
    if (e->id >= Bloober &&
        (uint8_t)(g_Player_Y_Position + 0x0c) < e->y) {
        stomp_enemy(e);
        return;
    }
    if (g_StompTimer != 0) {
        stomp_enemy(e);
        return;
    }
    if (g_InjuryTimer != 0) return;

    if (g_Player_Rel_XPos < e->rel_x) {
        if (e->moving_dir == BTN_RIGHT) enemy_turn_around(e);
    } else if (e->moving_dir != BTN_RIGHT) {
        enemy_turn_around(e);
    }
    injure_player();
}

/* PlayerEnemyCollision (main.asm:8508-8758), with the live supported
 * object set.  It owns the per-slot d0 latch, power-up collection, star
 * defeat, Goomba stomp/bounce, injury, and death branches. */
void player_enemy_collision(EnemySlot* e) {
    CollisionBox enemy_box;
    CollisionBox player_box;

    if ((g_FrameCounter & 1) != 0) return;
    if (!player_collision_vertical_ok()) return;
    if (e->offscreen_masked != 0) return;
    if (g_GameEngineSubroutine != 0x08) return;
    if (e->state & 0x20) return;

    enemy_box = enemy_collision_box(e);
    player_box = player_collision_box();
    if (!collision_boxes_overlap(&enemy_box, &player_box)) {
        e->collision_bits &= 0xfe;
        return;
    }

    if (e->id == PowerUpObject) {
        handle_powerup_collision(e);
        return;
    }

    if (g_StarInvincibleTimer != 0) {
        /* EColl is an unconditional JMP ShellOrBlockDefeat in the ROM
         * (main.asm:8532-8540).  The classification/demotion belongs to
         * ChkToStunEnemies, not to a C-side identity filter. */
        defeat_enemy_by_star(e);
        return;
    }

    if (e->collision_bits & 0x01) return;
    e->collision_bits |= 0x01;

    if (e->id == BulletBill_CannonVar) {
        check_player_injury(e);
        return;
    }
    if (e->id == Spiny || e->id == PiranhaPlant || e->id == Podoboo ||
        e->id >= 0x15 || g_AreaType == AREA_TYPE_WATER) {
        injure_player();
        return;
    }
    if ((e->state & 0x80) || (e->state & 0x07) < 0x02) {
        check_player_injury(e);
        return;
    }
    if (e->id == GOOMBA_ID) return;
    /* HandlePECollisions kicks every non-Goomba object in a shell state;
     * ProcEnemyCollisions owns enemy-enemy shell reactions at its separate
     * pair-collision boundary. */
    kick_enemy_shell(e);
}

/* PlayerCollisionCore as reached by PlayerHammerCollision (main.asm:
 * 8415-8447).  Collision.c owns the hammer's odd-frame/timer/offscreen
 * gates; this helper keeps the ROM BoundBoxCtrlData and byte-wrapped overlap
 * routine in the same owner as the ordinary enemy collision path. */
uint8_t Enemy_CheckHammerCollision(uint8_t bbox_ul_x, uint8_t bbox_ul_y,
                                   uint8_t bbox_lr_x, uint8_t bbox_lr_y) {
    CollisionBox hammer_box;
    CollisionBox player_box;

    hammer_box.ul_x = bbox_ul_x;
    hammer_box.ul_y = bbox_ul_y;
    hammer_box.lr_x = bbox_lr_x;
    hammer_box.lr_y = bbox_lr_y;
    player_box = player_collision_box();
    return collision_boxes_overlap(&hammer_box, &player_box);
}

/* InjurePlayer is intentionally kept behind its existing InjuryTimer guard.
 * The hammer path only requests this gameplay transition; Audio_SoundEngine
 * consumes the corresponding queue on the same NMI. */
void Enemy_HammerInjury(void) {
    injure_player();
}


/* EnemyTurnAround/RXSpd (main.asm:8912-8936).  The ID tests are part of the
 * collision reaction, not a screen-position heuristic: only Spiny, jumping
 * green paratroopa, and IDs below $07 turn, with the three explicit IDs
 * excluded first. */
void enemy_turn_around(EnemySlot* e) {
    if (e->id == PiranhaPlant || e->id == Lakitu || e->id == HammerBro)
        return;
    if (e->id == Spiny || e->id == GreenParatroopaJump || e->id < 0x07) {
        e->x_speed = (uint8_t)(0 - e->x_speed);
        e->moving_dir ^= 0x03;
    }
}

/* ProcEnemyCollisions (main.asm:8853-8910).  State >= $06 is the shell/
 * alternate-state branch.  ShellChainCounter belongs to the shell slot at
 * $0125+x; it is never inferred from an OAM record or a framebuffer pixel. */
void proc_enemy_collisions(EnemySlot* current, EnemySlot* other) {
    if ((current->state | other->state) & 0x20) return;

    if (current->state >= 0x06) {
        if (current->id == HammerBro) return;
        if (other->state & 0x80) {
            setup_floaty_number(current, 0x06);
            shell_or_block_defeat(current);
        }
        shell_or_block_defeat(other);
        setup_floaty_number(other,
                            (uint8_t)(current->shell_chain_counter + 0x04));
        current->shell_chain_counter++;
        return;
    }

    if (other->state >= 0x06) {
        if (other->id == HammerBro) return;
        shell_or_block_defeat(current);
        setup_floaty_number(current,
                            (uint8_t)(other->shell_chain_counter + 0x04));
        other->shell_chain_counter++;
        return;
    }

    enemy_turn_around(other);
    enemy_turn_around(current);
}

/* EnemiesCollision (main.asm:8780-8848).  This translates the exact
 * cadence/eligibility/pair traversal, persistent latches, and the immediate
 * ProcEnemyCollisions call at the same collision boundary. */
void enemy_enemy_collision(EnemySlot* current, uint8_t current_slot) {
    CollisionBox current_box;
    int second_slot;
    uint8_t bit_mask;

    /* EnemiesCollision runs on odd FrameCounter values, skips water, and
     * excludes the specialized IDs exactly before its pair loop. */
    if ((g_FrameCounter & 0x01) == 0 || g_AreaType == AREA_TYPE_WATER)
        return;
    if (current->id >= 0x15 || current->id == Lakitu ||
        current->id == PiranhaPlant || current->offscreen_masked != 0)
        return;
    if (current_slot >= 7) return;

    current_box = enemy_collision_box(current);
    bit_mask = s_EnemySetBitsMask[current_slot];
    for (second_slot = (int)current_slot - 1;
         second_slot >= 0; second_slot--) {
        EnemySlot* other = &enemies[second_slot];
        CollisionBox other_box;

        if (other->flag == 0 || other->id >= 0x15 ||
            other->id == Lakitu || other->id == PiranhaPlant ||
            other->offscreen_masked != 0)
            continue;

        other_box = enemy_collision_box(other);
        if (!collision_boxes_overlap(&other_box, &current_box)) {
            other->collision_bits &= s_EnemyClearBitsMask[current_slot];
            continue;
        }

        /* ProcEnemyCollisions is entered immediately when either state has
         * d7, before consulting the per-pair latch. */
        if ((current->state | other->state) & 0x80) {
            proc_enemy_collisions(current, other);
            continue;
        }
        if (other->collision_bits & bit_mask) continue;
        other->collision_bits |= bit_mask;
        proc_enemy_collisions(current, other);
    }
}

SprObjectView fireball_object(FireballSlot* f) {
    SprObjectView object = {
        &f->page, &f->x, &f->y_high, &f->y, &f->x_speed, &f->x_mf,
        &f->y_speed, &f->y_mf, &f->y_dummy, &f->rel_x, &f->rel_y,
        &f->offscreen_bits
    };
    return object;
}

/* ChkForNonSolids (main.asm:10012-10024). */
uint8_t fireball_metatile_is_nonsolid(uint8_t metatile) {
    return metatile == 0x26 || metatile == 0xc2 || metatile == 0xc3 ||
           metatile == 0x5f || metatile == 0x60;
}

/* FireballBGCollision (main.asm:10026-10058).  The probe is the shared
 * BlockBufferChk_FBall index $1a, so the raw metatile and aligned row come
 * from the same $0500/$05d0 producer used by PlayerBGCollision. */
void fireball_bg_collision(FireballSlot* f) {
    LevelBlockBufferProbe probe;
    uint8_t metatile;

    if (f->y < 0x18) {
        f->bouncing = 0;
        return;
    }

    metatile = Level_BlockBufferCollision(&probe, f->page, f->x, f->y,
                                          0x1a, 0);
    if (metatile == 0 || fireball_metatile_is_nonsolid(metatile)) {
        f->bouncing = 0;
        return;
    }

    if ((f->y_speed & 0x80) != 0 || f->bouncing != 0) {
        f->state = 0x80;
        g_Square1SoundQueue = Sfx_Bump;
        return;
    }

    f->y_speed = 0xfd;
    f->bouncing = 1;
    f->y &= 0xf8;
}

/* HandleEnemyFBallCol's supported ordinary-enemy branch
 * (main.asm:8328-8411).  The ROM deliberately leaves Buzzy Beetles
 * fireproof, ignores Bullet Bill/Podoboo and all IDs >= $15 here, and sends
 * the remaining IDs through ShellOrBlockDefeat. */
void defeat_enemy_by_fireball(EnemySlot* e) {
    SprObject_GetRelativePosition(&(SprObjectView){
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    g_Enemy_Rel_XPos = e->rel_x;
    shell_or_block_defeat(e);
}

uint8_t fireball_defeats_enemy(const EnemySlot *e) {
    if (e->id == BuzzyBeetle || e->id == BulletBill_FrenzyVar ||
        e->id == Podoboo || e->id >= 0x15)
        return 0;
    return 1;
}

/* HandleEnemyFBallCol/HurtBowser (main.asm:8330-8378).  A Bowser duplicate
 * carries the owner's slot in Enemy_Flag d7..d0; the ROM resolves that link
 * before changing BowserHitPoints, but keeps the pre-resolved relative X
 * used by SetupFloateyNumber.  The singleton state and the owner slot are
 * therefore both explicit inputs to this branch. */
EnemySlot *fireball_bowser_target(EnemySlot *hit) {
    EnemySlot *target = hit;

    if ((hit->flag & 0x80) != 0) {
        uint8_t owner_slot = hit->flag & 0x0f;
        if (owner_slot < ENEMY_ALLOC_COUNT &&
            enemies[owner_slot].id == Bowser)
            target = &enemies[owner_slot];
    }
    return target->id == Bowser ? target : NULL;
}

void hurt_bowser_by_fireball(EnemySlot *hit, EnemySlot *bowser) {
    uint8_t hit_rel_x;

    SprObject_GetRelativePosition(&(SprObjectView){
        &hit->page, &hit->x, &hit->y_high, &hit->y, &hit->x_speed,
        &hit->x_mf, &hit->y_speed, &hit->y_mf, &hit->y_dummy,
        &hit->rel_x, &hit->rel_y, &hit->offscreen_bits
    }, g_ScreenLeft_X_Pos);
    hit_rel_x = hit->rel_x;

    /* HurtBowser begins with an unconditional DEC BowserHitPoints
     * (main.asm:8351-8353).  It is an 8-bit RAM counter, not a saturating
     * host value: the post-defeat duplicate hit wraps $00 to $ff and the
     * following BNE exits before changing the target object. */
    s_Bowser.hit_points--;
    if (s_Bowser.hit_points != 0)
        return;

    /* InitVStf clears only the vertical speed/force pair; HurtBowser then
     * stores zero in X speed and writes the defeated vertical speed. */
    bowser->y_speed = 0;
    bowser->y_mf = 0;
    bowser->x_speed = 0;
    g_EnemyFrenzyBuffer = 0;
    bowser->y_speed = 0xfe;
    bowser->id = bowser_identities[g_WorldNumber];
    bowser->state = (uint8_t)(0x20 | (g_WorldNumber < 0x03 ? 0x03 : 0x00));
    g_Square2SoundQueue = Sfx_BowserFall;

    /* HurtBowser restores X from $01 before falling through to
     * EnemySmackScore (main.asm:8376-8407).  SetupFloateyNumber therefore
     * owns the hit/duplicate slot, while its X coordinate still comes from
     * the relative-position scratch computed before Bowser is resolved. */
    setup_floaty_number(hit, 0x09);
    hit->floaty_x = hit_rel_x;
    /* EnemySmackScore (main.asm:8406-8409) queues the square-1 smack after
     * both the Bowser-fall queue and the 5000-point floatey setup. */
    g_Square1SoundQueue = Sfx_EnemySmack;
}

/* FireballEnemyCollision (main.asm:8266-8326).  The candidate selector keeps
 * the ROM's platform gap ($24-$2a) and Goomba defeated-state check.  The
 * collision loop intentionally visits all five regular enemy slots even
 * after setting the fireball's d7 explosion state, matching the original
 * loop's lack of a second state test. */
void fireball_enemy_collision(FireballSlot* f, uint8_t slot) {
    CollisionBox fireball_box;
    int enemy_index;

    if (f->state == 0 || (f->state & 0x80) != 0) return;
    if ((g_FrameCounter & 1) != 0) return;

    make_collision_box(f->rel_x, f->rel_y, f->bbox_ctrl, &fireball_box);
    for (enemy_index = ENEMY_ALLOC_COUNT - 1; enemy_index >= 0;
         enemy_index--) {
        EnemySlot* e = &enemies[enemy_index];
        CollisionBox enemy_box;

        if (e->flag == 0 || (e->state & 0x20) != 0) continue;
        if (e->id >= 0x24 && e->id < 0x2b) continue;
        if (e->id == GOOMBA_ID && e->state >= 0x02) continue;
        if (e->offscreen_masked != 0) continue;

        /* SprObjectCollisionCore reads the enemy's persistent
         * BoundingBox_* bytes produced by the preceding enemy pass
         * (main.asm:6139-6155, 8266-8311).  FireballObjCore refreshes only
         * the fireball box before entering FireballEnemyCollision; rebuilding
         * this box from the current Enemy_Rel_* would observe one frame too
         * early and bypass the ROM's object-call ordering. */
        enemy_box = enemy_collision_box(e);
        if (collision_boxes_overlap(&fireball_box, &enemy_box)) {
            EnemySlot *bowser = fireball_bowser_target(e);
            f->state = 0x80;
            if (bowser != NULL)
                hurt_bowser_by_fireball(e, bowser);
            else if (fireball_defeats_enemy(e))
                defeat_enemy_by_fireball(e);
        }
    }
    (void)slot;
}

/* Platform object cluster (main.asm:6077-6265, 7847-8230,
 * 8939-9190, 10651-10732, 11665-11760).  These routines keep platform
 * positions, aliases, collision flags, paired balance state, and movement
 * displacement in the owning EnemySlot.  They do not derive a platform from
 * an OAM coordinate or from the current framebuffer. */

SprObjectView platform_object(EnemySlot *e) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };
    return object;
}

void platform_write_bound_box(EnemySlot *e) {
    const uint8_t *data = s_BoundBoxCtrlData[e->bbox_ctrl];
    uint16_t object_wx;
    uint16_t middle;

    e->bbox_ul_x = (uint8_t)(e->rel_x + data[0]);
    e->bbox_lr_x = (uint8_t)(e->rel_x + data[2]);
    e->bbox_ul_y = (uint8_t)(e->rel_y + data[1]);
    e->bbox_lr_y = (uint8_t)(e->rel_y + data[3]);

    /* This is CheckRightScreenBBox, kept on the same object-owned four-byte
     * coordinate record as GetEnemyBoundBox. */
    object_wx = (uint16_t)(((uint16_t)e->page << 8) | e->x);
    middle = (uint16_t)(((uint16_t)g_ScreenLeft_PageLoc << 8) |
                        g_ScreenLeft_X_Pos) + 0x80;
    if (object_wx >= middle) {
        if ((e->bbox_lr_x & 0x80) == 0) {
            if ((e->bbox_ul_x & 0x80) == 0) e->bbox_ul_x = 0xff;
            e->bbox_lr_x = 0xff;
        }
    } else if ((e->bbox_ul_x & 0x80) != 0 &&
               e->bbox_ul_x >= 0xa0) {
        if ((e->bbox_lr_x & 0x80) != 0) e->bbox_lr_x = 0x00;
        e->bbox_ul_x = 0x00;
    }
}

void platform_get_bound_box(EnemySlot *e, uint8_t small) {
    SprObjectView object = platform_object(e);
    SprScreenEdges edges;
    uint8_t raw_x_bits;
    uint8_t move_box_offscreen;
    uint8_t mask;
    uint8_t x_difference;
    uint8_t page_difference;
    uint8_t borrow;

    SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                             g_ScreenLeft_X_Pos);
    if (small) {
        /* GetMaskedOffScrBits selects $04 at/beyond the left edge and $08
         * to its right, using the 6502 page/X subtraction rather than a
         * host-width world-coordinate comparison. */
        borrow = (uint8_t)(e->x < g_ScreenLeft_X_Pos);
        x_difference = (uint8_t)(e->x - g_ScreenLeft_X_Pos);
        page_difference = (uint8_t)(e->page - g_ScreenLeft_PageLoc - borrow);
        mask = ((page_difference & 0x80) != 0 ||
                (page_difference == 0 && x_difference == 0)) ?
            0x04 : 0x08;
        e->offscreen_masked = (uint8_t)(e->offscreen_bits & mask);
        move_box_offscreen = e->offscreen_masked != 0;
    } else {
        /* LargePlatformBoundBox calls GetXOffscreenBits on the enemy
         * object (the assembly's X+1 alias), and only $fe/$ff means the
         * complete bounding box is offscreen.  Unlike
         * GetMaskedOffScrBits, LargePlatformBoundBox does not store this
         * result in EnemyOffscrBitsMasked,x; that physical byte remains
         * owned by its previous CheckpointEnemyID/normal-object writer
         * (main.asm:10132-10141, 5073-5083). */
        raw_x_bits = SprObject_GetRawXOffscreenBits(&object, &edges);
        move_box_offscreen = (raw_x_bits >= 0xfe);
    }

    if (move_box_offscreen || e->bbox_ctrl >= 12) {
        e->bbox_ul_x = 0xff;
        e->bbox_ul_y = 0xff;
        e->bbox_lr_x = 0xff;
        e->bbox_lr_y = 0xff;
        return;
    }
    platform_write_bound_box(e);
}

/* PositionPlayerOnVPlat (main.asm:9077-9098) keeps the 6502's byte
 * subtraction visible.  The carry from SEC/SBC #$20 is consumed by the
 * following SBC #$00, so a platform below the top 32 pixels moves the player
 * into the preceding high-Y page.  A widened host integer followed by a
 * wrapped cast loses that borrow and leaves Player_Y_HighPos wrong. */
void position_player_on_vplat(const EnemySlot *platform,
                                     uint8_t platform_y) {
    uint8_t no_borrow;

    if (g_GameEngineSubroutine == 0x0b || platform->y_high != 0x01)
        return;

    no_borrow = (uint8_t)(platform_y >= 0x20);
    g_Player_Y_Position = (uint8_t)(platform_y - 0x20);
    g_Player_Y_HighPos = (uint8_t)(platform->y_high -
                                   (no_borrow ? 0 : 1));
    g_Player_Y_Speed = 0;
    g_Player_Y_MoveForce = 0;
}

/* PositionPlayerOnS_Plat (main.asm:9072-9082) uses the collision flag as a
 * one-based index into PlayerPosSPlatData-1: the upper box adds $80 and the
 * lower box adds $00.  The table is kept as routine state rather than
 * inferring a platform location from sprites or rendered pixels. */
void position_player_on_splat(const EnemySlot *platform,
                                     uint8_t collision_flag) {
    uint8_t table_index;

    if (collision_flag == 0 || collision_flag > 2)
        return;
    table_index = (uint8_t)(collision_flag - 1);
    position_player_on_vplat(
        platform, (uint8_t)(platform->y + player_pos_splat_data[table_index]));
}

/* PositionPlayerOnHPlat (main.asm:8123-8158) preserves the carry produced by
 * the player's byte-sized X addition while updating Player_PageLoc.  The
 * signed direction test is on the raw displacement byte, and SBC #$00 uses
 * that same carry for a negative displacement.  A host signed 16-bit sum
 * does not preserve this distinction at an unsigned page boundary. */
void position_player_on_hplat(uint8_t displacement) {
    uint16_t x_sum = (uint16_t)g_Player_X_Position + displacement;
    uint8_t position_carry = (uint8_t)(x_sum >> 8);

    g_Player_X_Position = (uint8_t)x_sum;
    if ((displacement & 0x80) != 0)
        g_Player_PageLoc = (uint8_t)(g_Player_PageLoc -
                                     (position_carry ? 0 : 1));
    else
        g_Player_PageLoc = (uint8_t)(g_Player_PageLoc + position_carry);
    g_Platform_X_Scroll = displacement;
}

uint8_t platform_collision_box(EnemySlot *owner,
                                      EnemySlot *surface,
                                      uint8_t small, uint8_t box_index) {
    CollisionBox platform_box = enemy_collision_box(surface);
    CollisionBox player_box;
    uint8_t difference;
    uint8_t side_collision = 0x01;

    /* SmallPlatformCollision advances the shared EnemyBoundingBoxCoord Y
     * pair itself between its two passes (main.asm:8996-9004).  The caller
     * owns that persistent adjustment; adding $80 to a local copy here would
     * make the collision test look right while leaving FireballEnemyCollision
     * with the wrong box on the following object pass. */
    (void)small;
    (void)box_index;
    player_box = player_collision_box();
    if (!collision_boxes_overlap(&platform_box, &player_box)) return 0;

    /* ProcLPlatCollisions (main.asm:9016-9068): first subtract the player's
     * top from the platform's bottom to handle an upward jump at the
     * underside, then subtract the platform's top from the player's bottom
     * for the six-pixel top window. */
    difference = (uint8_t)(platform_box.lr_y - player_box.ul_y);
    if (difference < 0x04 && (g_Player_Y_Speed & 0x80) != 0)
        g_Player_Y_Speed = 0x01;

    difference = (uint8_t)(player_box.lr_y - platform_box.ul_y);
    if (difference < 0x06 && (g_Player_Y_Speed & 0x80) == 0) {
        owner->platform_collision_flag = small ? box_index :
            enemy_slot_index(surface);
        g_Player_State = PLAYER_STATE_GROUND;
        return 1;
    }

    /* PlatformSideCollisions retains the original left/right byte tests and
     * hands the actual player RAM mutation to ImpedePlayerMove. */
    difference = (uint8_t)(player_box.lr_x - platform_box.ul_x);
    if (difference >= 0x08) {
        side_collision = 0x02;
        /* PlatformSideCollisions (main.asm:9050-9063) enters this
         * subtraction with CLC, so 6502 SBC consumes a clear carry and
         * subtracts one additional byte compared with SEC/SBC. */
        difference = (uint8_t)(platform_box.lr_x - player_box.ul_x - 1);
        if (difference >= 0x09) return 1;
    }
    /* ProcLPlatCollisions passes its $00 left/right contact code (1/2), not
     * Player_MovingDir, to ImpedePlayerMove (main.asm:9050-9066). */
    Collision_ImpededByPlatform(side_collision);
    return 1;
}

void platform_collision(EnemySlot *e, uint8_t small) {
    EnemySlot *surface = e;
    uint8_t box;

    /* LargePlatformCollision initializes PlatformCollisionFlag before its
     * TimerControl/CheckPlayerVertical exits; otherwise a prior zero remains
     * consumable by BalancePlatform/VerticalPlatform.  SmallPlatformCollision
     * performs its zeroing after the master-timer gate (main.asm:8941-8950,
     * 8975-8985). */
    if (!small) e->platform_collision_flag = 0xff;
    if (g_TimerControl != 0) return;
    if (small) e->platform_collision_flag = 0x00;
    if (!player_collision_vertical_ok()) return;

    if (!small && e->id == ENTITY_BAL_PLATFORM) {
        if (e->state & 0x80) return;
        if (e->state >= ENEMY_SLOT_COUNT || !enemies[e->state].flag)
            return;
        surface = &enemies[e->state];
    }
    if (!small) {
        /* LargePlatformCollision (main.asm:7847-7890) calls
         * ChkForPlayerC_LargeP once with X=Enemy_State for a balance
         * platform, then falls through into that label after the subroutine
         * returns.  ExLPC restores X=ObjectOffset, so the body runs a second
         * time for the current platform.  Both passes consume the persistent
         * boxes prepared by LargePlatformBoundBox; only the current-platform
         * pass may commit ProcLPlatCollisions to this object's flag. */
        if (e->id == ENTITY_BAL_PLATFORM) {
            (void)platform_collision_box(e, surface, 0, 0);
        }
        {
            (void)platform_collision_box(e, e, 0, 0);
        }
        return;
    }

    /* SmallPlatformCollision tests raw Enemy_OffscreenBits bit $02; the
     * masked byte is only the bounding-box erase condition. */
    if ((surface->offscreen_bits & 0x02) != 0) return;
    platform_get_bound_box(surface, 1);

    /* SmallPlatformCollision starts with counter 2, tries the upper box,
     * then adds $80 and tries the lower box. */
    for (box = 2; box != 0; box--) {
        /* The loop's MoveBoundBox branch is a write to the persistent
         * EnemyBoundingBoxCoord record, not merely a local Y offset.  This
         * is the state later read by FireballEnemyCollision and by the next
         * object pass. */
        if (surface->bbox_ul_y < 0x20) {
            surface->bbox_ul_y = (uint8_t)(surface->bbox_ul_y + 0x80);
            surface->bbox_lr_y = (uint8_t)(surface->bbox_lr_y + 0x80);
            continue;
        }
        if (platform_collision_box(e, surface, 1, box))
            break;
        surface->bbox_ul_y = (uint8_t)(surface->bbox_ul_y + 0x80);
        surface->bbox_lr_y = (uint8_t)(surface->bbox_lr_y + 0x80);
    }
}

void firebar_player_collision(const EnemySlot* e, uint8_t x,
                                     uint8_t y) {
    uint8_t player_y;

    if (g_StarInvincibleTimer != 0 || g_TimerControl != 0 ||
        !player_collision_vertical_ok() || x >= 0xf0)
        return;
    player_y = g_Player_Y_Position;
    if (g_PlayerSize == PLAYER_SIZE_SMALL || g_CrouchingFlag != 0)
        player_y = (uint8_t)(player_y + 0x18);
    if (byte_abs_delta(y, player_y) >= 8 ||
        byte_abs_delta(x, (uint8_t)(g_Player_Rel_XPos + 4)) >= 8)
        return;
    (void)e;
    injure_player();
}
