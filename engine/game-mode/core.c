/* engine/game-mode/core.c - GameCoreRoutine (from core.asm, routines.asm)
 *
 * NES structure:
 *   GameCoreRoutine:
 *     1. master joypad bits <- current player's bits
 *     2. GameRoutines dispatch on GameEngineSubroutine ($0E)
 *     3. if OperMode_Task >= 3: GameEngine (player gfx, block objects,
 *        game timer, color rotation, area parser scroll update)
 *
 * GameRoutines jump table (routines.asm):
 *   0 Entrance_GameTimerSetup, 1 Vine_AutoClimb, 2 SideExitPipeEntry,
 *   3 VerticalPipeEntry, 4 FlagpoleSlide, 5 PlayerEndLevel, 6 PlayerLoseLife,
 *   7 PlayerEntrance, 8 PlayerCtrlRoutine, 9 PlayerChangeSize,
 *   10 PlayerInjuryBlink, 11 PlayerDeath, 12 PlayerFireFlower
 */

#include "constants/defs.h"
#include "system/platform.h"
#include "opermode.h"
#include "scroll.h"
#include "player/player.h"
#include "enemy/enemy.h"
#include "misc.h"
#include "collision.h"
#include "level/level.h"
#include "screen/routine/area_parser.h"
#include "screen/routine/hud.h"
#include "screen/routine/colors.h"
#include "score.h"
#include "assets.h"
#include "constants/globals.h"
#include "core.h"
#include <string.h>

static void Sub_ChangeAreaMode(void);
static void Sub_NextArea(void);
static void Sub_EnterSidePipe(void);
static void Sub_VineAutoClimb(void);
static void Sub_FlagpoleSlide(void);
static void Sub_PlayerEndLevel(void);

/* PlayerHole (player-control.asm:78-121) is the post-collision consumer of
 * Player_Y_HighPos.  Its flag in X distinguishes a cloud exit (zero) from a
 * death/life-loss transition (one).  EventMusicBuffer remains an explicit
 * audio-owned RAM input; the SDL audio layer does not synthesize or drain it. */
static void Player_Hole(void) {
    uint8_t death = 0;
    uint8_t threshold = 0x04;

    /* CMP #$02 / BMI uses the 6502 N flag, not a saturating comparison. */
    if ((uint8_t)(g_Player_Y_HighPos - 0x02) & 0x80) return;

    g_ScrollLock = 1; /* ScrollLock ($0714) */

    if (g_GameTimerExpiredFlag != 0) {
        death = 1;
    } else if (Level_CloudTypeOverride() == 0) {
        death = 1;
    }

    if (death != 0) {
        /* HoleDie skips the music-load branch for PlayerDeath ($0b), but
         * retains it for other hole/time-up callers. */
        if (g_GameEngineSubroutine != 0x0b) {
            if (g_DeathMusicLoaded == 0) {
                g_EventMusicQueue = DeathMusic;
                g_DeathMusicLoaded = 1;
            }
            threshold = 0x06;
        }
    }

    if ((uint8_t)(g_Player_Y_HighPos - threshold) & 0x80) return;

    if (death == 0) {
        /* CloudExit (player-control.asm:116-121) calls SetEntr, which is the
         * shared SetEntr label in vine-climb.asm.  It stores $02, performs
         * ChgAreaMode, then CloudExit increments the mode to $03. */
        g_JoypadOverride = 0;
        g_AltEntranceControl = 2;
        Sub_ChangeAreaMode();
        g_AltEntranceControl++;
        return;
    }

    /* The APU consumer is stubbed, so EventMusicBuffer is normally zero;
     * retain the original wait gate for any future queue producer. */
    if (g_EventMusicBuffer != 0) return;
    g_GameEngineSubroutine = 0x06; /* PlayerLoseLife next frame */
}

/* Entrance_GameTimerSetup data (game-timer-setup.asm) */
static uint8_t PlayerStarting_X_Pos[4];
static uint8_t AltYPosOffset[2];
static uint8_t PlayerStarting_Y_Pos[9];
static uint8_t PlayerBGPriorityData[9];
static uint8_t GameTimerData[4];
static uint8_t AreaMusicSelectData[6];
static uint8_t HalfwayPageNybbles[16];
static uint8_t Hidden1UpCoinAmts[8];

int GameMode_LoadRom(void) {
    uint8_t bg_priority[8];

    if (Assets_Copy("tables/player_starting_x.bin", PlayerStarting_X_Pos,
                    sizeof(PlayerStarting_X_Pos)) ||
        Assets_Copy("tables/alt_y_pos_offset.bin", AltYPosOffset,
                    sizeof(AltYPosOffset)) ||
        Assets_Copy("tables/player_starting_y.bin", PlayerStarting_Y_Pos,
                    sizeof(PlayerStarting_Y_Pos)) ||
        Assets_Copy("tables/player_bg_priority.bin", bg_priority,
                    sizeof(bg_priority)) ||
        Assets_Copy("tables/game_timer.bin", GameTimerData,
                    sizeof(GameTimerData)) ||
        Assets_Copy("tables/music_select.bin", AreaMusicSelectData,
                    sizeof(AreaMusicSelectData)) ||
        Assets_Copy("tables/halfway_page_nybbles.bin", HalfwayPageNybbles,
                    sizeof(HalfwayPageNybbles)) ||
        Assets_Copy("tables/hidden_1up_coin_amts.bin", Hidden1UpCoinAmts,
                    sizeof(Hidden1UpCoinAmts)))
        return -1;
    memcpy(PlayerBGPriorityData, bg_priority, sizeof(bg_priority));
    PlayerBGPriorityData[8] = GameTimerData[0];
    return 0;
}

/* Level header values (parsed by level.c) */
extern uint8_t Level_TimerSetting(void);
extern uint8_t Level_PlayerEntranceCtrl(void);
extern uint8_t Level_CloudTypeOverride(void);

/* GetAreaMusic (main.asm:1563-1583).  This writes only the original queue
 * byte; the APU/music playback layer is intentionally excluded. */
void GameMode_UpdateAreaMusicQueue(void) {
    uint8_t selection;

    if (g_OperMode == TITLE_SCREEN_MODE) return;
    if (g_AltEntranceControl != 0x02 &&
        (g_PlayerEntranceCtrl == 0x06 || g_PlayerEntranceCtrl == 0x07)) {
        selection = 5;
    } else if (Level_CloudTypeOverride() != 0) {
        selection = 4;
    } else {
        selection = (uint8_t)(g_AreaType & 0x03);
    }
    g_AreaMusicQueue = AreaMusicSelectData[selection];
}

/* Subroutine 0: Entrance_GameTimerSetup */
static void Sub_EntranceGameTimerSetup(void) {
    uint8_t entrance = g_PlayerEntranceCtrl;
    uint8_t alt = g_AltEntranceControl;
    uint8_t y_index = entrance;
    uint8_t setup_bubble_slot;

    Player_Init(); /* InitializeArea cleared the object RAM; this owns the
                    * named entrance fields written by the ASM routine. */
    g_Player_PageLoc = g_ScreenLeft_PageLoc;
    g_FallMForce = 0x28; /* VerticalForceDown, game-timer-setup.asm:24 */
    g_PlayerFacingDir = BTN_RIGHT;
    g_Player_Y_HighPos = 1;
    g_Player_State = PLAYER_STATE_GROUND;
    g_HalfwayPage = 0;
    /* Entrance_GameTimerSetup writes HalfwayPage ($075b) directly after
     * InitializeMemory.  The active seven-byte player record is the physical
     * owner of that address in the C state projection. */
    g_OnscreenPlayerInfo[1] = 0;
    g_Player_CollisionBits--; /* Entrance_GameTimerSetup: DEC from cleared RAM */
    g_SwimmingFlag = (g_AreaType == AREA_TYPE_WATER) ? 1 : 0;

    /* PlayerStarting_X_Pos is indexed by AltEntranceControl.  The original
     * routine keeps PlayerEntranceCtrl as the Y/priority index for normal
     * entries, but replaces it with AltYPosOffset-2 for alternate entries
     * (game-timer-setup.asm:38-50). */
    if (alt > 1) {
        uint8_t alt_y = (uint8_t)(alt - 2);
        y_index = alt_y < sizeof(AltYPosOffset)
            ? AltYPosOffset[alt_y] : 0;
    }
    if (alt >= sizeof(PlayerStarting_X_Pos)) alt = 0;
    if (y_index >= sizeof(PlayerStarting_Y_Pos)) y_index = 0;
    g_Player_X_Position = PlayerStarting_X_Pos[alt];
    g_Player_Y_Position = PlayerStarting_Y_Pos[y_index];
    g_Player_SprAttrib = PlayerBGPriorityData[y_index];

    {
        /* GetPlayerColors reloads the original X register from
         * VRAM_Buffer1_Offset before returning (colors.asm:36-75).  The
         * following SetupBubble therefore receives that raw X value, not
         * PlayerEntranceCtrl. */
        setup_bubble_slot = g_VRAM_Buffer1_Offset;
        GetPlayerColors();
    }
    {
        /* Game timer init: display = GameTimerData[setting], last digit 1 */
        uint8_t setting = Level_TimerSetting();
        if (setting != 0 && g_FetchNewGameTimerFlag) {
            /* GameTimerDisplay = $07E8 = DisplayDigits[23..25] region:
             * digits "4","0","0" style layout (hundreds, tens, ones) */
            HUD_SetGameTimer(GameTimerData[setting]);
            g_FetchNewGameTimerFlag = 0;
            g_StarInvincibleTimer = 0;
        }
    }

    /* ChkOverR -> Setup_Vine (game-timer-setup.asm:64-76).  The constructor
     * owns the reserved enemy slot and the VineFlagOffset/VineObjOffset
     * state; VineObjectHandler advances that object during GameEngine. */
    if (g_JoypadOverride != 0) {
        g_Player_State = PLAYER_STATE_CLIMB;
        Enemy_SetupEntranceVine();
    }

    /* ChkSwimE -> SetupBubble (game-timer-setup.asm:78-82).  Normal water
     * entries retain GetPlayerColors' restored X value.  After Setup_Vine the
     * ROM leaves X=$05, so mixed water+vine entries use the explicit
     * bubble/enemy RAM overlay rather than a fourth bubble record. */
    if (g_AreaType == AREA_TYPE_WATER) {
        if (g_JoypadOverride != 0)
            Misc_SetupEntranceBubbleAlias();
        else
            Misc_SetupEntranceBubble(setup_bubble_slot);
    }
    g_GameEngineSubroutine = 7; /* PlayerEntrance next frame */
}

/* Subroutine 7: PlayerEntrance (player-entrance.asm).  The alternate-entry
 * state is persistent RAM, not a route-specific animation: the same
 * PlayerEntranceCtrl, JoypadOverride, VineHeight, and collision-disable
 * bytes select the pipe/vine/normal branches on every frame. */
static void Sub_PlayerEntrance(void) {
    uint8_t input;

    if (g_AltEntranceControl == 2) {
        if (g_JoypadOverride == 0) {
            /* EntrMode2: MovePlayerYAxis(-1), then become ready once the
             * pipe exit has risen above the original $91 threshold. */
            g_Player_Y_Position--;
            if (g_Player_Y_Position >= 0x91) return;
        } else {
            /* VineEntr: Setup_Vine/VineObjectHandler owns VineHeight.  Once
             * the producer reaches $60, the player leaves the vine under
             * AutoControlPlayer; the $99 split is the original transition
             * between walking off and climbing. */
            if (g_VineHeight != 0x60) return;
            input = BTN_RIGHT;
            g_DisableCollisionDet = 0;
            if (g_Player_Y_Position >= 0x99) {
                g_Player_State = PLAYER_STATE_CLIMB;
                /* VineEntr (player-entrance.asm:42-50) writes the
                 * climb-forcing marker before AutoControlPlayer.  The ASM
                 * leaves A=$08 on this path, so the shared entry receives
                 * Up_Dir rather than the $01 walk-off input. */
                Level_WriteBlockBufferAddress(0x0500u + 0xb4u, 0x08);
                g_DisableCollisionDet = 1;
                input = BTN_UP;
            }
            GameMode_AutoControl(input);
            if (g_Player_X_Position < 0x48) return;
        }

        /* PlayerRdy is shared by all entry modes. */
        g_GameEngineSubroutine = 8;
        g_PlayerFacingDir = BTN_RIGHT;
        g_AltEntranceControl = 0;
        g_DisableCollisionDet = 0;
        g_JoypadOverride = 0;
        return;
    }

    if (g_Player_Y_Position < 0x30) {
        /* AutoControlPlayer with A=0 while the player is above the playfield. */
        GameMode_AutoControl(0);
        return;
    }

    if (g_PlayerEntranceCtrl == 0x06 || g_PlayerEntranceCtrl == 0x07) {
        if (g_Player_SprAttrib == 0) {
            /* ChkBehPipe: force the first rightward step before the pipe
             * intro has established its priority attribute. */
            GameMode_AutoControl(BTN_RIGHT);
            return;
        }
        Sub_EnterSidePipe();
        g_ChangeAreaTimer--;
        if (g_ChangeAreaTimer != 0) return;
        g_DisableIntermediate++;
        /* NextArea owns LoadAreaPointer → FetchNewGameTimerFlag →
         * ChgAreaMode → HalfwayPage/Silence in that order. */
        Sub_NextArea();
        return;
    }

    /* PlayerRdy */
    g_GameEngineSubroutine = 8;
    g_PlayerFacingDir = BTN_RIGHT;
    g_AltEntranceControl = 0;
    g_DisableCollisionDet = 0;
    g_JoypadOverride = 0;
}

static void Sub_PlayerCtrlRoutine(void);

/* ChkMoveDir's Player_BoundBoxCtrl write (player-control.asm:42-50).
 * PlayerSize is the NES encoding (small=$01, big=$00); crouching big Mario
 * selects the third player entry in BoundBoxCtrlData. */
static void Player_UpdateBoundBoxCtrl(void) {
    if (g_PlayerSize != PLAYER_SIZE_SMALL) {
        g_Player_BoundBoxCtrl = g_CrouchingFlag ? 0x02 : 0x00;
    } else {
        g_Player_BoundBoxCtrl = 0x01;
    }
}

/* DonePlayerTask (main.asm:11815-11821). */
static void Sub_DonePlayerTask(void) {
    g_TimerControl = 0;
    g_GameEngineSubroutine = 8;
}

/* ChgAreaPipe/ChgAreaMode (side-pipe-entry.asm:10-19).  This owns the
 * screen/task transition; the actual side-pipe object movement remains in
 * the supported PlayerCtrlRoutine path below. */
static void Sub_ChangeAreaPipe(uint8_t entrance_mode) {
    /* DEC/BEQ semantics: an unset timer wraps to $FF and does not
     * transition, rather than being treated as an already-expired timer. */
    g_ChangeAreaTimer--;
    if (g_ChangeAreaTimer != 0) return;
    g_AltEntranceControl = entrance_mode;
    g_DisableScreenFlag++;
    g_OperMode_Task = 0;
    g_Sprite0HitDetectFlag = 0;
}

/* ChgAreaMode (side-pipe-entry.asm:10-19), shared by Vine_AutoClimb and
 * the supported PlayerEntrance pipe-intro boundary. */
static void Sub_ChangeAreaMode(void) {
    g_DisableScreenFlag++;
    g_OperMode_Task = 0;
    g_Sprite0HitDetectFlag = 0;
}

/* NextArea (player-end-level.asm:38-49, player-entrance.asm:24-26).
 * LoadAreaPointer must finish before the fetch-timer latch is incremented;
 * ChgAreaMode then clears the task/sprite-zero state before its zero result
 * is stored in HalfwayPage and Silence is queued.  These are the original
 * transition RAM owners, not a route or presentation shortcut. */
static void Sub_NextArea(void) {
    (void)Level_AdvanceArea();
    /* NextArea's INC AreaNumber targets the active record byte at $0760;
     * Level_AdvanceArea updates the scalar used by the packed address table. */
    g_OnscreenPlayerInfo[6] = g_AreaNumber;
    Sub_ChangeAreaMode();
    g_HalfwayPage = 0;
    /* ChgAreaMode leaves A=$00; PlayerEndLevel stores it as HalfwayPage at
     * $075b before queuing Silence. */
    g_OnscreenPlayerInfo[1] = 0;
    g_EventMusicQueue = Silence;
}

/* EnterSidePipe (side-pipe-entry.asm:17-31). */
static void Sub_EnterSidePipe(void) {
    uint8_t input = 0;

    g_Player_X_Speed = 8;
    if ((g_Player_X_Position & 0x0f) != 0) {
        input = BTN_RIGHT;
    } else {
        g_Player_X_Speed = 0;
    }
    GameMode_AutoControl(input);
}

/* SideExitPipeEntry (side-pipe-entry.asm:2-19). */
static void Sub_SideExitPipeEntry(void) {
    Sub_EnterSidePipe();
    Sub_ChangeAreaPipe(2);
}

/* VerticalPipeEntry (vertical-pipe-entry.asm:2-19). */
static void Sub_VerticalPipeEntry(void) {
    g_Player_Y_Position++;
    Scroll_Update();
    if (g_WarpZoneControl != 0) {
        Sub_ChangeAreaPipe(0);
    } else if (g_AreaType == AREA_TYPE_CASTLE) {
        Sub_ChangeAreaPipe(2);
    } else {
        Sub_ChangeAreaPipe(1);
    }
}

/* Vine_AutoClimb (vine-climb.asm:2-16).  The vine object producer remains
 * deferred, but this consumer owns the exact height/high-byte split and the
 * transition into alternate entrance mode. */
static void Sub_VineAutoClimb(void) {
    if (g_Player_Y_HighPos != 0 || g_Player_Y_Position >= 0xe4) {
        g_JoypadOverride = BTN_UP;
        g_Player_State = PLAYER_STATE_CLIMB;
        GameMode_AutoControl(BTN_UP);
        return;
    }

    g_AltEntranceControl = 2;
    Sub_ChangeAreaMode();
}

/* PlayerChangeSize/InitChangeSize (player-change-size.asm:2-24). */
static void Sub_PlayerChangeSize(void) {
    if (g_TimerControl == 0xf8) {
        if (g_PlayerChangeSizeFlag == 0) {
            g_PlayerAnimCtrl = 0;
            g_PlayerChangeSizeFlag++;
            g_PlayerSize ^= 1;
        }
    } else if (g_TimerControl == 0xc4) {
        Sub_DonePlayerTask();
    }
}

/* PlayerInjuryBlink (player-injury-blink.asm:2-12). */
static void Sub_PlayerInjuryBlink(void) {
    /* PlayerInjuryBlink (player-injury-blink.asm:2-20) falls through
     * ExitBlink into InitChangeSize when TimerControl is exactly $f0.
     * Values above $f0 take ExitBoth; the equality is the injury sequence's
     * size-toggle boundary, not an early-return case. */
    if (g_TimerControl > 0xf0) return;
    if (g_TimerControl == 0xf0) {
        if (g_PlayerChangeSizeFlag != 0) return;
        g_PlayerAnimCtrl = 0;
        g_PlayerChangeSizeFlag++;
        g_PlayerSize ^= 1;
        return;
    }
    if (g_TimerControl == 0xc8) {
        Sub_DonePlayerTask();
    } else {
        Sub_PlayerCtrlRoutine();
    }
}

/* PlayerDeath (player-death.asm:3-13). */
static void Sub_PlayerDeath(void) {
    if (g_TimerControl < 0xf0) Sub_PlayerCtrlRoutine();
}

/* PlayerFireFlower/ResetPalFireFlower (player-fire-flower.asm:2-25).
 * Sound queue and APU playback are intentionally excluded. */
static void Sub_PlayerFireFlower(void) {
    uint8_t palette_bits;

    if (g_TimerControl == 0xc0) {
        g_Player_SprAttrib &= 0xfc;
        Sub_DonePlayerTask();
        return;
    }
    palette_bits = (uint8_t)((g_FrameCounter >> 2) & 0x03);
    g_Player_SprAttrib = (uint8_t)((g_Player_SprAttrib & 0xfc) |
                                   palette_bits);
}

/* CyclePlayerPalette/ResetPalStar (player-fire-flower.asm:10-26) is the
 * second palette consumer in GameEngine, after ColorRotation.  The star
 * timer selects the cadence: while StarInvincibleTimer is at least $08 the
 * player palette advances from FrameCounter/$02; during the final seven
 * ticks it advances from FrameCounter/$08.  A zero timer takes the
 * ResetPalStar path and clears only the two palette bits in
 * Player_SprAttrib=$03c5, preserving priority and flip attributes. */
static void GameMode_UpdatePlayerPalette(void) {
    uint8_t palette_bits;

    if (g_StarInvincibleTimer == 0) {
        g_Player_SprAttrib &= (uint8_t)~0x03;
        return;
    }

    palette_bits = g_FrameCounter;
    if (g_StarInvincibleTimer < 0x08)
        palette_bits >>= 2;
    palette_bits >>= 1;
    g_Player_SprAttrib = (uint8_t)((g_Player_SprAttrib & 0xfc) |
                                   (palette_bits & 0x03));
}

/* PlayerLoseLife/ContinueGame (main.asm:1603-1704).  The C scalar aliases
 * are kept in sync with the two seven-byte records because the gameplay
 * routines use named globals while the original 6502 uses the records at
 * $075A/$0761 directly. */

/* Store the C aliases into the active OnscreenPlayerInfo record.  This is a
 * representation boundary only: on the NES these values are the same bytes
 * as NumberofLives/HalfwayPage/... at $075A-$0760. */
static void StoreCurrentPlayerInfo(void) {
    g_OnscreenPlayerInfo[0] = g_NumberofLives;
    g_OnscreenPlayerInfo[1] = g_HalfwayPage;
    g_OnscreenPlayerInfo[2] = g_LevelNumber;
    g_OnscreenPlayerInfo[3] = g_Hidden1UpFlag;
    g_OnscreenPlayerInfo[4] = g_CoinTally;
    g_OnscreenPlayerInfo[5] = g_WorldNumber;
    g_OnscreenPlayerInfo[6] = g_AreaNumber;
}

static void LoadCurrentPlayerInfo(void) {
    g_NumberofLives = g_OnscreenPlayerInfo[0];
    g_HalfwayPage = g_OnscreenPlayerInfo[1];
    g_LevelNumber = g_OnscreenPlayerInfo[2];
    g_Hidden1UpFlag = g_OnscreenPlayerInfo[3];
    g_CoinTally = g_OnscreenPlayerInfo[4];
    g_WorldNumber = g_OnscreenPlayerInfo[5];
    g_AreaNumber = g_OnscreenPlayerInfo[6];
}

/* TransposePlayers (main.asm:1706-1726).  A return value of zero represents
 * the 6502 carry-set exit; one represents the clear-carry swap path. */
int GameMode_TransposePlayers(void) {
    int i;
    uint8_t tmp;

    StoreCurrentPlayerInfo();
    if (g_NumberOfPlayers == 0 || g_OffscreenPlayerInfo[0] >= 0x80)
        return 0;

    g_CurrentPlayer ^= 1;
    for (i = 6; i >= 0; i--) {
        tmp = g_OnscreenPlayerInfo[i];
        g_OnscreenPlayerInfo[i] = g_OffscreenPlayerInfo[i];
        g_OffscreenPlayerInfo[i] = tmp;
    }
    LoadCurrentPlayerInfo();
    return 1;
}

/* ContinueGame (main.asm:1691-1704).  LoadAreaPointer owns the world/area
 * lookup and header load before the game-mode task is restarted. */
void GameMode_ContinueGame(void) {
    /* ContinueGame calls LoadAreaPointer, not GetAreaDataAddrs.  Preserve
     * AreaDataOffset and the parser's physical slot state until the next
     * InitializeArea, as in main.asm:1691-1704. */
    (void)Level_SelectAreaPointer(g_WorldNumber, g_AreaNumber);
    g_PlayerSize = PLAYER_SIZE_SMALL;
    g_FetchNewGameTimerFlag++;
    g_TimerControl = 0;
    g_PlayerStatus = PLAYER_STATUS_SMALL;
    g_GameEngineSubroutine = 0;
    g_OperMode_Task = 0;
    g_OperMode = GAME_MODE;
}

/* TerminateGame (main.asm:1677-1704) is shared by RunGameOver and the
 * world-8 PlayerEndWorld branch.  TransposePlayers owns the clear-carry
 * decision; the SDL aliases still commit the active seven-byte player record
 * before that test, just as the NES RAM does. */
void GameMode_TerminateGame(void) {
    g_EventMusicQueue = Silence; /* queue state only; APU playback excluded */
    if (GameMode_TransposePlayers()) {
        GameMode_ContinueGame();
        return;
    }

    g_ContinueWorld = g_WorldNumber;
    g_OperMode_Task = 0;
    g_ScreenTimer = 0;
    g_OperMode = TITLE_SCREEN_MODE;
}

static void Sub_PlayerLoseLife(void) {
    uint8_t index;
    uint8_t halfway;

    StoreCurrentPlayerInfo();
    g_DisableScreenFlag++;
    g_Sprite0HitDetectFlag = 0;
    g_EventMusicQueue = Silence;
    /* The ROM performs DEC NumberofLives first, then uses BMI.  This makes
     * $FF the terminal value and preserves the three-life display sequence
     * from the initial value $02. */
    g_NumberofLives--;
    g_OnscreenPlayerInfo[0] = g_NumberofLives;
    if (g_NumberofLives & 0x80) {
        g_OperMode_Task = 0;
        g_OperMode = GAME_OVER_MODE;
        return;
    }

    index = (uint8_t)((g_WorldNumber & 0x07) * 2);
    if (g_LevelNumber & 0x02) index++;
    halfway = HalfwayPageNybbles[index];
    if ((g_LevelNumber & 1) == 0) halfway >>= 4;
    halfway &= 0x0f;
    /* GetHalfway uses CMP ScreenLeft_PageLoc followed by BEQ/BCC:
     * accept the table value only when it is <= the current screen page;
     * otherwise restart from page zero. */
    g_HalfwayPage = (halfway <= g_ScreenLeft_PageLoc) ? halfway : 0;

    /* ContinueGame follows TransposePlayers; a one-player game and a
     * two-player game with no reserve both retain the current record. */
    (void)GameMode_TransposePlayers();
    GameMode_ContinueGame();
    g_PlayerChangeSizeFlag = 0;
}

/* FlagpoleSlide (engine/game-mode/routine/flagpole-slide.asm). */
static void Sub_FlagpoleSlide(void) {
    uint8_t input = 0;

    if (!Enemy_FlagpoleSlideInput(&input)) {
        g_GameEngineSubroutine++;
        return;
    }
    GameMode_AutoControl(input);
}

/* PlayerEndLevel (engine/game-mode/routine/player-end-level.asm).  The
 * seven-byte player records and the packed area loader remain the RAM owners;
 * no route or framebuffer condition is used to advance the level. */
static void Sub_PlayerEndLevel(void) {
    GameMode_AutoControl(BTN_RIGHT);
    if (g_Player_Y_Position >= 0xae && g_ScrollLock != 0) {
        g_EventMusicQueue = EndOfLevelMusic;
        g_ScrollLock = 0;
    }

    if ((g_Player_CollisionBits & 0x01) == 0) {
        if (g_StarFlagTaskControl == 0)
            g_StarFlagTaskControl++;
        g_Player_SprAttrib = 0x20;
    }

    if (g_StarFlagTaskControl != 0x05) return;

    g_LevelNumber++;
    /* PlayerEndLevel's INC LevelNumber is the active record byte at $075c;
     * retain the scalar as the loader/HUD view of the same RAM. */
    g_OnscreenPlayerInfo[2] = g_LevelNumber;
    if (g_LevelNumber == 0x03 && g_WorldNumber < 8 &&
        g_CoinTallyFor1Ups >= Hidden1UpCoinAmts[g_WorldNumber])
    {
        g_Hidden1UpFlag++;
        /* The ROM INC targets the active player record at $075d. */
        g_OnscreenPlayerInfo[3] = g_Hidden1UpFlag;
    }
    Sub_NextArea();
}

/* GameRoutines (engine/game-mode/routines.asm:3-18).  Unsupported entries
 * are kept as explicit dispatch holes so the state byte remains authoritative
 * and later clusters can attach the original routine without changing call
 * order. */
static void GameRoutines_Dispatch(void) {
    switch (g_GameEngineSubroutine) {
        case 0:
            /* GameRoutines enters through JumpEngine.  Its selected
             * Entrance_GameTimerSetup pointer is in the $91 page, and the
             * indirect jump leaves that high byte in $0007.  SetupBubble
             * immediately reuses the live scratch byte as its Y table index
             * (routines.asm:2-18; game-timer-setup.asm:78-82). */
            g_ZeroPageScratch07 = 0x91;
            Sub_EntranceGameTimerSetup();
            break;
        case 2:  Sub_SideExitPipeEntry(); break;
        case 3:  Sub_VerticalPipeEntry(); break;
        case 6:  Sub_PlayerLoseLife(); break;
        case 7:  Sub_PlayerEntrance(); break;
        case 8:  Sub_PlayerCtrlRoutine(); break;
        case 9:  Sub_PlayerChangeSize(); break;
        case 10: Sub_PlayerInjuryBlink(); break;
        case 11: Sub_PlayerDeath(); break;
        case 12: Sub_PlayerFireFlower(); break;
        case 1:  Sub_VineAutoClimb(); break;
        case 4:  Sub_FlagpoleSlide(); break;
        case 5:  Sub_PlayerEndLevel(); break;
        default: /* invalid/reserved routine number: ASM table has no entry. */
            break;
    }
}

/* Subroutine 8: PlayerCtrlRoutine (player-control.asm) */
static void Sub_PlayerCtrlRoutine(void) {
    /* NES PlayerCtrlRoutine order (player-control.asm:41-77):
     * movement subs -> ChkMoveDir -> ScrollHandler -> offscreen bits ->
     * RelativePlayerPosition -> bounding box -> PlayerBGCollision. */
    Player_UpdateControl();
    Player_MovementSubs();
    Player_UpdateBoundBoxCtrl();
    Player_UpdateMovingDirection();
    Scroll_Update();
    Player_UpdateOffscreenBits();
    /* RelativePlayerPosition follows ScrollHandler and supplies both the
     * player bounding-box producer and later PlayerEnemyCollision users. */
    g_Player_Rel_XPos = (uint8_t)(g_Player_X_Position -
                                  g_ScreenLeft_X_Pos);
    g_Player_Rel_YPos = g_Player_Y_Position;
    Collision_UpdatePlayerBoundingBox();
    /* Player_Pos_ForScroll is written by RenderPlayerSub after this routine
     * returns.  Keeping that producer in PlayerGfxHandler is observable when
     * PlayerGfxHandler skips an injury-blink frame: ScrollHandler must then
     * consume the previous frame's relative X value. */
    Collision_PlayerBG();

    /* PlayerCtrlRoutine falls through to PlayerHole only after the complete
     * movement/collision/bounding-box chain above. */
    if (g_Player_Y_Position >= 0x40 &&
        g_GameEngineSubroutine != 0x05 &&
        g_GameEngineSubroutine != 0x07 &&
        g_GameEngineSubroutine >= 0x04) {
        g_Player_SprAttrib &= (uint8_t)~0x20;
    }
    Player_Hole();
}

/* AutoControlPlayer (player-control.asm:3-5) is a shared entry boundary:
 * replace the controller latch, then enter the normal PlayerCtrlRoutine. */
void GameMode_AutoControl(uint8_t input) {
    g_SavedJoypadBits = input;
    Sub_PlayerCtrlRoutine();
}

/* RunGameTimer (main.asm:3354-3393): decrement TIME digits every 24
 * frames ($18 reload of GameTimerCtrlTimer = Timers[7]), queue the
 * time-running-out music at 100, and enter ForceInjury/TimeUpOn at 000. */
static void RunGameTimer(void) {
    if (g_OperMode == TITLE_SCREEN_MODE) return;
    if (g_GameEngineSubroutine < 0x08) return;
    if (g_GameEngineSubroutine == 0x0b) return;
    if (g_Player_Y_HighPos >= 2) return;
    if (g_Timers[7] != 0) return; /* GameTimerCtrlTimer */

    {
        extern uint8_t g_DisplayDigits[36];
        uint8_t *d = &g_DisplayDigits[33]; /* GameTimerDisplay 3 digits */
        if (d[0] == 0 && d[1] == 0 && d[2] == 0) {
            /* TimeUpOn (main.asm:3389-3392): A is zero after the
             * all-digits test, then ForceInjury owns the player state and
             * TimerControl transition. */
            g_PlayerStatus = PLAYER_STATUS_SMALL;
            Enemy_ForceInjury();
            g_GameTimerExpiredFlag++;
            return;
        }
        if (d[0] == 1 && d[1] == 0 && d[2] == 0)
            g_EventMusicQueue = TimeRunningOutMusic;

        /* ResGTCtrl precedes DigitsMathRoutine in the ASM. */
        g_GameTimerCtrlTimer = 0x18;
        /* DigitsMathRoutine: DigitModifier+5=$ff, Y=$23. */
        Score_SetDigitModifier(5, 0xff);
        Score_DigitsMath(0x23);
    }
    HUD_PrintStatusBarNumbers(0xa4);
}

/* GameEngine (core.asm GameEngine): always runs at OperMode_Task >= 3 */
static void GameEngine(void) {
    /* GameEngine/ProcFireball_Bubble (core.asm:5) owns the two fireball
     * slots before the six enemy ObjectOffset passes.  This order is
     * observable: fireball collision consumes the prior enemy bounding-box
     * state, while newly spawned enemy objects run later in this frame. */
    Enemy_ProcFireballBubble();
    Enemies_Core();
    /* GetPlayerOffscreenBits/RelativePlayerPosition precede PlayerGfxHandler
     * in the ASM.  These are persistent player-object fields, not a
     * renderer-local visibility decision. */
    Player_UpdateOffscreenBits();
    g_Player_Rel_XPos = (uint8_t)(g_Player_X_Position -
                                  g_ScreenLeft_X_Pos);
    g_Player_Rel_YPos = g_Player_Y_Position;
    PlayerGfxHandler();

    /* BlockObjMT_Updater, BlockObjectsCore (offsets 1 then 0), and
     * MiscObjectsCore are kept in this call's internal order. */
    Collision_DrawBlockObjects();
    Enemy_ProcessHammers();
    /* ProcessCannons is the shared cannon/bullet producer between
     * MiscObjectsCore and ProcessWhirlpools (core.asm:31-34). */
    Enemy_ProcessCannons();
    /* ProcessWhirlpools follows MiscObjectsCore in core.asm and owns the
     * persistent player gravity/collision-bit effect. */
    Misc_ProcessWhirlpools();
    /* FlagpoleRoutine is the special ObjectOffset=$05 consumer and runs
     * after MiscObjectsCore, before RunGameTimer (main.asm:3506). */
    Enemy_RunFlagpoleRoutine();
    RunGameTimer();
    Player_ColorRotation();
    if (g_Player_Y_HighPos < 2 && g_StarInvincibleTimer == 4 &&
        g_IntervalTimerControl == 0)
        GameMode_UpdateAreaMusicQueue();
    GameMode_UpdatePlayerPalette();
    /* SaveAB (core.asm:57-60) stores the controller subset used by the
     * next frame's A/B edge tests, then clears the directional latch.  This
     * is deliberately at the end of GameEngine: fireball, jump, and
     * jumpspring consumers must see the previous frame's value first. */
    g_PreviousA_B_Buttons = g_A_B_Buttons;
    /* PlayerDeath may re-enter PlayerCtrlRoutine after TimerControl falls
     * below $f0; retaining the prior frame's directional latch would then
     * move the player although the 6502 sees $0c == 0. */
    g_Left_Right_Buttons = 0;
    /* UpdScrollVar / AreaParserTaskHandler (core.asm:62-80; main.asm:
     * 1739-1764): parser output selects VRAM_Buffer2 after this frame's
     * game-engine writes, so the next NMI preserves Buffer1 latency. */
    Scroll_RunParserTask();
}

void PlayerGfxHandler(void) {
    Player_UpdateSprite();
}

/* ========================================================================
 * GameCoreRoutine - called from opermode task 3 (menu demo and gameplay)
 * ======================================================================== */

void GameMode_MainLoop(void) {
    /* GameCoreRoutine (core.asm:2-13) selects SavedJoypadBits[CurrentPlayer]
     * before dispatch.  The second latch is populated by the platform input
     * boundary when available; FM2 port-0 movies leave it at its NES-zero
     * value. */
    if (g_CurrentPlayer != 0)
        g_SavedJoypadBits = g_SavedJoypad2Bits;
    GameRoutines_Dispatch();

    /* 3. GameEngine when we are supposed to be here (task >= 3) */
    if (g_OperMode_Task >= 3) {
        GameEngine();
    }
}
