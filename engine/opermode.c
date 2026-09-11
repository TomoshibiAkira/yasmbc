/* opermode.c - Operating mode handler (from opermode.asm, titlescreen.asm,
 * main.asm InitializeGame/InitializeArea/PrimaryGameSetup/SecondaryGameSetup)
 *
 * NES task layout:
 *   TitleScreenMode: 0=InitializeGame, 1=ScreenRoutines,
 *                    2=PrimaryGameSetup (falls into SecondaryGameSetup),
 *                    3=GameMenuRoutine
 *   GameMode:        0=InitializeArea, 1=ScreenRoutines,
 *                    2=SecondaryGameSetup, 3=GameCoreRoutine
 *
 * InitializeGame and InitializeArea run long enough to black out
 * subsequent NMIs; those skips are modeled with g_NMIBusy so downstream
 * frame timing matches FCEUX.
 */

#include "constants/defs.h"
#include "system/platform.h"
#include "opermode.h"
#include "nmi.h"
#include "game-mode/core.h"
#include "screen/screen.h"
#include "screen/routine/area_parser.h"
#include "screen/routine/hud.h"
#include "player/player.h"
#include "collision.h"
#include "level/level.h"
#include "timers.h"
#include "sprite-offsets.h"
#include "title-menu.h"
#include "victory.h"
#include "audio.h"
#include "constants/globals.h"
#include "score.h"
#include <string.h>

/* ========================================================================
 * SHARED INITIALIZATION (main.asm:1405-1476)
 * ======================================================================== */

/* InitializeMemory's descending 6502 page loop (main.asm:1536-1555) starts
 * with Y=$4b for InitializeArea.  It clears only offsets $00..$4b in each
 * page (and skips $0160..$01ff), not every byte below $074b.  Keep the two
 * parts separate so a long InitializeArea can resume at the same RAM
 * boundary.  C aliases outside that byte range are intentionally preserved;
 * their owning routines initialize them when needed. */
static void InitializeMemoryHigh(void) {
    g_FlagpoleFNumYPos = 0;          /* $010D */
    g_FlagpoleFNumYMF_Dummy = 0;     /* $010E */
    g_FlagpoleScore = 0;             /* $010F */
    memset(g_DigitModifier, 0, sizeof(g_DigitModifier)); /* $0133-$0139 */
    memset(g_SpriteData, 0, sizeof(g_SpriteData));
    memset(g_VRAM_Buffer1, 0, 512);
    memset(g_VRAM_Buffer2, 0, 512);
    SpriteOffsets_ClearMemory(); /* $03ee and $06e4-$06fc */
    g_VRAM_Buffer1_Offset = 0;       /* $0300 */
    g_VRAM_Buffer2_Offset = 0;       /* $0340 */
    g_Player_Rel_XPos = 0;            /* $03AD */
    g_Enemy_Rel_XPos = 0;             /* $03AE */
    g_Player_Rel_YPos = 0;            /* $03B8 */
    g_Player_SprAttrib = 0;            /* $03C4 */
    g_Player_OffscreenBits = 0;        /* $03D0 */
    g_BlockResidualCounter = 0;        /* $03F0 */
    g_BalPlatformAlignment = 0;        /* $03A0 */
    g_Platform_X_Scroll = 0;            /* $03A1 */
    g_VineFlagOffset = 0;               /* $0398 */
    g_VineHeight = 0;                   /* $0399 */
    memset(g_VineObjOffset, 0, sizeof(g_VineObjOffset)); /* $039A-$039C */
    g_VineStartY = 0;                   /* $039D */
    g_WhirlpoolOffset = 0;              /* $046A */
    g_WhirlpoolFlag = 0;                /* $047D */
    g_EntranceBubbleYMF = 0;          /* $0431 */
    g_Player_X_MF2 = 0;               /* $0400 */
    g_Player_YMF_Dummy = 0;           /* $0416 */
    g_Player_Y_MoveForce = 0;         /* $0433 */
    g_MaximumLeftSpeed = 0;             /* $0450 */
    g_MaximumRightSpeed = 0;            /* $0456 */
    g_Player_CollisionBits = 0;         /* $0490 */
    g_Player_BoundBoxCtrl = 0;          /* $0499 */
    memset(g_Player_BoundingBox, 0, sizeof(g_Player_BoundingBox)); /* $04AC */
    g_BrickCoinTimerFlag = 0;           /* $06BC */
    g_SecondaryHardMode = 0;            /* $06CC */
    g_EnemyFrenzyBuffer = 0;            /* $06CB */
    g_EnemyFrenzyQueue = 0;             /* $06CD */
    g_FireballCounter = 0;              /* $06CE */
    g_NumberofGroupEnemies = 0;         /* $06D3 */
    g_ColorRotateOffset = 0;            /* $06D4 */
    g_PlayerGfxOffset = 0;              /* $06D5 */
    g_WarpZoneControl = 0;              /* $06D6 */
    g_FireworksCounter = 0;              /* $06D7 */
    g_MultiLoopCorrectCntr = 0;          /* $06D9 */
    g_MultiLoopPassCntr = 0;             /* $06DA */
    g_JumpspringForce = 0;               /* $06DB */
    g_BitMFilter = 0;                    /* $06DD */
    g_ChangeAreaTimer = 0;               /* $06DE */
    g_SavedJoypadBits = 0;               /* $06FC */
    g_SavedJoypad2Bits = 0;              /* $06FD */
    g_Player_X_Scroll = 0;               /* $06FF */
    g_Player_XSpeedAbsolute = 0;      /* $0700 */
    g_RunningSpeed = 0;              /* $0703 */
    g_SwimmingFlag = 0;              /* $0704 */
    g_Player_X_MoveForce = 0;         /* $0705 */
    g_DiffToHaltJump = 0;             /* $0706 */
    g_JumpOrigin_Y_HighPos = 0;       /* $0707 */
    g_JumpOrigin_Y = 0;               /* $0708 */
    g_VerticalForce = 0;              /* $0709 */
    g_FallMForce = 0;                 /* $070A */
    g_PlayerChangeSizeFlag = 0;       /* $070B */
    g_PlayerAnimTimerSet = 0;         /* $070C */
    g_PlayerAnimCtrl = 0;             /* $070D */
    g_JumpspringAnimCtrl = 0;         /* $070E */
    g_FlagpoleCollisionYPos = 0;      /* $070F */
    g_PlayerEntranceCtrl = 0;         /* $0710 */
    g_FireballThrowingTimer = 0;      /* $0711 */
    g_DeathMusicLoaded = 0;           /* $0712 */
    g_FlagpoleSoundQueue = 0;         /* $0713 */
    g_CrouchingFlag = 0;              /* $0714 */
    g_GameTimerSetting = 0;           /* $0715 */
    g_DisableCollisionDet = 0;        /* $0716 */
    g_DemoAction = 0;                 /* $0717 */
    g_DemoActionTimer = 0;            /* $0718 */
    g_PrimaryMsgCounter = 0;          /* $0719 */
    g_ScreenLeft_PageLoc = 0;         /* $071A */
    g_ScreenRight_PageLoc = 0;        /* $071B */
    g_ScreenLeft_X_Pos = 0;           /* $071C */
    g_ScreenRight_X_Pos = 0;          /* $071D */
    g_ColumnSets = 0;                 /* $071E */
    g_AreaParserTaskNum = 0;          /* $071F */
    g_CurrentNTAddrHigh = 0;          /* $0720 */
    g_CurrentNTAddrLow = 0;           /* $0721 */
    g_Sprite0HitDetectFlag = 0;        /* $0722 */
    g_ScrollLock = 0;                 /* $0723 */
    g_CurrentPageLoc = 0;             /* $0725 */
    g_CurrentColumnPos = 0;           /* $0726 */
    g_TerrainControl = 0;             /* $0727 */
    g_BackloadingFlag = 0;            /* $0728 */
    g_BehindAreaParserFlag = 0;       /* $0729 */
    g_AreaStyle = 0;                  /* $0733 */
    g_StaircaseControl = 0;           /* $0734 */
    g_EnemyDataOffset = 0;            /* $0739 */
    g_EnemyObjectPageLoc = 0;         /* $073A */
    g_EnemyObjectPageSel = 0;         /* $073B */
    g_ScreenRoutineTask = 0;          /* $073C */
    g_ScrollThirtyTwo = 0;            /* $073D */
    g_HorizontalScroll = 0;           /* $073F */
    g_Screen_Y_Position = 0;          /* $0740 */
    g_ForegroundScenery = 0;          /* $0741 */
    g_BackgroundScenery = 0;          /* $0742 */
    g_CloudTypeOverride = 0;          /* $0743 */
    g_BackgroundColorCtrl = 0;        /* $0744 */
    g_LoopCommand = 0;                /* $0745 */
    g_StarFlagTaskControl = 0;        /* $0746 */
    g_TimerControl = 0;               /* $0747 */
    g_CoinTallyFor1Ups = 0;           /* $0748 */
    g_SecondaryMsgCounter = 0;        /* $0749 */
    g_JoypadBitMask[0] = 0;            /* $074A */
    g_JoypadBitMask[1] = 0;            /* $074B */
}

static void InitializeMemoryLow(void) {
    g_FrameCounter = 0;              /* $0009 */
    g_A_B_Buttons = 0;               /* $000A */
    g_PreviousA_B_Buttons = 0;       /* $000D */
    g_Up_Down_Buttons = 0;           /* $000B */
    g_Left_Right_Buttons = 0;        /* $000C */
    g_GameEngineSubroutine = 0;      /* $000E */
    g_Player_State = 0;              /* $001D */
    g_PlayerFacingDir = 0;           /* $0033 */
    g_DestinationPageLoc = 0;        /* $0034 */
    g_VictoryWalkControl = 0;        /* $0035 */
    g_Player_MovingDir = 0;           /* $0045 */
    g_Player_X_Speed = 0;             /* $0057 */
    g_Player_PageLoc = 0;             /* $006D */
    g_Player_X_Position = 0;          /* $0086 */
    g_Player_Y_Speed = 0;             /* $009F */
    g_Player_Y_HighPos = 0;           /* $00B5 */
    g_Player_Y_Position = 0;          /* $00CE */
    g_EnemyDataLow = 0;              /* $00E9 */
}

static void InitializeMemory(void) {
    /* InitializeMemory's Y-bounded page clear owns the low sound symbols
     * $00f0-$00ff.  It intentionally does not clear SoundMemory=$07b0. */
    Audio_ResetLowMemory();
    InitializeMemoryHigh();
    InitializeMemoryLow();
}

/* InitializeArea common part after InitializeMemory: clears timers, sets
 * screen start position, resets column set count and area data addresses. */
static void InitializeAreaAfterMemory(void) {
    uint8_t start_page;

    /* ClrTimersLoop clears $0780-$07A1 only; DemoTimer ($07A2) survives. */
    Timers_ClearAreaBank();
    Collision_ResetBlockObjects(); /* Block_State/RepFlag slots 0 and 1. */

    /* InitializeArea (main.asm:1417-1476) chooses the saved entry page for
     * an alternate entrance and the halfway page for a normal restart. */
    start_page = g_AltEntranceControl ? g_EntrancePage : g_HalfwayPage;
    g_ScreenLeft_PageLoc = start_page;
    g_CurrentPageLoc = start_page;
    g_BackloadingFlag = start_page;
    g_CurrentNTAddrLow = 0x80;
    g_CurrentNTAddrHigh = (uint8_t)((start_page & 1) ? 0x24 : 0x20);
    g_BlockBufferColumnPos = (uint8_t)((start_page & 1) << 4);

    /* Render 12 column sets (~1.5 screens) */
    g_ColumnSets = 0x0B;

    /* InitializeArea calls GetAreaDataAddrs unconditionally after the
     * packed AreaPointer has been selected (main.asm:1451-1453).  The title
     * InitializeGame path has no live pointer yet, so its preceding
     * LoadAreaPointer is represented by the world/area lookup; every later
     * area transition must consume the already-owned packed pointer. */
    {
        extern const uint8_t* g_AreaDataPtr;
        if (g_AreaDataPtr == NULL) {
            Level_Load(g_WorldNumber, g_AreaNumber);
        } else {
            (void)Level_LoadAreaPointer(g_AreaPointer);
        }
    }
    AreaParser_Reset();

    g_PlayerEntranceCtrl = Level_PlayerEntranceCtrl();

    /* CheckHalfway (main.asm:1468-1474): a nonzero restart page forces the
     * entrance header's halfway entry rather than the normal spawn. */
    if (g_HalfwayPage != 0)
        g_PlayerEntranceCtrl = 2;

    /* ResolveAreaAddress/InitializeArea already applied the
     * GetAreaDataAddrs secondary-hard-mode gate at the load boundary. */

    /* The original queues Silence here for SoundEngine to consume. */
    g_AreaMusicQueue = Silence;

    g_DisableScreenFlag = 1;
}

static void InitializeArea(void) {
    InitializeMemory();
    InitializeAreaAfterMemory();
}

/* InitializeGame (main.asm:1405-1415) performs a longer InitializeMemory
 * pass (Y=$6f) before entering InitializeArea's ordinary Y=$4b pass.  The
 * latter deliberately preserves $074c-$076f, so a title reset must clear the
 * named state in that interval first.  These are the C views of the same
 * physical bytes; the active/offscreen player records are the owners of
 * $075a-$0767. */
static void InitializeGame(void) {
    /* InitializeGame follows InitializeMemory with ClrSndLoop over the full
     * SoundMemory bank ($07b0-$07cf), unlike a later InitializeArea. */
    Audio_ResetGameMemory();
    g_AreaType = 0;                 /* $074e */
    g_AreaPointer = 0;              /* $0750 */
    g_EntrancePage = 0;             /* $0751 */
    g_AltEntranceControl = 0;       /* $0752 */
    g_CurrentPlayer = 0;             /* $0753 */
    g_PlayerSize = 0;                /* $0754 */
    g_Player_Pos_ForScroll = 0;      /* $0755 */
    g_PlayerStatus = 0;              /* $0756 */
    g_FetchNewGameTimerFlag = 0;     /* $0757 */
    g_JoypadOverride = 0;            /* $0758 */
    g_GameTimerExpiredFlag = 0;      /* $0759 */
    memset(g_OnscreenPlayerInfo, 0, sizeof(g_OnscreenPlayerInfo)); /* $075a */
    memset(g_OffscreenPlayerInfo, 0, sizeof(g_OffscreenPlayerInfo)); /* $0761 */
    g_ScrollFractional = 0;          /* $0768 */
    g_DisableIntermediate = 0;       /* $0769 */
    g_PrimaryHardMode = 0;           /* $076a */
    g_WorldSelectNumber = 0;         /* $076b */

    /* Keep scalar views synchronized with the cleared active record. */
    g_HalfwayPage = 0;
    g_WorldNumber = 0;
    g_LevelNumber = 0;
    g_AreaNumber = 0;
    g_Hidden1UpFlag = 0;
    g_CoinTally = 0;
    g_NumberofLives = 0;

    /* LoadAreaPointer precedes InitializeArea in the ROM.  The host loader
     * selects that pointer as part of the NULL-data load boundary below. */
    g_AreaDataPtr = NULL;
    g_AreaDataLen = 0;
    g_AreaDataOffset = 0;
    InitializeArea();
}

/* ========================================================================
 * MAIN OPERATING MODE HANDLER
 * ======================================================================== */

void OperMode_SetMode(uint8_t mode) {
    g_OperMode = mode;
    g_OperMode_Task = 0;
    g_ScreenRoutineTask = 0;
}

void OperMode_Tasks(void) {
    switch (g_OperMode) {
        case TITLE_SCREEN_MODE:
            OperMode_TitleScreen();
            break;
        case GAME_MODE:
            OperMode_GameMode();
            break;
        case VICTORY_MODE:
            OperMode_VictoryMode();
            break;
        case GAME_OVER_MODE:
            OperMode_GameOverMode();
            break;
    }
}

/* ========================================================================
 * TITLE SCREEN MODE
 * ======================================================================== */

void OperMode_TitleScreen(void) {
    switch (g_OperMode_Task) {
        case 0: {
            /* InitializeGame (main.asm:1405-1415) */
            g_DemoTimer = 0x18;
            g_DemoAction = 0;
            g_DemoActionTimer = 0;
            InitializeGame();
            g_OperMode_Task = 1;
            /* The real InitializeGame+InitializeArea clear ~5 KB of RAM.
             * It runs during NMI frame 5 and spans frames 6-7, so those
             * two NMIs only bump FrameCounter (reference itc stays $14
             * across frames 5-7, ScreenRoutineTask 0 first advances on
             * frame 8). */
            g_NMIBusy = 2;
            break;
        }
        case 1:
            /* Run screen routines (tasks 0-14) */
            ScreenRoutines();
            if (g_ScreenRoutineTask >= 15) {
                g_OperMode_Task = 2;
            }
            break;
        case 2:
            /* PrimaryGameSetup (main.asm:1480-1486) */
            g_FetchNewGameTimerFlag = 1;
            g_PlayerSize = 1;
            g_NumberofLives = 2;
            g_OnscreenPlayerInfo[0] = 2;
            g_OffscreenPlayerInfo[0] = 2;
            g_OnscreenPlayerInfo[1] = g_HalfwayPage;
            g_OffscreenPlayerInfo[1] = g_HalfwayPage;
            g_OnscreenPlayerInfo[2] = g_LevelNumber;
            g_OffscreenPlayerInfo[2] = g_LevelNumber;
            g_OnscreenPlayerInfo[5] = g_WorldNumber;
            g_OffscreenPlayerInfo[5] = g_WorldNumber;
            /* SecondaryGameSetup (main.asm:1488-1529) */
            g_DisableScreenFlag = 0;
            memset(g_VRAM_Buffer1, 0, 512);
            g_VRAM_Buffer1_Offset = 0;
            g_GameTimerExpiredFlag = 0;
            g_DisableIntermediate = 0;
            g_BalPlatformAlignment = 0xff; /* SecondaryGameSetup: $03a0 */
            /* PrimaryGameSetup falls through SecondaryGameSetup
             * (main.asm:1480-1518), which installs DefaultSprOffsets. */
            SpriteOffsets_Reset();
            SpriteOffsets_InstallSprite0(g_SpriteData);
            g_Sprite0HitDetectFlag = 1;
            g_OperMode_Task = 3;
            break;
        case 3:
            Title_GameMenuRoutine();
            break;
    }
}

/* ========================================================================
 * GAME MODE
 * ======================================================================== */

void OperMode_GameMode(void) {
    switch (g_OperMode_Task) {
        case 0: /* InitializeArea (main.asm:1417-1476) */
            /* Gameplay treats this coherent ASM routine atomically.  Its CPU
             * runtime may suppress the next video-frame NMI, but canonical
             * state is sampled only at actual NMI entries. */
            InitializeArea();
            g_OperMode_Task++;
            g_NMIBusy = 1;
            break;
        case 1: /* ScreenRoutines */
            ScreenRoutines();
            if (g_ScreenRoutineTask >= 15) {
                g_OperMode_Task = 2;
            }
            break;
        case 2: /* SecondaryGameSetup (main.asm:1488-1529) */
            g_DisableScreenFlag = 0;
            memset(g_VRAM_Buffer1, 0, 512);
            g_VRAM_Buffer1_Offset = 0;
            g_GameTimerExpiredFlag = 0;
            g_DisableIntermediate = 0;
            g_BackloadingFlag = 0;
            g_BalPlatformAlignment = 0xff;
            g_MirrorPPUCtrl1 = (uint8_t)((g_MirrorPPUCtrl1 & 0xfe) |
                                         (g_ScreenLeft_PageLoc & 1));
            GameMode_UpdateAreaMusicQueue();
            SpriteOffsets_Reset();
            SpriteOffsets_InstallSprite0(g_SpriteData);
            g_Sprite0HitDetectFlag = 1;
            g_OperMode_Task = 3;
            break;
        case 3: /* GameCoreRoutine */
            GameMode_MainLoop();
            break;
    }
}

/* ========================================================================
 * VICTORY MODE
 * Level completion sequence (flagpole, tally, world transition)
 * ======================================================================== */

void OperMode_VictoryMode(void) {
    VictoryMode_Tick();
}

/* ========================================================================
 * GAME OVER MODE
 * ======================================================================== */

void OperMode_GameOverMode(void) {
    switch (g_OperMode_Task) {
        case 0: /* SetupGameOver (main.asm:1657-1665) */
            g_ScreenRoutineTask = 0;
            g_Sprite0HitDetectFlag = 0;
            g_EventMusicQueue = GameOverMusic;
            g_DisableScreenFlag++;
            g_OperMode_Task = 1;
            break;
        case 1: /* ScreenRoutines; DisplayIntermediate advances to task 2 */
            ScreenRoutines();
            break;
        case 2: { /* RunGameOver (main.asm:1669-1690) */
            g_DisableScreenFlag = 0;
            if ((g_SavedJoypadBits & BTN_START) == 0 && g_ScreenTimer != 0)
                break;

            GameMode_TerminateGame();
            break;
        }
    }
}
