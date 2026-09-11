/* title-menu.c - title mode menu/demo routines from titlescreen.asm and
 * title-mode/demo.asm. */
#include <string.h>

#include "title-menu.h"
#include "assets.h"
#include "opermode.h"
#include "game-mode/core.h"
#include "level/level.h"
#include "screen/routine/title.h"
#include "constants/defs.h"
#include "constants/globals.h"

/* DemoActionData/DemoTimingData (engine/title-mode/demo.asm:2-10). */
static uint8_t DemoActionData[21];
static uint8_t DemoTimingData[22];
static uint8_t WSelectBufferTemplate[6];

int TitleMenu_LoadRom(void) {
    if (Assets_Copy("tables/demo_action.bin", DemoActionData,
                    sizeof(DemoActionData)) ||
        Assets_Copy("tables/demo_timing.bin", DemoTimingData,
                    sizeof(DemoTimingData)) ||
        Assets_Copy("tables/wselect_buffer.bin", WSelectBufferTemplate,
                    sizeof(WSelectBufferTemplate)))
        return -1;
    return 0;
}

/* DemoEngine returns nonzero when the terminating zero timing is reached. */
static int Title_DemoEngine(void) {
    uint8_t x = g_DemoAction;

    if (g_DemoActionTimer == 0) {
        x++;
        g_DemoAction = x;
        g_DemoActionTimer = DemoTimingData[x - 1];
        if (g_DemoActionTimer == 0) return 1;
    }

    /* The original data has one action for every nonzero timing entry. */
    g_SavedJoypadBits = DemoActionData[x - 1];
    g_DemoActionTimer--;
    return 0;
}

static void Title_ResetTitle(void) {
    g_OperMode = TITLE_SCREEN_MODE;
    g_OperMode_Task = 0;
    g_Sprite0HitDetectFlag = 0;
    g_DisableScreenFlag++;
}

static void Title_GoContinue(uint8_t world) {
    /* GoContinue (titlescreen.asm:111-117) updates both players' world/area
     * records before the shared LoadAreaPointer boundary. */
    g_WorldNumber = world;
    g_LevelNumber = 0;
    g_AreaNumber = 0;
    g_OffscreenPlayerInfo[5] = world;
    g_OffscreenPlayerInfo[6] = 0;
}

static void Title_StartWorld(uint8_t start_bits) {
    if (start_bits & BTN_A) {
        Title_GoContinue(g_ContinueWorld);
    }
    /* StartWorld1 calls LoadAreaPointer (titlescreen.asm:92-93), which only
     * selects the packed pointer and area type.  GetAreaDataAddrs runs later
     * from InitializeArea; keeping the old data/cursor live here preserves
     * the physical AreaDataOffset while the mode transition is pending. */
    (void)Level_SelectAreaPointer(g_WorldNumber, g_AreaNumber);
    /* Hidden1UpFlag is the active player-record byte at $075D.  Keep the
     * named C alias synchronized, but make the record the owning storage as
     * it is in StartWorld1 (titlescreen.asm:92-109). */
    g_OnscreenPlayerInfo[3]++;
    g_Hidden1UpFlag = g_OnscreenPlayerInfo[3];
    g_OffscreenPlayerInfo[3]++;
    g_FetchNewGameTimerFlag++;
    g_OperMode++;
    g_PrimaryHardMode = g_WorldSelectEnableFlag;
    /* StartWorld1 clears only OperMode_Task.  ScreenRoutineTask ($073C) is
     * cleared by the following GameMode/InitializeArea memory pass. */
    g_OperMode_Task = 0;
    g_DemoTimer = 0;
    /* InitScores starts at ScoreAndCoinDisplay ($07dd), not at the
     * preserved TopScoreDisplay ($07d7): LDX #$17 clears $07dd-$07f4. */
    memset(&g_DisplayDigits[6], 0, 24);
}

static void Title_UpdateWorldSelect(void) {
    /* IncWorldSel keeps the incremented value in X after masking only the
     * stored WorldSelectNumber.  That distinction matters when the selector
     * wraps: X is 8 on the 8->1 transition, so the BMI loop emits no stale
     * template bytes, exactly as the 6502 CMP #$06 branch does. */
    uint8_t x = (uint8_t)(g_WorldSelectNumber + 1);
    uint8_t world = (uint8_t)(x & 0x07);

    g_WorldSelectNumber = world;
    Title_GoContinue(world);

    /* UpdateShroom: lda template,x / sta VRAM_Buffer1-1,x until X=$06. */
    while (x < sizeof(WSelectBufferTemplate)) {
        g_VRAM_Buffer1[x - 1] = WSelectBufferTemplate[x];
        x++;
    }
    g_VRAM_Buffer1[3] = (uint8_t)(g_WorldNumber + 1);
}

/* RunDemo is the shared fall-through target of NullJoypad and the attract
 * input path (titlescreen.asm:71-77).  Select/B handling must still execute
 * GameCoreRoutine with the cleared controller latch. */
static void Title_RunDemoCore(void) {
    GameMode_MainLoop();
    if (g_GameEngineSubroutine == 0x06) Title_ResetTitle();
}

void Title_GameMenuRoutine(void) {
    uint8_t bits = (uint8_t)(g_SavedJoypadBits | g_SavedJoypad2Bits);

    /* GameMenuRoutine (titlescreen.asm:14-23). */
    if (bits == BTN_START || bits == (BTN_A | BTN_START)) {
        if (g_DemoTimer == 0) {
            Title_ResetTitle();
            return;
        }
        Title_StartWorld(bits);
        return;
    }

    if (bits == BTN_SELECT ||
        (g_DemoTimer != 0 && g_WorldSelectEnableFlag && bits == BTN_B)) {
        if (g_DemoTimer == 0) {
            Title_ResetTitle();
            return;
        }
        g_DemoTimer = 0x18;
        if (g_SelectTimer != 0) {
            g_SavedJoypadBits = 0;
            return;
        }
        g_SelectTimer = 0x10;
        if (bits == BTN_B) {
            Title_UpdateWorldSelect();
        } else {
            g_NumberOfPlayers ^= 1;
            DrawMushroomIcon();
        }
        g_SavedJoypadBits = 0;
        Title_RunDemoCore();
        return;
    }

    if (g_DemoTimer == 0) {
        /* ChkSelect stores the observed pad byte before DemoEngine, even
         * though the demo replaces SavedJoypadBits for this frame. */
        g_SelectTimer = bits;
        if (Title_DemoEngine()) Title_ResetTitle();
        else Title_RunDemoCore();
        return;
    }

    /* World select is disabled by default; when enabled, only B is consumed
     * by the branch above.  Other title input is nulled before RunDemo. */
    g_SavedJoypadBits = 0;
    Title_RunDemoCore();
}
