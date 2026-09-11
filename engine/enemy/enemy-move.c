#include <stdint.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "assets.h"

/* Enemy movement. */

/* MoveEnemyHorizontally (main.asm:4527): fixed point X movement */

static uint8_t s_XSpeedAdderData[4];
static uint8_t s_RevivedXSpeed[4];
static uint8_t throw_timer[2];
static uint8_t jump_length[2];
static uint8_t s_LakituDiffAdjust[3];
uint8_t s_PRDiffAdjustData[3][4];
static uint8_t s_FirebarPosLookup[99];
static uint8_t s_FirebarMirrorData[4];
static uint8_t s_FirebarTableOffsets[12];

int Enemy_LoadMoveTables(void) {
    if (Assets_Copy("tables/x_speed_adder.bin", s_XSpeedAdderData,
                    sizeof(s_XSpeedAdderData)) ||
        Assets_Copy("tables/revived_x_spd.bin", s_RevivedXSpeed,
                    sizeof(s_RevivedXSpeed)) ||
        Assets_Copy("tables/hammer_throw_timer.bin", throw_timer,
                    sizeof(throw_timer)) ||
        Assets_Copy("tables/hammer_bro_jump_l.bin", jump_length,
                    sizeof(jump_length)) ||
        Assets_Copy("tables/lakitu_diff_adj.bin", s_LakituDiffAdjust,
                    sizeof(s_LakituDiffAdjust)) ||
        Assets_Copy("tables/pr_diff_adjust.bin", s_PRDiffAdjustData,
                    sizeof(s_PRDiffAdjustData)) ||
        Assets_Copy("tables/firebar_pos_lookup.bin", s_FirebarPosLookup,
                    sizeof(s_FirebarPosLookup)) ||
        Assets_Copy("tables/firebar_mirror.bin", s_FirebarMirrorData,
                    sizeof(s_FirebarMirrorData)) ||
        Assets_Copy("tables/firebar_tbl_offsets.bin", s_FirebarTableOffsets,
                    sizeof(s_FirebarTableOffsets)))
        return -1;
    return 0;
}

uint8_t move_enemy_x(EnemySlot* e) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };
    return SprObject_MoveHorizontally(&object);
}

/* PlayerEnemyDiff (main.asm:9933-9940) returns the low byte of the
 * page/X subtraction while its sign branch observes the high-byte result.
 * Keep the final no-borrow carry as well: BulletBillHandler immediately
 * performs ADC #$28 on the low byte after this routine returns. */
uint8_t enemy_player_diff_with_carry(const EnemySlot* e,
                                            uint8_t* negative,
                                            uint8_t* no_borrow) {
    uint16_t enemy_position = (uint16_t)(((uint16_t)e->page << 8) | e->x);
    uint16_t player_position =
        (uint16_t)(((uint16_t)g_Player_PageLoc << 8) | g_Player_X_Position);
    uint16_t difference = (uint16_t)(enemy_position - player_position);

    *negative = (uint8_t)((difference & 0x8000) != 0);
    *no_borrow = (uint8_t)(enemy_position >= player_position);
    return (uint8_t)difference;
}

uint8_t enemy_player_diff(const EnemySlot* e, uint8_t* negative) {
    uint8_t no_borrow;

    return enemy_player_diff_with_carry(e, negative, &no_borrow);
}

void move_enemy_world_x(EnemySlot* e, uint8_t amount, uint8_t right) {
    if (right)
        pos_add_u8(&e->page, &e->x, amount);
    else
        pos_sub_u8(&e->page, &e->x, amount);
}

void move_enemy_gravity(EnemySlot* e, uint8_t downward,
                               uint8_t upward, uint8_t max_speed) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };

    SprObject_ImposeGravity(&object, downward, upward, max_speed, 0);
}

/* MoveDefeatedBloober/MoveEnemySlowVert (main.asm:4594-4604, 6617).
 * BlooperMoveCounter aliases Enemy_Y_Speed=$a0, so the defeated branch
 * intentionally reuses that byte as vertical speed. */
void move_slow_special_enemy(EnemySlot* e) {
    move_enemy_gravity(e, 0x0f, 0x00, 0x02);
}

/* ProcSwimmingB and MoveBloober (main.asm:6565-6679).  The two movement
 * bytes are the original aliases BlooperMoveSpeed=$58 and
 * BlooperMoveCounter=$a0; Y movement uses Enemy_Y_MoveForce=$0434. */
void move_bloober(EnemySlot* e) {
    uint8_t slot = enemy_slot_index(e);
    uint8_t mask;
    uint8_t negative;
    uint8_t near_player_carry = 0;

    if (e->state & 0x20) {
        move_slow_special_enemy(e);
        return;
    }

    mask = g_SecondaryHardMode ? 0x03 : 0x3f;
    if ((g_PseudoRandomBitReg[slot + 1] & mask) == 0) {
        if (slot & 0x01) {
            /* MoveBloober reaches this branch through TXA/LSR.  LSR leaves
             * the odd slot's bit 0 set in carry; the following LDY/BCS and
             * ProcSwimmingB path preserve it until ChkNearPlayer's ADC. */
            near_player_carry = 1;
            e->moving_dir = g_Player_MovingDir;
        } else {
            /* The even-slot path calls PlayerEnemyDiff after LSR.  Its
             * page SBC carry is likewise live at ChkNearPlayer; DEY and the
             * intervening flag-only branches do not change it. */
            (void)enemy_player_diff_with_carry(e, &negative,
                                               &near_player_carry);
            e->moving_dir = negative ? BTN_RIGHT : BTN_LEFT;
        }
    }

    if (e->y_speed & 0x02) {
        if (e->interval_timer != 0) {
            if ((g_FrameCounter & 0x01) == 0) e->y++;
        } else if ((uint8_t)(e->y + 0x10 + near_player_carry) <
                   g_Player_Y_Position) {
            if ((g_FrameCounter & 0x01) == 0) e->y++;
        } else {
            e->y_speed = 0;
        }
    } else if ((g_FrameCounter & 0x07) == 0) {
        if ((e->y_speed & 0x01) == 0) {
            e->y_mf = (uint8_t)(e->y_mf + 1);
            e->x_speed = e->y_mf;
            if (e->y_mf == 0x02) e->y_speed++;
        } else {
            e->y_mf = (uint8_t)(e->y_mf - 1);
            e->x_speed = e->y_mf;
            if (e->y_mf == 0) {
                e->y_speed++;
                set_enemy_interval_timer(e, 0x02);
            }
        }
    }

    if ((uint8_t)(e->y - e->y_mf) >= 0x20)
        e->y = (uint8_t)(e->y - e->y_mf);
    move_enemy_world_x(e, e->x_speed, e->moving_dir == BTN_RIGHT);
}

/* MoveBulletBill (main.asm:6681-6697).  The frenzy bullet uses the shared
 * horizontal fixed-point owner; the defeated branch is vertical-only. */
void move_bullet_bill(EnemySlot* e) {
    if (e->state & 0x20) {
        move_enemy_gravity(e, 0x1c, 0x00, 0x03);
        return;
    }
    e->x_speed = 0xe8;
    move_enemy_x(e);
}

/* MoveFlyingCheepCheep (main.asm:7047-7053).  The defeated d5 branch uses
 * the ordinary falling gravity owner; the live branch moves horizontally,
 * applies the ROM's 0x0d Y-force, then updates priority from the exact ROM
 * windows above, including the reachable post-table code bytes. */
void move_flying_cheep_cheep(EnemySlot* e) {
    uint8_t vertical_index;
    uint8_t difference;

    if (e->state & 0x20) {
        e->spr_attrib = 0;
        move_enemy_gravity(e, 0x1c, 0x00, 0x03);
        return;
    }

    move_enemy_x(e);
    move_enemy_gravity(e, 0x0d, 0x00, 0x05);

    vertical_index = (uint8_t)(e->y_mf >> 4);
    difference = (uint8_t)(e->y -
                           s_PRandomSubtracterRomWindow[vertical_index]);
    if (difference & 0x80)
        difference = (uint8_t)(0 - difference);
    if (difference < 0x08)
        e->y_mf = (uint8_t)(e->y_mf + 0x10);

    vertical_index = (uint8_t)(e->y_mf >> 4);
    e->spr_attrib = s_FlyCCBPriorityRomWindow[vertical_index];
}

/* MoveSwimmingCheepCheep (main.asm:6699-6768).  The $58 byte is the
 * movement flag, $0434 is the original Y, $0401 is the fractional X force,
 * and $0417 is the fractional Y dummy. */
void move_swimming_cheep(EnemySlot* e) {
    uint8_t amount = (e->id == GreyCheepCheep) ? 0x40 : 0x80;
    uint8_t borrow;
    uint8_t movement_state = (uint8_t)(e->state & 0x20);
    uint16_t position;
    uint8_t difference;
    uint8_t movement_flag;

    if (e->state & 0x20) {
        move_slow_special_enemy(e);
        return;
    }

    borrow = (uint8_t)(e->x_mf < amount);
    e->x_mf = (uint8_t)(e->x_mf - amount);
    if (borrow) move_enemy_world_x(e, 1, 0);

    if (enemy_slot_index(e) < 2) return;

    position = (uint16_t)(((uint16_t)e->y_high << 8) | e->y);
    if (e->x_speed < 0x10) {
        /* CCSwimUpwards: SEC/SBC the $20 vertical force and propagate
         * the borrow through Y_Position/Y_HighPos. */
        borrow = (uint8_t)(e->y_dummy < 0x20);
        e->y_dummy = (uint8_t)(e->y_dummy - 0x20);
        position = (uint16_t)(position -
                              (uint8_t)(movement_state + borrow));
    } else {
        /* The $10 movement mode is the ROM's downward ADC path.  Its carry
         * is from Enemy_YMF_Dummy ($0417), not from a guessed coordinate. */
        uint16_t sum = (uint16_t)e->y_dummy + 0x20;
        e->y_dummy = (uint8_t)sum;
        position = (uint16_t)(position +
                              (uint8_t)(movement_state + (sum >> 8)));
    }
    e->y_high = (uint8_t)(position >> 8);
    e->y = (uint8_t)position;

    difference = (uint8_t)(e->y - e->y_mf);
    if (difference & 0x80) {
        movement_flag = 0x10;
        difference = (uint8_t)(0 - difference);
    } else {
        movement_flag = 0x00;
    }
    if (difference >= 0x0f) {
        /* CheepCheepMoveMFlag is either the upward or downward $10 mode. */
        e->x_speed = movement_flag;
    }
}

/* MovePodoboo (main.asm:6262-6279).  InitPodoboo is re-entered when its
 * interval expires, then the PRNG-derived $0434 force and interval timer are
 * written before the shared $1c/$03 gravity path. */
void move_podoboo(EnemySlot* e) {
    uint8_t slot = enemy_slot_index(e);

    if (e->interval_timer == 0) {
        e->y_high = 0x02;
        e->y = 0x02;
        e->interval_timer = 0x01;
        e->state = 0;
        init_small_bbox(e);
        e->y_mf = (uint8_t)(g_PseudoRandomBitReg[slot + 1] | 0x80);
        set_enemy_interval_timer(e,
            (uint8_t)((e->y_mf & 0x0f) | 0x06));
        e->y_speed = 0xf9;
    }
    move_enemy_gravity(e, 0x1c, 0x00, 0x03);
}

/* MovePiranhaPlant (main.asm:7757-7815).  The Piranha-specific aliases are
 * PiranhaPlant_Y_Speed=$58, PiranhaPlant_MoveFlag=$a0,
 * PiranhaPlantUpYPos=$0417, and PiranhaPlantDownYPos=$0434. */
void move_piranha(EnemySlot* e) {
    uint8_t negative;
    uint8_t difference;
    uint8_t target;
    uint8_t frame_timer = g_Timers[TIMER_ENEMY_FRAME_BASE +
                                    enemy_slot_index(e)];

    if (e->state != 0 || frame_timer != 0) {
        e->spr_attrib = 0x20;
        return;
    }

    if (e->y_speed == 0) {
        if (e->x_speed & 0x80) {
            e->x_speed = (uint8_t)(0 - e->x_speed);
            e->y_speed++;
        } else {
            difference = enemy_player_diff(e, &negative);
            if (negative) difference = (uint8_t)(0 - difference);
            if (difference < 0x21) {
                e->spr_attrib = 0x20;
                return;
            }
            e->x_speed = (uint8_t)(0 - e->x_speed);
            e->y_speed++;
        }
    }

    target = (e->x_speed & 0x80) ? e->y_dummy : e->y_mf;
    if ((g_FrameCounter & 0x01) == 0 || g_TimerControl != 0) {
        e->spr_attrib = 0x20;
        return;
    }
    e->y = (uint8_t)(e->y + e->x_speed);
    if (e->y == target) {
        e->y_speed = 0;
        set_enemy_frame_timer(e, 0x40);
    }
    e->spr_attrib = 0x20;
}

/* MoveD_EnemyVertically/MoveJ_EnemyVertically (main.asm:4594-4660).
 * Landing is deliberately not part of this arithmetic routine: the 6502
 * calls EnemyToBGCollisionDet before movement, and its block-buffer result
 * owns LandEnemyProperly. */
void move_enemy_vertically(EnemySlot* e) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };
    uint8_t downward = (e->state == 0x05) ? 0x20 : 0x3d;

    /* SetHiMax/ImposeGravitySprObj loads maximum speed $03 and enters the
     * shared ImposeGravity path with no upward correction. */
    SprObject_ImposeGravity(&object, downward, 0x00, 0x03, 0);
}


/* SteadM (main.asm:6410-6430).  The temporary speed adjustment is a table
 * lookup used only for this movement call; Enemy_X_Speed is restored after
 * MoveEnemyHorizontally, while Enemy_X_MoveForce and position remain owned by
 * the persistent slot. */
void move_enemy_steady(EnemySlot* e, uint8_t slow) {
    uint8_t saved_speed = e->x_speed;
    uint8_t table_index = slow;

    if (saved_speed & 0x80) table_index = (uint8_t)(table_index + 2);
    e->x_speed = (uint8_t)(saved_speed + s_XSpeedAdderData[table_index]);
    move_enemy_x(e);
    e->x_speed = saved_speed;
}

void move_normal_enemy(EnemySlot* e) {
    uint8_t state = e->state;
    uint8_t low_state;

    /* MoveNormalEnemy's table-independent state tests (main.asm:6330-6375). */
    if (state & 0x40) {
        move_enemy_vertically(e);
        if (e->state == 0x02) {
            move_enemy_x(e);
        } else if (e->id == PowerUpObject) {
            move_enemy_steady(e, 0);
        } else {
            move_enemy_steady(e, 1);
        }
        return;
    }
    if (state & 0x80) {
        move_enemy_steady(e, 0);
        return;
    }
    if (state & 0x20) {
        move_enemy_vertically(e);
        move_enemy_x(e);
        return;
    }

    low_state = state & 0x07;
    if (low_state == 0) {
        move_enemy_steady(e, 0);
    } else if (low_state == 5) {
        move_enemy_vertically(e);
        if (e->state == 0x02) move_enemy_x(e);
        else move_enemy_steady(e, 0);
    } else if (low_state >= 3) {
        /* ReviveStunned/ChkKillGoomba (main.asm:6408-6458). */
        if (e->interval_timer != 0) {
            if (e->interval_timer == 0x0e && e->id == GOOMBA_ID)
                erase_enemy(e);
            return;
        }
        e->state = 0;
        e->moving_dir = (uint8_t)((g_FrameCounter & 0x01) + 1);
        {
            uint8_t speed_index = (uint8_t)(g_FrameCounter & 0x01);
            if (g_PrimaryHardMode) speed_index = (uint8_t)(speed_index + 2);
            e->x_speed = s_RevivedXSpeed[speed_index];
        }
    } else {
        /* States $01/$02 enter FallE just like the ROM's low-state path. */
        move_enemy_vertically(e);
        if (e->state == 0x02) move_enemy_x(e);
        else move_enemy_steady(e, 0);
    }
}

/* MoveJumpingEnemy (main.asm:4639-4648, 6460-6462), the green jumping
 * paratroopa's shared $1c/$03 gravity and ordinary horizontal movement. */
void move_jumping_paratroopa(EnemySlot* e) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };

    SprObject_ImposeGravity(&object, 0x1c, 0x00, 0x03, 0);
    move_enemy_x(e);
}

/* ProcMoveRedPTroopa/MoveRedPTroopa (main.asm:4611-4623, 6466-6487).
 * The red paratroopa has no horizontal movement call in this branch; its
 * vertical motion oscillates between the constructor's original and center
 * Y waypoints, with a slow one-pixel return toward the original position. */
void move_red_paratroopa(EnemySlot* e) {
    SprObjectView object = {
        &e->page, &e->x, &e->y_high, &e->y, &e->x_speed, &e->x_mf,
        &e->y_speed, &e->y_mf, &e->y_dummy, &e->rel_x, &e->rel_y,
        &e->offscreen_bits
    };
    uint8_t moving_up;

    if (e->y_speed == 0 && e->y_mf == 0) {
        e->y_dummy = 0;
        if (e->y < e->x_mf) {
            if ((g_FrameCounter & 0x07) == 0) e->y++;
            return;
        }
    }

    moving_up = (uint8_t)(e->y >= e->x_speed);
    SprObject_ImposeGravity(&object, 0x03, 0x06, 0x02, moving_up);
}

/* XMoveCntr_GreenPTroopa and MoveWithXMCntrs
 * (main.asm:6493-6559).  Green flying paratroopa movement overlays the
 * ordinary enemy slot's $00a0 XMovePrimaryCounter on y_speed and $0058
 * XMoveSecondaryCounter on x_speed.  The temporary signed presentation of
 * the secondary counter is consumed by MoveObjectHorizontally, then the
 * counter byte is restored exactly as the 6502 stack pair does. */
uint8_t move_with_xm_counters(EnemySlot* e) {
    uint8_t saved_secondary = e->x_speed;
    uint8_t direction = BTN_RIGHT;
    uint8_t result;

    if ((e->y_speed & 0x02) == 0) {
        e->x_speed = (uint8_t)(0 - e->x_speed);
        direction = BTN_LEFT;
    }
    e->moving_dir = direction;
    result = move_enemy_x(e);
    e->x_speed = saved_secondary;
    return result;
}

/* MoveFlyGreenPTroopa and XMoveCntr_GreenPTroopa.  This is deliberately
 * separate from MoveJumpingEnemy: ID $0e uses gravity and block landing,
 * while ID $10 uses the two counter aliases and never enters EnemyJump. */
void move_flying_paratroopa(EnemySlot* e) {
    if ((g_FrameCounter & 0x03) == 0) {
        if ((e->y_speed & 0x01) != 0) {
            if (e->x_speed == 0)
                e->y_speed++;
            else
                e->x_speed--;
        } else if (e->x_speed == 0x13) {
            e->y_speed++;
        } else {
            e->x_speed++;
        }
    }

    (void)move_with_xm_counters(e);

    if ((g_FrameCounter & 0x03) == 0) {
        e->y = (uint8_t)(e->y +
                         ((g_FrameCounter & 0x40) ? 0x01 : 0xff));
    }
}

void move_hammer_bro(EnemySlot* e) {
    uint8_t negative;

    /* ProcHammerBro (main.asm:6289-6374).  SpawnHammerObj owns the shared
     * Misc_State/$90 record; the producer and the later MiscObjectsCore pass
     * therefore share the original state rather than a Hammer-Bro-local
     * rendering flag. */
    if (e->state & 0x20) {
        /* The d5 branch is `jmp MoveDefeatedEnemy`, which continues with
         * MoveEnemyHorizontally after MoveD_EnemyVertically. */
        move_normal_enemy(e);
        return;
    }
    if (e->hammer_jump_timer != 0) {
        e->hammer_jump_timer--;
        if ((e->offscreen_bits & 0x0c) == 0) {
            if (e->hammer_throw_timer == 0) {
                e->hammer_throw_timer = throw_timer[g_SecondaryHardMode & 1];
                {
                    if (enemy_try_spawn_hammer(enemy_slot_index(e))) {
                        e->state |= 0x08;
                    } else {
                        /* ProcHammerBro's failed SpawnHammerObj path is
                         * `bcc DecHT`, so the timer loaded immediately
                         * before the attempt is decremented as well
                         * (main.asm:6301-6314). */
                        e->hammer_throw_timer--;
                    }
                }
            } else {
                e->hammer_throw_timer--;
            }
        }
    } else {
        uint8_t jump_class = 0;
        uint8_t y_speed = 0xfa;

        if ((e->state & 0x07) == 0x01) {
            /* Already in the jump state; only the horizontal branch runs. */
        } else {
            if ((e->y & 0x80) == 0) {
                y_speed = 0xfd;
                jump_class = (e->y < 0x70) ? 1 : 0;
            }
            if ((e->y & 0x80) == 0 && e->y >= 0x70 &&
                /* PseudoRandomBitReg+1,X (main.asm:6334), not +2,X. */
                (g_PseudoRandomBitReg[enemy_slot_index(e) + 1] & 1) == 0)
                y_speed = 0xfa;
            e->y_speed = y_speed;
            e->state |= 0x01;
            if (g_SecondaryHardMode) {
                jump_class = (uint8_t)(jump_class &
                    g_PseudoRandomBitReg[enemy_slot_index(e) + 2]);
            } else {
                jump_class = 0;
            }
            set_enemy_frame_timer(e, jump_length[jump_class & 1]);
            e->hammer_jump_timer =
                (uint8_t)(g_PseudoRandomBitReg[enemy_slot_index(e) + 1] |
                          0xc0);
        }
    }

    /* ProcHammerBro loads $fc first and changes it to $04 only when the
     * FrameCounter $40 bit is clear (main.asm:6356-6363). */
    e->x_speed = (g_FrameCounter & 0x40) ? 0xfc : 0x04;
    (void)enemy_player_diff(e, &negative);
    if (negative) {
        e->moving_dir = BTN_RIGHT;
    } else {
        e->moving_dir = BTN_LEFT;
        if (e->interval_timer == 0) e->x_speed = 0xf8;
    }

    /* SetShim falls through to MoveNormalEnemy in the ROM; this is the
     * vertical/horizontal movement consumer for the state selected above. */
    move_normal_enemy(e);
}


uint8_t player_lakitu_diff_with_adjust(
    EnemySlot* e, const uint8_t adjust_data[3]) {
    uint8_t negative;
    uint8_t difference = enemy_player_diff(e, &negative);
    uint8_t adjust_index = 0;
    uint8_t adjust;
    uint8_t pixels;

    if (negative) difference = (uint8_t)(0 - difference);
    /* PlayerLakituDiff (main.asm:7143-7209) clamps at $3c with the
     * 6502's `cmp #$3c / bcc` boundary.  At that boundary a normal Lakitu
     * also updates LakituMoveDirection, which is the Enemy_Y_Speed alias
     * ($00a0+x), and may spend one unit of LakituMoveSpeed ($0058+x) while
     * reversing. */
    if (difference >= 0x3c) {
        difference = 0x3c;
        if (e->id == Lakitu) {
            uint8_t direction = negative ? 1 : 0;

            if (direction != e->y_speed) {
                if (e->y_speed == 0) {
                    e->y_speed = direction;
                } else {
                    e->x_speed--;
                    /* ExMoveLak returns immediately after DEC when the
                     * previous horizontal speed was nonzero.  MoveLakitu
                     * stores that returned byte back through $58+x. */
                    if (e->x_speed != 0)
                        return e->x_speed;
                    e->y_speed = direction;
                }
            }
        }
    }
    pixels = (uint8_t)((difference & 0x3c) >> 2);
    if (g_Player_X_Speed != 0 && g_ScrollAmount != 0) {
        adjust_index = 1;
        if (g_Player_X_Speed >= 0x19 && g_ScrollAmount >= 2)
            adjust_index = 2;
    }
    /* The ROM reaches SubDifAdj directly for a moving Spiny.  Only an
     * object with zero Enemy_Y_Speed after the ChkSpinyO path is forced back
     * to adjuster zero; nonzero vertical/direction aliases retain the
     * movement-selected row. */
    if ((e->id != Spiny || g_Player_X_Speed == 0) && e->y_speed == 0)
        adjust_index = 0;
    adjust = adjust_data[adjust_index];
    /* SPixelLak (main.asm:7201-7207) decrements Y after the subtraction and
     * branches while N is clear, so a count of N performs N+1 subtractions.
     * Keep the zero-count subtraction instead of translating it as a C
     * post-decrement loop with only N iterations. */
    do {
        adjust--;
    } while (pixels-- != 0);
    return adjust;
}

uint8_t player_lakitu_diff(EnemySlot* e) {
    return player_lakitu_diff_with_adjust(e, s_LakituDiffAdjust);
}

/* PutAtRightExtent (main.asm:5623-5655) writes the screen-relative spawn
 * position and the common active-object fields.  It is used by CreateL in
 * LakituAndSpinyHandler as well as by the flame path, so keep the page carry
 * in the object owner instead of introducing a screen-coordinate shortcut. */
void put_enemy_at_right_extent(EnemySlot* e, uint8_t y) {
    uint16_t edge = (uint16_t)(((uint16_t)g_ScreenRight_PageLoc << 8) |
                               g_ScreenRight_X_Pos);
    edge = (uint16_t)(edge + 0x20);
    e->y = y;
    e->x = (uint8_t)edge;
    e->page = (uint8_t)(edge >> 8);
    e->bbox_ctrl = 0x08;
    e->y_high = 0x01;
    e->flag = 0x01;
    e->x_mf = 0;
    e->state = 0;
}

void move_lakitu(EnemySlot* e) {
    uint8_t speed;

    if (e->state & 0x20) {
        /* MoveLakitu branches to MoveD_EnemyVertically for defeated enemies.
         * The common ContVMove path uses force $3d and max speed $03; only
         * enemy state $05 enters MoveFallingPlatform with force $20. */
        move_enemy_gravity(e, 0x3d, 0x00, 0x03);
        return;
    }
    if (e->state != 0) {
        /* MoveLakitu (main.asm:7103-7139) clears LakituMoveDirection,
         * which overlays Enemy_Y_Speed=$00a0+x; it does not clear the
         * generic Enemy_Y_MoveForce byte at $0434+x. */
        e->y_speed = 0;
        g_EnemyFrenzyBuffer = 0;
        speed = 0x10;
    } else {
        g_EnemyFrenzyBuffer = Spiny;
        speed = player_lakitu_diff(e);
    }
    /* SetLSpd/SetLMov uses LakituMoveDirection's bit zero: one means move
     * right, zero negates LakituMoveSpeed and moves left. */
    e->x_speed = speed;
    if (e->y_speed & 0x01) {
        e->moving_dir = BTN_RIGHT;
    } else {
        e->x_speed = (uint8_t)(0 - e->x_speed);
        e->moving_dir = BTN_LEFT;
    }
    move_enemy_x(e);
}

void move_spiny(EnemySlot* e) {
    /* MoveNormalEnemy's ID-$12 table entry is the ordinary horizontal/
     * vertical state machine; the egg's state-$05 path is already represented
     * by the shared low-state dispatch. */
    move_normal_enemy(e);
}

enum {
    ENEMY_MOVE_NORMAL = 0,
    ENEMY_MOVE_NO_CODE = 1,
    ENEMY_MOVE_DEFERRED = 2,
    ENEMY_MOVE_JUMPING_PARATROOPA = 3,
    ENEMY_MOVE_RED_PARATROOPA = 4,
    ENEMY_MOVE_BLOOBER = 5,
    ENEMY_MOVE_BULLET_BILL = 6,
    ENEMY_MOVE_SWIMMING_CHEEP = 7,
    ENEMY_MOVE_PODOBOO = 8,
    ENEMY_MOVE_PIRANHA = 9,
    ENEMY_MOVE_HAMMER = 10,
    ENEMY_MOVE_LAKITU = 11,
    ENEMY_MOVE_SPINY = 12,
    ENEMY_MOVE_FLYING_CHEEP = 13,
    ENEMY_MOVE_FLYING_PARATROOPA = 14
};

/* EnemyMovementSubs (main.asm:6180-6203), including its explicit deferred
 * specialized routines.  The table is the dispatch owner even where the
 * type-specific implementation belongs to a later roadmap cluster. */
static const uint8_t s_EnemyMovementDispatch[0x15] = {
    /* $00-$06: normal enemy movement, with the ASM $05 entry reserved
     * for Hammer Bro.  The index is the raw Enemy_ID, not a compacted
     * constructor ordinal. */
    ENEMY_MOVE_NORMAL, ENEMY_MOVE_NORMAL, ENEMY_MOVE_NORMAL,
    ENEMY_MOVE_NORMAL, ENEMY_MOVE_NORMAL, ENEMY_MOVE_HAMMER,
    ENEMY_MOVE_NORMAL,
    /* $07-$0d: Bloober, Bullet Bill, Cheeps, Podoboo, Piranha. */
    ENEMY_MOVE_BLOOBER, ENEMY_MOVE_BULLET_BILL, ENEMY_MOVE_NO_CODE,
    ENEMY_MOVE_SWIMMING_CHEEP, ENEMY_MOVE_SWIMMING_CHEEP,
    ENEMY_MOVE_PODOBOO, ENEMY_MOVE_PIRANHA,
    /* $0e-$14: paratroopas, Lakitu/Spiny, and the flying Cheep. */
    ENEMY_MOVE_JUMPING_PARATROOPA, ENEMY_MOVE_RED_PARATROOPA,
    ENEMY_MOVE_FLYING_PARATROOPA, ENEMY_MOVE_LAKITU, ENEMY_MOVE_SPINY,
    ENEMY_MOVE_NO_CODE, ENEMY_MOVE_FLYING_CHEEP
};

void enemy_movement_dispatch(EnemySlot* e) {
    uint8_t route = (e->id < 0x15)
        ? s_EnemyMovementDispatch[e->id] : ENEMY_MOVE_DEFERRED;

    if (route == ENEMY_MOVE_NORMAL) {
        move_normal_enemy(e);
    } else if (route == ENEMY_MOVE_JUMPING_PARATROOPA) {
        move_jumping_paratroopa(e);
    } else if (route == ENEMY_MOVE_RED_PARATROOPA) {
        move_red_paratroopa(e);
    } else if (route == ENEMY_MOVE_FLYING_PARATROOPA) {
        move_flying_paratroopa(e);
    } else if (route == ENEMY_MOVE_BLOOBER) {
        move_bloober(e);
    } else if (route == ENEMY_MOVE_BULLET_BILL) {
        move_bullet_bill(e);
    } else if (route == ENEMY_MOVE_SWIMMING_CHEEP) {
        move_swimming_cheep(e);
    } else if (route == ENEMY_MOVE_PODOBOO) {
        move_podoboo(e);
    } else if (route == ENEMY_MOVE_PIRANHA) {
        move_piranha(e);
    } else if (route == ENEMY_MOVE_HAMMER) {
        move_hammer_bro(e);
    } else if (route == ENEMY_MOVE_LAKITU) {
        move_lakitu(e);
    } else if (route == ENEMY_MOVE_SPINY) {
        move_spiny(e);
    } else if (route == ENEMY_MOVE_FLYING_CHEEP) {
        move_flying_cheep_cheep(e);
    }
    /* The platform/frenzy branches outside the supported Cheep-Cheep child
     * remain explicit deferred table entries for their owning clusters. */
}

void move_lift_platforms(EnemySlot *e) {
    uint16_t sum;

    if (g_TimerControl != 0) return;

    /* MoveLiftPlatforms (main.asm:8183-8193) is deliberately not the
     * generic MoveObjectVertically/ImposeGravity path.  It accumulates the
     * platform's fractional Y force and adds the speed to the low position,
     * but has no STA Enemy_Y_HighPos.  Keeping the high byte owned by the
     * platform constructor is what lets the ROM's vertical offscreen test
     * treat a lift crossing Y=$00 as its wrapped in-screen half. */
    sum = (uint16_t)e->y_dummy + e->y_mf;
    e->y_dummy = (uint8_t)sum;
    sum = (uint16_t)e->y + e->y_speed + (sum >> 8);
    e->y = (uint8_t)sum;
}

void move_platform_gravity(EnemySlot *e, uint8_t upwards) {
    SprObjectView object = platform_object(e);
    SprObject_ImposeGravity(&object, 0x05, 0x0a, 0x03, upwards);
}

void stop_platforms(EnemySlot *e, uint8_t y_target) {
    e->y_speed = 0;
    e->y_mf = 0;
    if (y_target < ENEMY_SLOT_COUNT) {
        enemies[y_target].y_speed = 0;
        enemies[y_target].y_mf = 0;
    }
}

/* InitPlatformFall (main.asm:8056-8064).  GetEnemyOffscreenBits computes the
 * paired slot's offscreen byte but restores X to ObjectOffset before
 * returning.  The following SetupFloateyNumber and moving-direction writes
 * therefore belong to the current platform, while the offscreen result
 * belongs to the paired slot.  The later StopPlatforms call does not reuse
 * that pair pointer: GetEnemyOffscreenBits leaves Y=$01 in the original
 * routine, and SetupFloateyNumber does not alter Y. */
void init_platform_fall(EnemySlot *current, EnemySlot *other) {
    const uint8_t get_enemy_offscreen_post_y = 0x01;

    other->offscreen_bits = enemy_offscreen_bits(other);
    setup_floaty_number(current, 0x06);
    current->floaty_x = g_Player_Rel_XPos;
    current->floaty_y = g_Player_Y_Position;
    current->moving_dir = 1;
    /* InitPlatformFall falls through to StopPlatforms (main.asm:8065),
     * which uses the live Y register, not the paired object pointer. */
    stop_platforms(current, get_enemy_offscreen_post_y);
}

/* MoveFallingPlatform (main.asm:4600-4605) uses the shared gravity owner
 * with downward force $20 and maximum speed $03. */
void move_falling_platform(EnemySlot *e) {
    move_enemy_gravity(e, 0x20, 0x00, 0x03);
}

/* PlatformFall (main.asm:8071-8084).  Both paired slots move through the
 * same object-owned vertical state.  PositionPlayerOnVPlat receives the
 * collision slot, not the slot currently being dispatched. */
void platform_fall(EnemySlot *e, EnemySlot *other) {
    move_falling_platform(e);
    move_falling_platform(other);
    if ((e->platform_collision_flag & 0x80) == 0 &&
        e->platform_collision_flag < ENEMY_SLOT_COUNT)
        position_player_on_vplat(
            &enemies[e->platform_collision_flag],
            enemies[e->platform_collision_flag].y);
}

void setup_platform_rope(const EnemySlot *platform, uint8_t speed,
                                uint8_t *high, uint8_t *low) {
    uint16_t x = (uint16_t)platform->x + 8;
    uint8_t y = platform->y;
    uint8_t rope_page;

    if (!g_SecondaryHardMode) x = (uint16_t)(x + 0x10);
    rope_page = (uint8_t)(platform->page + (x >> 8));
    if (speed & 0x80) y = (uint8_t)(y + 8);

    /* SetupPlatformRope performs ASL/ROL/ROL on the vertical coordinate.
     * The first two rotates supply the nametable low-byte row bits; the
     * third supplies the two-bit nametable high-byte quadrant.  Keep these
     * separate from the horizontal page carry and from the final bottom-row
     * mask, exactly as the 6502 stack temporaries do. */
    *low = (uint8_t)(((uint8_t)(y << 2) & 0xe0) +
                     (((uint8_t)x & 0xf0) >> 3));
    *high = (uint8_t)(0x20 | ((y >> 6) & 0x03) |
                      ((rope_page & 0x01) << 2));
    if (platform->y >= 0xe8) *low &= 0xbf;
}

void write_platform_rope(const EnemySlot *e, const EnemySlot *other,
                                uint8_t speed) {
    uint8_t offset = g_VRAM_Buffer1_Offset;
    uint8_t h1, l1, h2, l2;

    if (other == 0 || offset >= 0x20) return;
    setup_platform_rope(e, speed, &h1, &l1);
    setup_platform_rope(other, (uint8_t)~speed, &h2, &l2);
    g_VRAM_Buffer1[offset + 0] = h1;
    g_VRAM_Buffer1[offset + 1] = l1;
    g_VRAM_Buffer1[offset + 2] = 0x02;
    g_VRAM_Buffer1[offset + 3] = (speed & 0x80) ? 0x24 : 0xa2;
    g_VRAM_Buffer1[offset + 4] = (speed & 0x80) ? 0x24 : 0xa3;
    g_VRAM_Buffer1[offset + 5] = h2;
    g_VRAM_Buffer1[offset + 6] = l2;
    g_VRAM_Buffer1[offset + 7] = 0x02;
    g_VRAM_Buffer1[offset + 8] = ((speed & 0x80) == 0) ? 0x24 : 0xa2;
    g_VRAM_Buffer1[offset + 9] = ((speed & 0x80) == 0) ? 0x24 : 0xa3;
    g_VRAM_Buffer1[offset + 10] = 0x00;
    g_VRAM_Buffer1_Offset = (uint8_t)(offset + 10);
}

void balance_platform(EnemySlot *e, uint8_t slot) {
    EnemySlot *other;
    uint8_t old_y;
    uint16_t force_sum;
    uint8_t force_plus_five;
    uint8_t speed_probe;

    if (e->y_high == 0x03) {
        erase_enemy(e);
        return;
    }
    if (e->state & 0x80 || e->state >= ENEMY_SLOT_COUNT) return;
    other = &enemies[e->state];
    /* BalancePlatform (main.asm:7857-7863) uses Enemy_State,x as the
     * partner offset without testing the partner's Enemy_Flag.  PlatformFall
     * therefore still advances the partner's retained vertical state after
     * that slot has been erased from the active object list. */

    /* BalancePlatform's first threshold compares A=$2d against the current
     * Y and branches only when current Y is strictly greater than $2d. */
    if (e->moving_dir != 0) {
        platform_fall(e, other);
        return;
    }
    if (e->y <= 0x2d) {
        if (e->state == e->platform_collision_flag) {
            init_platform_fall(e, other);
            return;
        }
        e->y = (uint8_t)(e->y + 2);
        stop_platforms(e, e->state);
        return;
    }
    if (other->y <= 0x2d) {
        if (slot == e->platform_collision_flag) {
            init_platform_fall(e, other);
            return;
        }
        other->y = (uint8_t)(other->y + 2);
        stop_platforms(e, e->state);
        return;
    }

    old_y = e->y;
    if ((e->platform_collision_flag & 0x80) == 0) {
        if (slot == e->platform_collision_flag)
            move_platform_gravity(e, 0);
        else
            move_platform_gravity(e, 1);
    } else {
        /* ChkToMoveBalPlat probes force+5 and the carry into speed without
         * storing either byte; MovePlatformUp/Down performs the only actual
         * gravity update below. */
        force_sum = (uint16_t)e->y_mf + 5;
        force_plus_five = (uint8_t)force_sum;
        speed_probe = (uint8_t)(e->y_speed + (uint8_t)(force_sum >> 8));
        if (speed_probe & 0x80)
            move_platform_gravity(e, 0);
        else if (speed_probe != 0 || force_plus_five >= 0x0b)
            move_platform_gravity(e, 1);
        else
            stop_platforms(e, e->state);
    }
    other->y = (uint8_t)(other->y + old_y - e->y);
    if ((e->platform_collision_flag & 0x80) == 0 &&
        e->platform_collision_flag < ENEMY_SLOT_COUNT)
        position_player_on_vplat(
            &enemies[e->platform_collision_flag],
            enemies[e->platform_collision_flag].y);
    if (e->y_speed != 0 || e->y_mf != 0)
        write_platform_rope(e, other, e->y_speed);
}

void vertical_platform(EnemySlot *e) {
    if (e->y_speed == 0 && e->y_mf == 0) {
        e->y_dummy = 0;
        if (e->y >= e->y_platform_top) {
            if (e->y < e->y_platform_center)
                move_platform_gravity(e, 0);
            else
                move_platform_gravity(e, 1);
        } else if ((g_FrameCounter & 0x07) == 0) {
            e->y++;
        }
    } else if (e->y < e->y_platform_center) {
        move_platform_gravity(e, 0);
    } else {
        move_platform_gravity(e, 1);
    }
    if (e->platform_collision_flag != 0xff)
        position_player_on_vplat(e, e->y);
}

void horizontal_platform(EnemySlot *e, uint8_t right_platform) {
    uint8_t maximum = 0x0e;
    uint8_t displacement;
    uint8_t original_secondary;

    if ((g_FrameCounter & 0x03) == 0) {
        if ((e->y_speed & 0x01) == 0) {
            if (e->x_move_secondary_counter == maximum)
                e->y_speed++;
            else
                e->x_move_secondary_counter++;
        } else if (e->x_move_secondary_counter == 0) {
            e->y_speed++;
        } else {
            e->x_move_secondary_counter--;
        }
    }
    /* XMoveCntr_Platform owns the persistent secondary counter.  The stack
     * save in MoveWithXMCntrs occurs after that update, so restoring a value
     * captured before the counter routine would discard every fourth-frame
     * increment/decrement (main.asm:6516-6537, 6539-6558). */
    original_secondary = e->x_move_secondary_counter;
    if ((e->y_speed & 0x02) == 0) {
        e->x_move_secondary_counter =
            (uint8_t)(0 - e->x_move_secondary_counter);
        e->moving_dir = BTN_LEFT;
    } else {
        e->moving_dir = BTN_RIGHT;
    }
    displacement = move_enemy_x(e);
    e->x_move_secondary_counter = original_secondary;
    if (e->platform_collision_flag != 0xff) {
        position_player_on_hplat(displacement);
        position_player_on_vplat(e, e->y);
    }
    if (right_platform && e->platform_collision_flag != 0xff)
        e->x_speed = 0x10;
}

/* RightPlatform (main.asm:8162-8171) is not XMovingPlatform.  It first
 * moves with the current Enemy_X_Speed, then starts the platform at $10 only
 * after a player collision and carries that same displacement into the
 * player through PositionPlayerOnHPlat. */
void right_platform(EnemySlot *e) {
    uint8_t displacement = move_enemy_x(e);

    if ((e->platform_collision_flag & 0x80) != 0)
        return;
    e->x_speed = 0x10;
    position_player_on_hplat(displacement);
    position_player_on_vplat(e, e->y);
}

void drop_platform(EnemySlot *e) {
    if (e->platform_collision_flag == 0xff) return;
    move_enemy_gravity(e, 0x7f, 0x00, 0x02);
    position_player_on_vplat(e, e->y);
}

uint8_t byte_abs_delta(uint8_t left, uint8_t right) {
    uint8_t delta = (uint8_t)(left - right);
    return (delta & 0x80) ? (uint8_t)(0 - delta) : delta;
}

/* DrawFirebar_Collision (main.asm:6884-6900) compares the wrapped X
 * coordinates with CMP and then subtracts the smaller byte from the larger
 * one.  It is an unsigned directional distance, not a signed byte distance:
 * an arm crossing from $fc to $02 is $fa away for the offscreen test. */
uint8_t firebar_coordinate_delta(uint8_t x, uint8_t center_x) {
    return x >= center_x ? (uint8_t)(x - center_x) :
                           (uint8_t)(center_x - x);
}


void firebar_spin(EnemySlot* e) {
    uint16_t spin = (uint16_t)(((uint16_t)e->y_speed << 8) | e->x_speed);

    if (firebar_direction_value(e) == 0)
        spin = (uint16_t)(spin + e->firebar_spin_speed);
    else
        spin = (uint16_t)(spin - e->firebar_spin_speed);
    e->x_speed = (uint8_t)spin;
    e->y_speed = (uint8_t)(spin >> 8);
}

void firebar_position(const EnemySlot* e, uint8_t segment,
                             uint8_t* horizontal, uint8_t* vertical,
                             uint8_t* mirror) {
    uint8_t high = e->y_speed;
    uint8_t h_index = (uint8_t)(high & 0x0f);
    uint8_t v_index = (uint8_t)((high + 8) & 0x0f);
    uint8_t row = segment < 12 ? segment : 0;
    uint8_t offset = s_FirebarTableOffsets[row];

    if (h_index >= 9) h_index = (uint8_t)(16 - h_index);
    if (v_index >= 9) v_index = (uint8_t)(16 - v_index);
    *horizontal = s_FirebarPosLookup[offset + h_index];
    *vertical = s_FirebarPosLookup[offset + v_index];
    *mirror = s_FirebarMirrorData[(high >> 3) & 3];
}
