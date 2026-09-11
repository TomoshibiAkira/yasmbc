/* globals.c - Global variables for SDL build */

#include "defs.h"
#include "globals.h"

/* Screen/scroll positions */
uint8_t g_Screen_X_Position = 0;
uint8_t g_Screen_Y_Position = 0;

/* Operating mode variables */
uint8_t g_OperMode = TITLE_SCREEN_MODE;
uint8_t g_GameEngineSubroutine = 0;
uint8_t g_OperMode_Task = 0;
uint8_t g_ScreenRoutineTask = 0;
uint8_t g_VRAM_Buffer_AddrCtrl = 0;
uint8_t g_MirrorPPUCtrl1 = 0x10; /* init.asm/InitializeNameTables */
uint8_t g_NMI_FrameCounter = 0;
uint16_t g_FrameCounter = 0;
uint8_t g_GamePauseStatus = 0;
uint8_t g_GamePauseTimer = 0;
uint8_t g_PauseSoundQueue = 0;
uint8_t g_Square1SoundQueue = 0;
uint8_t g_Square2SoundQueue = 0;
uint8_t g_NoiseSoundQueue = 0;
uint8_t g_EventMusicQueue = 0;
uint8_t g_EventMusicBuffer = 0;
uint8_t g_FlagpoleSoundQueue = 0;
uint8_t g_FlagpoleCollisionYPos = 0;
uint8_t g_FlagpoleScore = 0;
uint8_t g_FlagpoleFNumYPos = 0;
uint8_t g_FlagpoleFNumYMF_Dummy = 0;
uint8_t g_DemoAction = 0;
uint8_t g_DemoActionTimer = 0;
/* ColdBoot seeds only byte 0 with $A5; the remaining InitializeMemory-cleared
 * bytes stay zero (system/init.asm:31-34, PseudoRandomBitReg=$07A7). */
uint8_t g_PseudoRandomBitReg[8] = {0xA5, 0, 0, 0, 0, 0, 0, 0};

/* Player variables */
uint8_t g_Player_X_Position = 0;
uint8_t g_Player_PageLoc = 0;
uint8_t g_Player_Y_Position = 0;
uint8_t g_Player_Y_HighPos = 0;
uint8_t g_Player_SprAttrib = 0;
uint8_t g_PlayerSize = PLAYER_SIZE_SMALL;
uint8_t g_Player_State = PLAYER_STATE_GROUND;
uint8_t g_Player_Y_Speed = 0;
uint8_t g_Player_X_Speed = 0;
uint8_t g_Player_MovingDir = 0;
uint8_t g_PlayerFacingDir = 0;
uint8_t g_Player_Rel_XPos = 0;
uint8_t g_Player_Rel_YPos = 0;
uint8_t g_Enemy_Rel_XPos = 0;
uint8_t g_Player_BoundBoxCtrl = 1;
uint8_t g_Player_BoundingBox[4] = {0, 0, 0, 0};
uint8_t g_Player_OffscreenBits = 0;
uint8_t g_PlayerGfxOffset = 0;
uint8_t g_CurrentPlayer = 0;
uint8_t g_PlayerStatus = PLAYER_STATUS_SMALL;
uint8_t g_PlayerChangeSizeFlag = 0;
uint8_t g_Player_CollisionBits = 0;
uint8_t g_DisableCollisionDet = 0;

/* Level variables */
uint8_t g_AreaType = AREA_TYPE_OVERWORLD;
uint8_t g_AreaPointer = 0;
uint8_t g_AreaStyle = 0;
uint8_t g_WorldNumber = WORLD_1;
uint8_t g_LevelNumber = 0;
uint8_t g_GameTimerSetting = 0;
uint8_t g_TerrainControl = 0;
uint8_t g_ForegroundScenery = 0;
uint8_t g_BackgroundScenery = 0;
uint8_t g_CloudTypeOverride = 0;
uint8_t g_BackgroundColorCtrl = 0;
uint8_t g_AltEntranceControl = 0;
uint8_t g_EntrancePage = 0;
uint8_t g_HalfwayPage = 0;
uint8_t g_BackloadingFlag = 0;
uint8_t g_BehindAreaParserFlag = 0;
uint8_t g_BlockBufferColumnPos = 0;
uint8_t g_CurrentNTAddrLow = 0;
uint8_t g_CurrentNTAddrHigh = 0;
uint8_t g_SecondaryHardMode = 0;
uint8_t g_EnemyFrenzyBuffer = 0;
uint8_t g_NumberofGroupEnemies = 0;
uint8_t g_EnemyFrenzyQueue = 0;
uint8_t g_FireworksCounter = 0;
uint8_t g_BitMFilter = 0;
uint8_t g_EnemyDataOffset = 0;
uint8_t g_EnemyObjectPageLoc = 0;
uint8_t g_EnemyObjectPageSel = 0;
uint8_t g_StaircaseControl = 0; /* $0734 StaircaseObject global step alias */
uint8_t g_EnemyDataLow = 0;       /* $00E9 EnemyDataLow */
uint8_t g_ZeroPageScratch37 = 0;  /* $0037 unlabelled zero-page RAM */
uint8_t g_ZeroPageScratchEF = 0;  /* $00EF EnemyGfxHandler scratch */
uint8_t g_ZeroPageScratch07 = 0;  /* $0007 DecodeAreaData/GetLrgObjAttrib */
uint8_t g_EntranceBubbleYMF = 0;  /* $0431 Bubble_YMF_Dummy+5 */
uint8_t g_BalPlatformAlignment = 0;
uint8_t g_Platform_X_Scroll = 0;
uint8_t g_AreaMusicQueue = 0;
uint8_t g_JoypadOverride = 0;
uint8_t g_PlayerEntranceCtrl = 0;
uint8_t g_AreaNumber = 0;
uint8_t g_DestinationPageLoc = 0;
uint8_t g_VictoryWalkControl = 0;
uint8_t g_PrimaryMsgCounter = 0;
uint8_t g_SecondaryMsgCounter = 0;
uint8_t g_ScrollFractional = 0;
uint8_t g_DisableIntermediate = 0;
uint8_t g_DisableScreenFlag = 1;  /* init.asm:40 — screen disabled until SecondaryGameSetup */
uint8_t g_CurrentPageLoc = 0;
uint8_t g_ColumnSets = 0;
uint8_t g_CurrentColumnPos = 0;
uint8_t g_AreaParserTaskNum = 0;
uint8_t g_LoopCommand = 0;
uint8_t g_MultiLoopCorrectCntr = 0;
uint8_t g_MultiLoopPassCntr = 0;
uint8_t g_FetchNewGameTimerFlag = 0;
uint8_t g_Hidden1UpFlag = 0;
uint8_t g_Sprite0HitDetectFlag = 0;
uint8_t g_StarFlagTaskControl = 0;
uint8_t g_BrickCoinTimerFlag = 0; /* $06BC BrickCoinTimerFlag */
uint8_t g_BlockResidualCounter = 0; /* $03F0 Block_ResidualCounter */

/* Scroll state */
uint8_t g_ScreenLeft_X_Pos = 0;
uint8_t g_ScreenLeft_PageLoc = 0;
uint8_t g_ScreenRight_X_Pos = 0;
uint8_t g_ScreenRight_PageLoc = 0;
uint8_t g_HorizontalScroll = 0;
uint8_t g_ScrollAmount = 0;
uint8_t g_ScrollThirtyTwo = 0;
uint8_t g_ScrollLock = 0;
uint8_t g_Player_Pos_ForScroll = 0;

/* VRAM buffers */
uint8_t g_VRAM_Buffer1[512] = {0};
uint8_t g_VRAM_Buffer1_Offset = 0;
uint8_t g_VRAM_Buffer2[512] = {0};
uint8_t g_VRAM_Buffer2_Offset = 0;

/* Input state */
uint8_t g_SavedJoypadBits = 0;
uint8_t g_SavedJoypad2Bits = 0;
uint8_t g_PrevJoypadBits = 0;
uint8_t g_JoypadBitMask[2] = {0, 0};
uint8_t g_A_B_Buttons = 0;
uint8_t g_PreviousA_B_Buttons = 0;
uint8_t g_Up_Down_Buttons = 0;
uint8_t g_Left_Right_Buttons = 0;

/* Player physics */
uint8_t g_Player_X_MoveForce = 0;
uint8_t g_Player_X_MF2 = 0;  /* $0400 SprObject frac accumulator */
uint8_t g_ColorRotateOffset = 0;
uint8_t g_Player_X_Scroll = 0;  /* $06FF pixels moved this frame */
uint8_t g_Player_Y_MoveForce = 0;
uint8_t g_Player_YMF_Dummy = 0;
uint8_t g_Player_XSpeedAbsolute = 0;
uint8_t g_VerticalForce = 0;
uint8_t g_FallMForce = 0;
uint8_t g_JumpspringForce = 0;
uint8_t g_JumpspringAnimCtrl = 0;
uint8_t g_JumpOrigin_Y = 0;
uint8_t g_JumpOrigin_Y_HighPos = 0;
uint8_t g_DiffToHaltJump = 0;
uint8_t g_RunningSpeed = 0;
uint8_t g_PlayerAnimCtrl = 0;
uint8_t g_PlayerAnimTimerSet = 0;
uint8_t g_FireballThrowingTimer = 0;
uint8_t g_DeathMusicLoaded = 0;
uint8_t g_FireballCounter = 0;
uint8_t g_CrouchingFlag = 0;
uint8_t g_SwimmingFlag = 0;
uint8_t g_WhirlpoolFlag = 0;
uint8_t g_WhirlpoolOffset = 0;
uint8_t g_MaximumLeftSpeed = 0;
uint8_t g_MaximumRightSpeed = 0;

/* Per-frame/interval timer array (NES Timers at $0780).
 * All timers are zeroed on startup; gameplay code reinitializes them as needed.
 * g_TimerControl gates all timer decrements (matches NES TimerControl).
 * g_IntervalTimerControl gates interval timers; reloaded to 20 each expiry.
 */
uint8_t g_Timers[TIMERS_ARRAY_SIZE] = {0};
uint8_t g_TimerControl = 0;
/* Power-on value 0: first NMI's DecTimers wraps it immediately and
 * reloads $14 (reference itc trace: 00 at frames 3-4, 14 from frame 5). */
uint8_t g_IntervalTimerControl = 0;

/* Game state variables */
uint16_t g_PlayerScore = 0;
uint8_t g_CoinTally = 0;
uint8_t g_NumberofLives = 3;
uint8_t g_CoinTallyFor1Ups = 0; /* $0748 CoinTallyFor1Ups */
/* DigitModifier-1 through DigitModifier+5: $0133-$0139. */
uint8_t g_DigitModifier[7] = {0};
/* NumberOfPlayers is the original zero/one flag: 0=1P, 1=2P. */
uint8_t g_NumberOfPlayers = 0;
uint8_t g_OnscreenPlayerInfo[7] = {3, 0, 0, 0, 0, WORLD_1, 0};
uint8_t g_OffscreenPlayerInfo[7] = {3, 0, 0, 0, 0, WORLD_1, 0};
uint8_t g_WorldSelectNumber = 0;
uint8_t g_WorldSelectEnableFlag = 0;
uint8_t g_ContinueWorld = 0;
uint8_t g_PrimaryHardMode = 0;
uint8_t g_ChangeAreaTimer = 0;
uint8_t g_WarpZoneControl = 0;
uint8_t g_VineFlagOffset = 0;
uint8_t g_VineHeight = 0;
uint8_t g_VineObjOffset[3] = {0, 0, 0};
uint8_t g_VineStartY = 0;

/* Timer and display variables */
uint8_t g_GameTimerExpiredFlag = 0;
uint8_t g_DisplayDigits[36] = {0};

/* OAM sprite data (64 sprites × 4 bytes = 256 bytes) */
uint8_t g_SpriteData[256] = {0};
