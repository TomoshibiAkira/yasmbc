/* victory.c - VictoryMode routine graph from engine/victory-mode ASM files. */

#include <stddef.h>
#include "victory.h"
#include "enemy/enemy.h"
#include "game-mode/core.h"
#include "level/level.h"
#include "player/player.h"
#include "scroll.h"
#include "constants/defs.h"
#include "constants/globals.h"

/* SetupVictoryMode (victory-mode/setup.asm:2-8). */
static void SetupVictoryMode(void) {
    /* SetupVictoryMode reads ScreenRight_PageLoc, then increments it. */
    g_DestinationPageLoc = (uint8_t)(g_ScreenRight_PageLoc + 1);
    /* SetupVictoryMode stores EndOfCastleMusic in the secondary
     * EventMusicQueue ($00fc), not the primary AreaMusicQueue ($00fb). */
    g_EventMusicQueue = EndOfCastleMusic;
    g_OperMode_Task++;
}

/* PlayerVictoryWalk (victory-mode/victory-walk.asm:2-33). */
static void PlayerVictoryWalk(void) {
    g_VictoryWalkControl = 0;
    if (g_Player_PageLoc != g_DestinationPageLoc ||
        g_Player_X_Position < 0x60) {
        g_VictoryWalkControl++;
        GameMode_AutoControl(BTN_RIGHT);
    } else {
        GameMode_AutoControl(0);
    }

    /* ScrollScreen/UpdScrollVar uses the same camera RAM that normal player
     * control owns. Retain the original fractional carry state when the
     * destination page has not entered view. */
    if (g_ScreenLeft_PageLoc != g_DestinationPageLoc) {
        uint8_t old_fraction = g_ScrollFractional;
        uint8_t amount;
        g_ScrollFractional = (uint8_t)(old_fraction + 0x80);
        /* ScrollScreen receives #$01 plus the carry from ADC #$80. */
        amount = (uint8_t)(1 + (g_ScrollFractional < old_fraction));
        Scroll_Advance(amount);
        /* PlayerVictoryWalk calls UpdScrollVar immediately after the
         * camera step, sharing the normal parser-task owner. */
        Scroll_RunParserTask();
        g_VictoryWalkControl++;
    }

    if (g_VictoryWalkControl == 0) g_OperMode_Task++;
}

/* PrintVictoryMessages (victory-mode/victory-message.asm:2-59).  The
 * message stream is selected by AddrCtrl; actual message data remains in the
 * existing VRAM source table and no framebuffer-derived text is introduced. */
static void PrintVictoryMessages(void) {
    uint8_t message = 0;
    uint8_t print_message = 0;

    /* PrintVictoryMessages (victory-mode/victory-message.asm:2-59).
     * Keep the 6502 labels visible here: the non-World-8 retainer path
     * increments Y, decrements it again, and skips printing at Y >= 3.
     * That distinction is what prevents PrincessSaved1 ($0f) from being
     * selected between the retainer message and world-end delay. */
    if (g_SecondaryMsgCounter == 0 && g_PrimaryMsgCounter == 0) {
        message = g_CurrentPlayer ? 1 : 0;
        print_message = 1;
    } else if (g_SecondaryMsgCounter == 0 && g_PrimaryMsgCounter < 0x09) {
        if (g_WorldNumber == WORLD_8) {
            if (g_PrimaryMsgCounter >= 0x03) {
                /* CMP #$03 / SBC #$01 then ThankPlayer INY: world 8 eval
                 * uses the primary counter itself as the message index. */
                message = g_PrimaryMsgCounter;
                print_message = 1;
            }
        } else if (g_PrimaryMsgCounter >= 0x02) {
            message = g_PrimaryMsgCounter;
            /* ThankPlayer/SecondPartMsg: nonzero Y takes INY.  World 1-7 then
             * DEY's back to the primary counter before the two termination tests;
             * World 8 falls through directly to EvalForMusic. */
            if (message != 0) {
                message++;
                message--;
                if (message >= 0x04) {
                    g_WorldEndTimer = 6;
                    g_OperMode_Task++;
                    return;
                }
                if (message < 0x03)
                    print_message = 1;
            } else {
                print_message = 1;
            }
        }
    }

    if (print_message) {
        if (message == 0x03) g_EventMusicQueue = VictoryMusic;
        g_VRAM_Buffer_AddrCtrl = (uint8_t)(message + 0x0c);
    }

    {
        uint8_t old_secondary = g_SecondaryMsgCounter;
        g_SecondaryMsgCounter = (uint8_t)(old_secondary + 4);
        if (g_SecondaryMsgCounter < old_secondary) g_PrimaryMsgCounter++;
    }
    if (g_PrimaryMsgCounter < 7) return;

    g_WorldEndTimer = 6;
    g_OperMode_Task++;
}

/* PlayerEndWorld (victory-mode/end-world.asm:2-25). */
static void PlayerEndWorld(void) {
    if (g_WorldEndTimer != 0) return;
    if (g_WorldNumber < WORLD_8) {
        g_AreaNumber = 0;
        g_LevelNumber = 0;
        g_OperMode_Task = 0;
        g_WorldNumber++;
        /* PlayerEndWorld writes the active zero-page aliases directly.  The
         * C scalar views are convenient for the translated routines, but
         * the canonical RAM owner is OnscreenPlayerInfo at $075a: mirror
         * AreaNumber=$0760, LevelNumber=$075c, and WorldNumber=$075f at the
         * same routine boundary. */
        g_OnscreenPlayerInfo[6] = g_AreaNumber;
        g_OnscreenPlayerInfo[2] = g_LevelNumber;
        g_OnscreenPlayerInfo[5] = g_WorldNumber;
        /* PlayerEndWorld calls LoadAreaPointer after selecting world+area;
         * keep the old parsed header live until GAME_MODE task 0 reaches
         * InitializeArea/GetAreaDataAddrs. */
        (void)Level_SelectAreaPointer(g_WorldNumber, g_AreaNumber);
        g_FetchNewGameTimerFlag++;
        g_OperMode = GAME_MODE;
        return;
    }

    if ((g_SavedJoypadBits | g_SavedJoypad2Bits) & BTN_B) {
        g_WorldSelectEnableFlag = 1;
        g_NumberofLives = 0xff;
        GameMode_TerminateGame();
    }
}

void VictoryMode_Tick(void) {
    static void (*const routines[])(void) = {
        Enemy_BridgeCollapse, SetupVictoryMode, PlayerVictoryWalk,
        PrintVictoryMessages, PlayerEndWorld
    };
    uint8_t task = g_OperMode_Task;

    if (task < sizeof(routines) / sizeof(routines[0]) && routines[task]) {
        routines[task]();
    }
    /* VictoryMode (victory-mode/core.asm:4-9) reloads OperMode_Task after
     * VictoryModeSubroutines returns.  PlayerEndWorld changes the task to
     * zero before this branch, so the terminating world task must skip the
     * enemy pass even though the dispatch index was 4 on entry. */
    if (g_OperMode_Task != 0) {
        Enemy_ProcessObjectPass(0);
    }
    /* VictoryMode branches around EnemiesAndLoopsCore for task 0, then
     * falls through AutoPlayer to RelativePlayerPosition and
     * PlayerGfxHandler (victory-mode/core.asm).  Player graphics therefore
     * run on the bridge-collapse task as well.  RelativePlayerPosition must
     * follow PlayerVictoryWalk's ScrollScreen call; PlayerGfxHandler consumes
     * these persistent $03AD/$03B8 values rather than recomputing them. */
    g_Player_Rel_XPos = (uint8_t)(g_Player_X_Position - g_ScreenLeft_X_Pos);
    g_Player_Rel_YPos = g_Player_Y_Position;
    Player_UpdateSprite();
}
