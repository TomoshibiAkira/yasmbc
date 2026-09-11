/* engine/player/player-physics.c - Player mechanics and physics
 *
 * Faithful reimplementation of SMB1 player control from:
 *   smb1-disasm/engine/game-mode/player-movement.asm
 *   smb1-disasm/engine/game-mode/routine/player-control.asm
 */

#include "player/player.h"
#include "spr-object.h"
#include "assets.h"
#include "constants/defs.h"
#include "constants/globals.h"

/* ========================================================================
 * PHYSICS TABLES (from disassembly)
 * ======================================================================== */

/* Max horizontal speeds: index 0=ground, 1=water, 2=pipe_entrance */
static uint8_t MaxRightXSpdData[4];
static uint8_t MaxLeftXSpdData[3];
static uint8_t FrictionData[3];
static uint8_t JumpMForceData[7];
static uint8_t FallMForceData[7];
static uint8_t PlayerYSpdData[7];
static uint8_t InitMForceData[7];
static uint8_t Climb_Y_SpeedData[3];
static uint8_t Climb_Y_MForceData[3];
static uint8_t ClimbAdderLow[4];
static uint8_t ClimbAdderHigh[4];
static uint8_t PlayerAnimTmrData[3];

/* ========================================================================
 * HELPER MACROS
 * ======================================================================== */

/* Check if button was newly pressed this frame */
#define BUTTON_PRESSED(btn) ((g_A_B_Buttons & (btn)) && !(g_PreviousA_B_Buttons & (btn)))
#define BUTTON_HELD(btn)    (g_A_B_Buttons & (btn))

/* Forward declarations for internal functions */
static void Player_AirMove(void);
static void Player_ImposeGravity(void);
static void Player_ImposeFriction(void);
static void Player_CalcFriction(void);
static void Player_MoveHorizontally(void);
static void Player_ClimbPhysics(void);
static void Player_ClimbingSub(void);

/* ========================================================================
 * INITIALIZATION
 * ======================================================================== */

void Player_Init(void) {
    /* NES birth position: X=$28 (PlayerStarting_X_Pos[0]), standing on
     * ground (Y=$B0 head position for small Mario on floor rows). */
    g_Player_X_Position = 0x28;
    g_Player_PageLoc = 0;
    g_Player_Y_Position = 0xB0;
    g_Player_Y_HighPos = 0;
    g_Player_X_Speed = 0;
    g_Player_Y_Speed = 0;
    g_Player_X_MoveForce = 0;
    g_Player_X_MF2 = 0;
    g_Player_Y_MoveForce = 0;
    g_Player_YMF_Dummy = 0;
    /* InitializeArea's InitializeMemory clears the zero-page controller
     * latches before Entrance_GameTimerSetup (main.asm:1429-1453). */
    g_A_B_Buttons = 0;
    g_Left_Right_Buttons = 0;
    g_Up_Down_Buttons = 0;
    g_JumpspringForce = 0;
    g_JumpspringAnimCtrl = 0;
    g_Player_XSpeedAbsolute = 0;
    /* InitializeMemory leaves Player_BoundBoxCtrl ($0499) at zero for the
     * entrance interval.  ChkMoveDir in PlayerCtrlRoutine writes the size
     * selector only when normal control begins; Entrance_GameTimerSetup does
     * not initialize this byte in the ROM. */
    /* InitializeMemory/BoundingBoxCore own the persistent player box at
     * $04ac-$04af.  No new-area frame has produced it yet. */
    g_Player_BoundingBox[0] = 0;
    g_Player_BoundingBox[1] = 0;
    g_Player_BoundingBox[2] = 0;
    g_Player_BoundingBox[3] = 0;
    g_Player_OffscreenBits = 0;
    g_PlayerGfxOffset = 0;
    g_FireballThrowingTimer = 0;
    g_Player_State = PLAYER_STATE_GROUND;
    /* PlayerBGCollision's entrance initializer owns the DEC from cleared RAM;
     * do not pre-fill this byte in the generic player constructor. */
    g_Player_CollisionBits = 0;
    g_Player_MovingDir = 0;    /* 0 on NES (zero-page init), set to BTN_RIGHT/BTN_LEFT when speed != 0 */
    g_PlayerFacingDir = BTN_RIGHT;  /* Face right ($01) */
    g_PlayerAnimTimer = 0;
    /* InitializeArea clears PlayerAnimTimerSet ($070C) with the general RAM
     * clear; keep the named player constructor's scalar state equivalent. */
    g_PlayerAnimTimerSet = 0;
    g_PlayerAnimCtrl = 0;
    g_CrouchingFlag = 0;
    g_JumpSwimTimer = 0;
    g_VerticalForce = 0;
    g_FallMForce = 0;
    g_JumpOrigin_Y = 0;
    g_DiffToHaltJump = 0;
    g_RunningTimer = 0;
    g_RunningSpeed = 0;
}

/* ========================================================================
 * PLAYER CONTROL - Input parsing
 * From player-control.asm: PlayerCtrlRoutine
 * ======================================================================== */

void Player_UpdateControl(void) {
    /* PlayerCtrlRoutine (player-control.asm:6-121):
     * Parse joypad input, set facing direction, handle water/crouch/death.
     */
    /* Skip controller processing if GameEngineSubroutine==$0B (player killed) */
    if (g_GameEngineSubroutine == 0x0B) {
        goto size_check;
    }

    /* Water area: disable controller input when player is outside the
     * normal vertical gameplay band (player-control.asm:10-20).
     * Condition: AreaType==WATER AND (Y_HighPos!=1 OR Y_Position>=$D0).
     * NES zeroes SavedJoypadBits directly so subsequent A-button jump
     * checks also see no input.
     */
    if (g_AreaType == AREA_TYPE_WATER &&
        (g_Player_Y_HighPos != 1 || g_Player_Y_Position >= 0xD0)) {
        g_SavedJoypadBits = 0;
    }

    /* SaveJoyp: these are persistent zero-page latches, not C locals.  The
     * movement routines and later collision code read these exact subsets. */
    g_A_B_Buttons = g_SavedJoypadBits & (BTN_A | BTN_B);
    g_Left_Right_Buttons = g_SavedJoypadBits & (BTN_LEFT | BTN_RIGHT);
    g_Up_Down_Buttons = g_SavedJoypadBits & (BTN_UP | BTN_DOWN);

size_check:
    /* Down button clears left/right when on ground (player-control.asm:31-39).
     * Size-independent — both small and big players have L/R nulled when
     * pressing down on the ground. This must precede SetCrouch: the 6502
     * reaches PlayerMovementSubs only after this clear, so a simultaneous
     * DOWN+L/R input leaves Up_Down_Buttons zero for the crouch write.
     */
    if ((g_Up_Down_Buttons & BTN_DOWN) &&
        g_Player_State == PLAYER_STATE_GROUND &&
        g_Left_Right_Buttons) {
        g_Left_Right_Buttons = 0;
        g_Up_Down_Buttons = 0;
    }

    /* SetCrouch (player-movement.asm:2-11) is inside PlayerMovementSubs,
     * after PlayerCtrlRoutine's DOWN/LR clear:
     * Small player: CrouchingFlag = 0 (always).
     * Big + on ground: CrouchingFlag = down button bit ($04 if pressed,
     * $00 if not).
     * Big + airborne: CrouchingFlag UNCHANGED (BNE ProcMove skips the store).
     */
    if (g_PlayerSize != PLAYER_SIZE_SMALL) {
        if (g_Player_State == PLAYER_STATE_GROUND) {
            g_CrouchingFlag = (g_Up_Down_Buttons & BTN_DOWN) ? BTN_DOWN : 0;
        }
        /* else: airborne — leave CrouchingFlag unchanged */
    } else {
        g_CrouchingFlag = 0;
    }

    /* PlayerHole is ordered after PlayerBGCollision in PlayerCtrlRoutine;
     * do not clamp Player_Y_HighPos here.  The 6502 lets the vertical object
     * byte advance and consumes it in the post-collision hole branch. */
}

/* LRAir (player-movement.asm:97-110): JUMP and FALL share this path.
 * Friction runs only when a direction button is held (beq JSMove skips
 * ImposeFriction in air — no-button air keeps horizontal speed).
 * Ground collision runs separately (PlayerBGCollision). */
static void Player_AirMove(void) {
    if (g_Left_Right_Buttons)
        Player_ImposeFriction();
    Player_MoveHorizontally();
    if (g_GameEngineSubroutine == 0x0b) {
        /* PlayerDeath's ExitMov1 override occurs immediately
         * before MovePlayerVertically. */
        g_VerticalForce = 0x28;
    }
    Player_ImposeGravity();
}

/* ========================================================================
 * MOVEMENT SUBS - State machine dispatcher
 * From player-movement.asm: PlayerMovementSubs
 * ======================================================================== */

void Player_MovementSubs(void) {
    /* NES PlayerMovementSubs (player-movement.asm:2-30):
     *   1. SetCrouch (handled in Player_UpdateControl)
     *   2. jsr PlayerPhysicsSub  (jump check + friction setup)
     *   3. jsr JumpEngine        (state dispatch)
     *
     * PlayerPhysicsSub:CheckForJumping runs BEFORE the state dispatch.
     * If a jump is initiated, state changes to JUMP, then JumpEngine
     * dispatches to JumpSwimSub on the SAME frame — running the full
     * physics pipeline (friction, horizontal movement, gravity) even
     * on the jump initiation frame.
     */

    /* Jump check (NES PlayerPhysicsSub:CheckForJumping/ProcJumping/InitJS) */
    /* ProcJumping is entered only on the A-button rising edge.  Holding A
     * across a landing must not restart a ground jump; the swimming branch
     * below still permits a new stroke when its own timer/state allows it. */
    /* CheckForJumping (player-movement.asm:209) branches to NoJump while
     * JumpspringAnimCtrl is active.  The spring handler owns the player's
     * vertical motion until it clears that control byte; an A-button edge
     * must not enter InitJS on the same frames. */
    if (g_JumpspringAnimCtrl == 0 && BUTTON_PRESSED(BTN_A)) {
        /* Calculate speed index (0-4) from absolute speed */
        uint8_t abs_speed = g_Player_XSpeedAbsolute;
        uint8_t speed_idx;
        uint8_t initialize_jump = (g_Player_State == PLAYER_STATE_GROUND);
        if (!initialize_jump && g_SwimmingFlag &&
            (g_JumpSwimTimer != 0 || (uint8_t)g_Player_Y_Speed < 0x80)) {
            /* ProcJumping's BPL InitJS permits a swim re-init while the
             * swim timer is active or while Player_Y_Speed is nonnegative. */
            initialize_jump = 1;
        }
        if (initialize_jump && g_SwimmingFlag) {
            /* ProcJumping selects swimming row 5, or row 6 when the
             * Whirlpool_Flag object producer owns the current whirlpool. */
            speed_idx = g_WhirlpoolFlag ? 6 : 5;
        } else if (initialize_jump && abs_speed >= 0x1C) {
            speed_idx = 4;
        } else if (initialize_jump && abs_speed >= 0x19) {
            speed_idx = 3;
        } else if (initialize_jump && abs_speed >= 0x10) {
            speed_idx = 2;
        } else if (initialize_jump && abs_speed >= 0x09) {
            speed_idx = 1;
        } else {
            speed_idx = 0;
        }

        if (initialize_jump) {
            /* InitJS (player-movement.asm:229-271) */
            g_JumpSwimTimer = 0x20;                          /* NES: lda #$20; sta JumpSwimTimer */
            g_Player_YMF_Dummy = 0;                           /* NES: sty Player_YMF_Dummy */
            g_Player_Y_MoveForce = 0;                         /* NES: sty Player_Y_MoveForce (overwritten below) */
            g_JumpOrigin_Y_HighPos = g_Player_Y_HighPos;      /* NES: sta JumpOrigin_Y_HighPos */
            g_JumpOrigin_Y = g_Player_Y_Position;             /* NES: sta JumpOrigin_Y_Position */
            g_Player_State = PLAYER_STATE_JUMP;               /* NES: lda #$01; sta Player_State */
            g_DiffToHaltJump = 1;                             /* NES: lda #$01; sta DiffToHaltJump */
            g_VerticalForce = JumpMForceData[speed_idx];      /* NES: lda JumpMForceData,y; sta VerticalForce */
            g_FallMForce = FallMForceData[speed_idx];         /* NES: lda FallMForceData,y; sta VerticalForceDown */
            g_Player_Y_MoveForce = (int8_t)InitMForceData[speed_idx]; /* NES: lda InitMForceData,y; sta Player_Y_MoveForce */
            g_Player_Y_Speed = (int8_t)PlayerYSpdData[speed_idx];     /* NES: lda PlayerYSpdData,y; sta Player_Y_Speed */

            /* GetYPhy/PJumpSnd (player-movement.asm:272-281): a swim
             * stroke queues the stomp cue, and InitJS clamps the initial
             * upward speed when the player's low Y byte is above the water
             * surface.  This is separate from JumpSwimSub's later
             * VerticalForce=$18 surface guard. */
            if (g_SwimmingFlag) {
                g_Square1SoundQueue = Sfx_EnemyStomp;
                if (g_Player_Y_Position < 0x14)
                    g_Player_Y_Speed = 0;
            } else {
                g_Square1SoundQueue = (g_PlayerSize == PLAYER_SIZE_SMALL)
                    ? Sfx_SmallJump : Sfx_BigJump;
            }
        }
    }

    /* PlayerPhysicsSub runs X_Physics before PlayerMovementSubs tests
     * PlayerChangeSizeFlag.  Its climbing branch returns before X_Physics, so
     * preserve that separate control-flow boundary (player-movement.asm:
     * 184-217, 289-346). */
    if (g_Player_State != PLAYER_STATE_CLIMB)
        Player_CalcFriction();

    /* PlayerMovementSubs returns immediately after PlayerPhysicsSub when a
     * size-change task owns the player, matching the NoMoveSub branch. */
    if (g_PlayerChangeSizeFlag) return;

    /* PlayerMovementSubs (player-movement.asm:10-29) reloads
     * ClimbSideTimer=$0789 for every non-climb state before JumpEngine.
     * Keeping the reload here prevents the first frame after a flagpole or
     * vine collision from consuming ClimbAdderLow immediately. */
    if (g_Player_State != PLAYER_STATE_CLIMB)
        g_ClimbSideTimer = 0x18;

    /* PlayerPhysicsSub has a separate climbing branch and does not compute
     * horizontal friction for it. */
    if (g_Player_State == PLAYER_STATE_CLIMB) {
        Player_ClimbPhysics();
        Player_ClimbingSub();
        return;
    }

    /* State dispatch (NES JumpEngine) */
    switch (g_Player_State) {
        case PLAYER_STATE_GROUND:
            /* OnGroundStateSub (player-movement.asm:35-44):
             * GetPlayerAnimSpeed runs FIRST (sets skid stop, RunningSpeed,
             * PlayerAnimTimerSet), THEN facing dir, friction and movement.
             */
            Player_GetAnimSpeed();

            /* Set facing direction from L/R buttons (NES OnGroundStateSub:37-39) */
            {
                uint8_t buttons_lr = g_Left_Right_Buttons;
                if (buttons_lr) {
                    g_PlayerFacingDir = buttons_lr;
                }
            }
            Player_ImposeFriction();
            Player_MoveHorizontally();
            break;

        case PLAYER_STATE_JUMP:
            /* JumpSwimSub (player-movement.asm:55-96):
             * Manage VerticalForce based on A button state.
             * NES does NOT transition to FALL state — stays JUMP until landing.
             * "Falling" behavior comes from VerticalForce being increased.
             */
            if ((uint8_t)g_Player_Y_Speed < 0x80) {
                /* Speed turned positive (moving downward) → use heavy gravity */
                g_VerticalForce = g_FallMForce;
            } else if ((g_A_B_Buttons & BTN_A) &&
                       (g_PreviousA_B_Buttons & BTN_A)) {
                /* A held AND was held last frame → keep light gravity (float) */
            } else {
                /* A released or not held previous frame */
                uint8_t delta = g_JumpOrigin_Y - g_Player_Y_Position;
                if (delta >= g_DiffToHaltJump) {
                    g_VerticalForce = g_FallMForce;  /* switch to heavy gravity */
                }
            }
            if (g_SwimmingFlag) {
                /* ProcSwim: animation timing and the water-surface force
                 * are part of JumpSwimSub, before the shared LRAir path. */
                Player_GetAnimSpeed();
                if (g_Player_Y_Position < 0x14) {
                    g_VerticalForce = 0x18;
                }
                if (g_Left_Right_Buttons) {
                    g_PlayerFacingDir = g_Left_Right_Buttons;
                }
            }
            Player_AirMove();
            break;

        case PLAYER_STATE_FALL:
            /* FallingSub (player-movement.asm:48-51):
             * Set heavy gravity, then share LRAir with JUMP.
             */
            g_VerticalForce = g_FallMForce;
            Player_AirMove();
            break;

        default:
            Player_AirMove();
            break;

        case PLAYER_STATE_CLIMB:
            /* Reached only through the explicit climb path above. */
            break;
    }
}

/* PlayerPhysicsSub:ProcClimb (player-movement.asm:185-205).  Collision bits
 * are owned by PlayerBGCollision; until that producer is translated, a zero
 * mask correctly selects the idle-on-vine branch. */
static void Player_ClimbPhysics(void) {
    uint8_t index = 0;
    if ((g_Up_Down_Buttons & g_Player_CollisionBits) != 0) {
        index = (g_Up_Down_Buttons & BTN_UP) ? 1 : 2;
    }
    g_Player_Y_MoveForce = Climb_Y_MForceData[index];
    g_Player_Y_Speed = Climb_Y_SpeedData[index];
    g_PlayerAnimTimerSet = (g_Player_Y_Speed & 0x80) ? 0x08 : 0x04;
}

/* ClimbingSub (player-movement.asm:113-151).  The four adders preserve the
 * original facing/input-dependent horizontal correction and carry into the
 * page byte; no screen coordinate is used as object state. */
static void Player_ClimbingSub(void) {
    uint16_t force_sum = (uint16_t)g_Player_YMF_Dummy + g_Player_Y_MoveForce;
    uint8_t y_carry = (uint8_t)(force_sum >> 8);
    uint16_t y_sum;
    uint8_t add_index;

    g_Player_YMF_Dummy = (uint8_t)force_sum;
    y_sum = (uint16_t)g_Player_Y_Position + g_Player_Y_Speed + y_carry;
    g_Player_Y_Position = (uint8_t)y_sum;
    g_Player_Y_HighPos = (uint8_t)(g_Player_Y_HighPos +
                                   ((g_Player_Y_Speed & 0x80) ? 0xff : 0) +
                                   (uint8_t)(y_sum >> 8));

    if ((g_Left_Right_Buttons & g_Player_CollisionBits) == 0) {
        g_ClimbSideTimer = 0;
        return;
    }
    if (g_ClimbSideTimer != 0) return;
    g_ClimbSideTimer = 0x18;

    /* ClimbFD/CSetFDir choose right-facing/right-input, right-facing/
     * left-input, left-facing/right-input, left-facing/left-input. */
    if (g_Left_Right_Buttons & BTN_RIGHT) {
        add_index = (g_PlayerFacingDir == BTN_RIGHT) ? 0 : 1;
    } else {
        add_index = (g_PlayerFacingDir == BTN_RIGHT) ? 2 : 3;
    }
    pos_add_u8(&g_Player_PageLoc, &g_Player_X_Position,
               ClimbAdderLow[add_index]);
    g_Player_PageLoc = (uint8_t)(g_Player_PageLoc + ClimbAdderHigh[add_index]);
    g_PlayerFacingDir = g_Left_Right_Buttons ^ (BTN_LEFT | BTN_RIGHT);
}

/* ChkMoveDir (player-control.asm:51-57) runs after PlayerMovementSubs.
 * Keeping this at the PlayerCtrlRoutine boundary is important: X_Physics and
 * GetPlayerAnimSpeed must see the previous frame's Player_MovingDir. */
void Player_UpdateMovingDirection(void) {
    if ((uint8_t)g_Player_X_Speed >= 0x80) {
        g_Player_MovingDir = BTN_LEFT;
    } else if (g_Player_X_Speed != 0) {
        g_Player_MovingDir = BTN_RIGHT;
    }
}

/* ========================================================================
 * GRAVITY - Vertical physics
 * From main.asm: ImposeGravity
 * ======================================================================== */

static void Player_ImposeGravity(void) {
    /* MovePlayerVertically exits through ExXMove while JumpspringHandler
     * owns the player's vertical position. */
    if (g_JumpspringAnimCtrl != 0) return;
    SprObjectView object = {
        &g_Player_PageLoc, &g_Player_X_Position, &g_Player_Y_HighPos,
        &g_Player_Y_Position, &g_Player_X_Speed, &g_Player_X_MF2,
        &g_Player_Y_Speed, &g_Player_Y_MoveForce, &g_Player_YMF_Dummy,
        &g_Player_Rel_XPos, &g_Player_Rel_YPos, 0
    };
    SprObject_ImposeGravity(&object, g_VerticalForce, 0, 0x04, 0);
}

/* WhirlpoolActivate (main.asm:3449-3496) jumps directly to ImposeGravity
 * after setting downward force $10 and maximum speed $01. */
void Player_ApplyWhirlpoolGravity(void) {
    SprObjectView object = {
        &g_Player_PageLoc, &g_Player_X_Position, &g_Player_Y_HighPos,
        &g_Player_Y_Position, &g_Player_X_Speed, &g_Player_X_MF2,
        &g_Player_Y_Speed, &g_Player_Y_MoveForce, &g_Player_YMF_Dummy,
        &g_Player_Rel_XPos, &g_Player_Rel_YPos, 0
    };
    SprObject_ImposeGravity(&object, 0x10, 0, 0x01, 0);
}

/* ========================================================================
 * FRICTION - Horizontal acceleration/deceleration
 * From player-movement.asm: ImposeFriction
 *
 * NES logic:
 *   - RIGHT button: ADD friction adder (ADC) → accelerates right
 *   - LEFT button or decelerating: SUBTRACT friction adder (SBC) → decelerates or goes left
 *   - Friction value is 16-bit (Low, High), doubled when skidding
 * ======================================================================== */

/* Friction adder selected by X_Physics (player-movement.asm:289-356).
 * Runs BEFORE the state dispatch, so the skid doubling compares the
 * PREVIOUS frame's facing (the state subs update facing later). */
static uint8_t s_FrictionLow, s_FrictionHigh;

static void Player_CalcFriction(void) {
    uint8_t buttons_lr = g_Left_Right_Buttons;
    uint8_t max_spd_idx = 0;
    uint8_t friction_idx = 0;
    uint8_t apply_index_increments = 1;

    if (g_Player_State == PLAYER_STATE_GROUND) {
        max_spd_idx = (g_AreaType == AREA_TYPE_WATER) ? 1 : 0;
        /* X_Physics (player-movement.asm:299-321): water branches directly
         * to ChkRFast and therefore never enters ProcPRun.  RunningTimer
         * and B-button handling belong only to non-water ground movement. */
        if (g_AreaType != AREA_TYPE_WATER &&
            buttons_lr == g_Player_MovingDir) {
            if (g_A_B_Buttons & BTN_B) {
                g_RunningTimer = 0x0A;
                apply_index_increments = 0; /* SetRTmr → GetXPhy */
            } else if (g_RunningTimer != 0) {
                apply_index_increments = 0;
            }
        }
        if (apply_index_increments) {
            max_spd_idx++;
            friction_idx++;
            if (g_RunningSpeed || g_Player_XSpeedAbsolute >= 0x21)
                friction_idx++;
        }
    } else if (g_Player_XSpeedAbsolute < 0x19) {
        max_spd_idx = 1;
        friction_idx = 1;
        if (g_RunningSpeed || g_Player_XSpeedAbsolute >= 0x21)
            friction_idx++;
    }

    /* Set max speeds from table index */
    /* GetXPhy loads MaximumLeftSpeed before the PlayerEntrance override
     * changes Y for MaximumRightSpeed (player-movement.asm:337-348). */
    g_MaximumLeftSpeed = MaxLeftXSpdData[max_spd_idx];
    {
        uint8_t right_idx = (g_GameEngineSubroutine == 0x07) ? 3 : max_spd_idx;
        g_MaximumRightSpeed = MaxRightXSpdData[right_idx];
    }

    /* Get friction value */
    s_FrictionLow = FrictionData[friction_idx];
    s_FrictionHigh = 0;

    /* Double friction when facing != moving direction (skidding)
     * Uses the facing value from BEFORE this frame's state sub runs */
    if (g_PlayerFacingDir != g_Player_MovingDir) {
        uint8_t carry = (s_FrictionLow >> 7) & 1;
        s_FrictionLow <<= 1;
        s_FrictionHigh = (uint8_t)((s_FrictionHigh << 1) | carry);
    }
}

/* ImposeFriction (player-movement.asm:389-433): apply the precomputed
 * adder to MoveForce/X_Speed with clamping, then update absolute speed
 * and moving direction. */
static void Player_ImposeFriction(void) {
    uint8_t speed = (uint8_t)g_Player_X_Speed;
    uint8_t friction_low = s_FrictionLow, friction_high = s_FrictionHigh;
    uint8_t buttons_lr = g_Left_Right_Buttons & g_Player_CollisionBits;
    uint8_t do_add;

    /* Determine direction of friction application */
    if (buttons_lr == 0 && speed == 0) {
        g_Player_XSpeedAbsolute = 0;
        return;
    }
    if (buttons_lr == 0) {
        /* No directional input: decelerate toward zero */
        if (speed == 0) {
            do_add = 0;
        } else if (speed < 0x80) {
            do_add = 0;  /* Moving right → subtract to slow down (RghtFrict) */
        } else {
            do_add = 1;  /* Moving left → add to slow down (LeftFrict) */
        }
    } else if (buttons_lr & BTN_RIGHT) {
        do_add = 1;  /* RIGHT pressed → add (LeftFrict path) */
    } else {
        do_add = 0;  /* LEFT pressed → subtract (RghtFrict path) */
    }

    /* Apply friction */
    if (do_add) {
        int16_t result = (int16_t)g_Player_X_MoveForce + (int16_t)friction_low;
        uint8_t carry = (result > 0xFF) ? 1 : 0;
        g_Player_X_MoveForce += friction_low;
        g_Player_X_Speed = g_Player_X_Speed + friction_high + carry;

        /* Clamp to max right speed
         * NES: cmp MaximumRightSpeed; bmi XSpdSign
         * bmi branches when speed < max (unsigned), so clamp when speed >= max
         * NES does NOT reset MoveForce on clamp — only Speed is clamped */
        if (g_Player_X_Speed >= g_MaximumRightSpeed && g_Player_X_Speed < 0x80) {
            g_Player_X_Speed = g_MaximumRightSpeed;
        }
    } else {
        /* RghtFrict: SBC — MoveForce -= friction, Speed -= friction_high + borrow */
        uint8_t borrow = (g_Player_X_MoveForce < friction_low) ? 1 : 0;
        g_Player_X_MoveForce -= friction_low; // the carry
        g_Player_X_Speed = g_Player_X_Speed - friction_high - borrow;

        /* Clamp to max left speed — NES does NOT reset MoveForce */
        if (g_Player_X_Speed < g_MaximumLeftSpeed && g_Player_X_Speed >= 0x80) {
            g_Player_X_Speed = g_MaximumLeftSpeed;
        }
    }

    /* Calculate absolute speed (XSpdSign) */
    speed = g_Player_X_Speed;
    if (speed >= 0x80) {
        /* Negative speed: two's complement to get absolute */
        g_Player_XSpeedAbsolute = (~speed + 1) & 0xFF;
    } else {
        g_Player_XSpeedAbsolute = speed;
    }

}

/* ========================================================================
 * HORIZONTAL MOVEMENT - Position update with page crossing
 * From main.asm: MoveObjectHorizontally
 * ======================================================================== */

static void Player_MoveHorizontally(void) {
    /* MovePlayerHorizontally has the same JumpspringAnimCtrl early exit as
     * MovePlayerVertically; the spring handler owns both position updates. */
    if (g_JumpspringAnimCtrl != 0) {
        /* MovePlayerHorizontally (main.asm:4527-4530) returns through
         * ExXMove immediately after loading JumpspringAnimCtrl.  The caller
         * stores that live A value in Player_X_Scroll; it is not a synthetic
         * zero-motion result. */
        g_Player_X_Scroll = g_JumpspringAnimCtrl;
        return;
    }
    SprObjectView object = {
        &g_Player_PageLoc, &g_Player_X_Position, &g_Player_Y_HighPos,
        &g_Player_Y_Position, &g_Player_X_Speed, &g_Player_X_MF2,
        &g_Player_Y_Speed, &g_Player_Y_MoveForce, &g_Player_YMF_Dummy,
        &g_Player_Rel_XPos, &g_Player_Rel_YPos, 0
    };
    g_Player_X_Scroll = SprObject_MoveHorizontally(&object);
}

/* ========================================================================
 * ANIMATION - Get speed and skid stop (BEFORE friction)
 * NES: GetPlayerAnimSpeed (player-movement.asm:353-385)
 *
 * IMPORTANT: This must be called BEFORE ImposeFriction in the GROUND state,
 * matching NES OnGroundStateSub call order. The skid-stop logic here zeroes
 * Player_X_Speed and Player_X_MoveForce; doing this after friction would
 * destroy sub-pixel accumulation across frames.
 * ======================================================================== */

void Player_GetAnimSpeed(void) {
    /* GetPlayerAnimSpeed (player-movement.asm:353-385) */
    uint8_t y;

    if (g_Player_XSpeedAbsolute >= 0x1C) {
        /* SetRunSpd: speed >= $1C → RunningSpeed = speed value (non-zero flag).
         * NES: lda XSpeedAbsolute; cmp #$1c; bcs SetRunSpd
         * A still holds speed value when sta RunningSpeed executes.
         * ChkSkid is bypassed entirely.
         */
        g_RunningSpeed = g_Player_XSpeedAbsolute;
        y = 0;
    } else {
        /* Speed < $1C: determine tier index */
        y = 1;
        if (g_Player_XSpeedAbsolute < 0x0E) {
            y = 2;
        }

        /* ChkSkid: only process if some button (besides A) is pressed.
         * NES: lda SavedJoypadBits; and #%01111111; beq SetAnimSpd
         */
        if ((g_SavedJoypadBits & 0x7F) != 0) {
            /* GetPlayerAnimSpeed reloads SavedJoypadBits and masks it with
             * #$03 (player-movement.asm:363-367).  This is deliberately not
             * Left_Right_Buttons: PlayerCtrlRoutine may have cleared that
             * latch after a ground DOWN press, while the raw saved RIGHT or
             * LEFT bit still selects ProcSkid. */
            uint8_t raw_lr = g_SavedJoypadBits & (BTN_LEFT | BTN_RIGHT);
            if (raw_lr == g_Player_MovingDir) {
                /* Moving in held direction → RunningSpeed = 0 (not running flag) */
                g_RunningSpeed = 0;
            } else {
                /* ProcSkid: pressing direction opposite to movement */
                if (g_Player_XSpeedAbsolute < 0x0B) {
                    /* Skid stop: speed too low, snap direction and zero speed */
                    g_Player_MovingDir = g_PlayerFacingDir;
                    g_Player_X_Speed = 0;
                    g_Player_X_MoveForce = 0;
                }
                /* RunningSpeed unchanged (latches previous value during skid) */
            }
        }
        /* else: no buttons pressed — RunningSpeed unchanged */
    }

    /* SetAnimSpd: store animation timer setting */
    g_PlayerAnimTimerSet = PlayerAnimTmrData[y];
}

int Player_LoadPhysicsTables(void) {
    if (Assets_Copy("tables/max_right_x_spd.bin", MaxRightXSpdData, sizeof(MaxRightXSpdData)) ||
        Assets_Copy("tables/max_left_x_spd.bin", MaxLeftXSpdData, sizeof(MaxLeftXSpdData)) ||
        Assets_Copy("tables/friction.bin", FrictionData, sizeof(FrictionData)) ||
        Assets_Copy("tables/jump_mforce.bin", JumpMForceData, sizeof(JumpMForceData)) ||
        Assets_Copy("tables/fall_mforce.bin", FallMForceData, sizeof(FallMForceData)) ||
        Assets_Copy("tables/player_y_spd.bin", PlayerYSpdData, sizeof(PlayerYSpdData)) ||
        Assets_Copy("tables/init_mforce.bin", InitMForceData, sizeof(InitMForceData)) ||
        Assets_Copy("tables/climb_y_speed.bin", Climb_Y_SpeedData, sizeof(Climb_Y_SpeedData)) ||
        Assets_Copy("tables/climb_y_mforce.bin", Climb_Y_MForceData, sizeof(Climb_Y_MForceData)) ||
        Assets_Copy("tables/climb_adder_low.bin", ClimbAdderLow, sizeof(ClimbAdderLow)) ||
        Assets_Copy("tables/climb_adder_high.bin", ClimbAdderHigh, sizeof(ClimbAdderHigh)) ||
        Assets_Copy("tables/player_anim_tmr.bin", PlayerAnimTmrData, sizeof(PlayerAnimTmrData)))
        return -1;
    return 0;
}
