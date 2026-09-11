/* globals.h - Global variables for SDL build */
/* These mirror the NES RAM layout */

#ifndef SMB_GLOBALS_H
#define SMB_GLOBALS_H

#include "types.h"

/* Screen/scroll positions */
extern uint8_t g_Screen_X_Position;
extern uint8_t g_Screen_Y_Position;

/* Operating mode variables */
extern uint8_t g_OperMode;
extern uint8_t g_GameEngineSubroutine;
extern uint8_t g_OperMode_Task;
extern uint8_t g_ScreenRoutineTask;
extern uint8_t g_VRAM_Buffer_AddrCtrl;
extern uint8_t g_MirrorPPUCtrl1;       /* $0778: Mirror_PPU_CTRL_REG1 */
extern uint8_t g_NMI_FrameCounter;
extern uint16_t g_FrameCounter;
extern uint8_t g_GamePauseStatus;       /* $0776: pause latch/edge state */
extern uint8_t g_GamePauseTimer;        /* $0777: pause debounce timer */
extern uint8_t g_PauseSoundQueue;       /* PauseSoundQueue ($00fa) */
extern uint8_t g_Square1SoundQueue;     /* Square1SoundQueue ($00ff) */
extern uint8_t g_Square2SoundQueue;     /* Square2SoundQueue ($00fe) */
extern uint8_t g_NoiseSoundQueue;       /* NoiseSoundQueue ($00fd) */
extern uint8_t g_EventMusicQueue;       /* EventMusicQueue ($00fc) */
extern uint8_t g_EventMusicBuffer;      /* EventMusicBuffer ($07b1) */
extern uint8_t g_FlagpoleSoundQueue;    /* $0713: FlagpoleSoundQueue */
extern uint8_t g_FlagpoleCollisionYPos; /* $070f: collision Y latch */
extern uint8_t g_FlagpoleScore;         /* $010f: score table index */
extern uint8_t g_FlagpoleFNumYPos;      /* $010d: floatey Y */
extern uint8_t g_FlagpoleFNumYMF_Dummy; /* $010e: floatey fractional Y */
/* $07A7-$07AD are rotated by NMI; $07AE is an adjacent random source read by
 * the largest object offsets but is not part of the seven-byte LFSR chain. */
extern uint8_t g_PseudoRandomBitReg[8];
extern uint8_t g_DemoAction;             /* $0717 */
extern uint8_t g_DemoActionTimer;        /* $0718 */

/* Player variables */
extern uint8_t g_Player_X_Position;
extern uint8_t g_Player_PageLoc;
extern uint8_t g_Player_Y_Position;
extern uint8_t g_Player_Y_HighPos;
extern uint8_t g_Player_SprAttrib;
extern uint8_t g_PlayerSize;
extern uint8_t g_Player_State;
extern uint8_t g_Player_Y_Speed;
extern uint8_t g_Player_X_Speed;
extern uint8_t g_Player_MovingDir;
extern uint8_t g_PlayerFacingDir;
extern uint8_t g_Player_Rel_XPos;
extern uint8_t g_Player_Rel_YPos;
extern uint8_t g_Enemy_Rel_XPos;       /* $03AE: Enemy_Rel_XPos scratch */
extern uint8_t g_Player_BoundBoxCtrl;  /* $0499 Player_BoundBoxCtrl */
extern uint8_t g_Player_BoundingBox[4]; /* $04AC-$04AF BoundingBox_* */
extern uint8_t g_Player_OffscreenBits; /* $03D0 Player_OffscreenBits */
extern uint8_t g_PlayerGfxOffset;       /* $06D5 PlayerGfxOffset */
extern uint8_t g_CurrentPlayer;
extern uint8_t g_PlayerStatus;
extern uint8_t g_PlayerChangeSizeFlag;  /* PlayerChangeSizeFlag */
extern uint8_t g_Player_CollisionBits;  /* $0490 Player_CollisionBits */
extern uint8_t g_DisableCollisionDet;   /* $0716 DisableCollisionDet */

/* Level variables */
extern uint8_t g_AreaType;
extern uint8_t g_AreaPointer;          /* $0750: packed AreaAddrOffsets value */
extern uint8_t g_AreaStyle;
extern uint8_t g_WorldNumber;
extern uint8_t g_LevelNumber;
extern uint8_t g_GameTimerSetting;     /* $0715: GameTimerSetting */
extern uint8_t g_TerrainControl;       /* $0727: TerrainControl */
extern uint8_t g_ForegroundScenery;    /* $0741: ForegroundScenery */
extern uint8_t g_BackgroundScenery;    /* $0742: BackgroundScenery */
extern uint8_t g_CloudTypeOverride;    /* $0743: CloudTypeOverride */
extern uint8_t g_BackgroundColorCtrl;
extern uint8_t g_AltEntranceControl;   /* $0752: alternate entry mode (pipe/vine/cloud) */
extern uint8_t g_EntrancePage;          /* $0751 */
extern uint8_t g_HalfwayPage;           /* $075B */
extern uint8_t g_BackloadingFlag;       /* $0728 */
extern uint8_t g_BehindAreaParserFlag;  /* $0729 */
extern uint8_t g_BlockBufferColumnPos;  /* $06A0 */
extern uint8_t g_CurrentNTAddrLow;      /* $0721 */
extern uint8_t g_CurrentNTAddrHigh;     /* $0720 */
extern uint8_t g_SecondaryHardMode;     /* $06CC */
/* Enemy stream parser state (main.asm: ProcessEnemyData). */
extern uint8_t g_EnemyFrenzyBuffer;     /* $06CB */
extern uint8_t g_NumberofGroupEnemies;  /* $06D3 */
extern uint8_t g_EnemyFrenzyQueue;      /* $06CD */
extern uint8_t g_FireworksCounter;      /* $06D7 */
extern uint8_t g_BitMFilter;             /* $06DD: water Cheep spawn mask */
extern uint8_t g_EnemyDataOffset;       /* $0739 */
extern uint8_t g_EnemyObjectPageLoc;    /* $073A */
extern uint8_t g_EnemyObjectPageSel;    /* $073B */
extern uint8_t g_StaircaseControl;      /* $0734: StaircaseObject step alias */
extern uint8_t g_EnemyDataLow;           /* $00E9: EnemyDataLow / Bubble_Y_Position+5 */
extern uint8_t g_ZeroPageScratch37;      /* $0037: unlabelled zero-page RAM */
extern uint8_t g_ZeroPageScratchEF;      /* $00EF: EnemyGfxHandler scratch */
extern uint8_t g_ZeroPageScratch07;      /* $0007: DecodeAreaData/GetLrgObjAttrib */
extern uint8_t g_EntranceBubbleYMF;      /* $0431: Bubble_YMF_Dummy+5 alias */
extern uint8_t g_BalPlatformAlignment;  /* $03A0 */
extern uint8_t g_Platform_X_Scroll;     /* $03A1 */
extern uint8_t g_AreaMusicQueue;        /* AreaMusicQueue; audio playback excluded */
extern uint8_t g_JoypadOverride;        /* $0758 */
extern uint8_t g_PlayerEntranceCtrl;    /* $0710 */
extern uint8_t g_AreaNumber;             /* $0760 */
extern uint8_t g_DestinationPageLoc;     /* $0034 */
extern uint8_t g_VictoryWalkControl;     /* $0035 */
extern uint8_t g_PrimaryMsgCounter;      /* $0719 */
extern uint8_t g_SecondaryMsgCounter;    /* $0749 */
extern uint8_t g_ScrollFractional;       /* $0768 */
extern uint8_t g_DisableIntermediate;  /* $0769: skip world/lives display */
extern uint8_t g_DisableScreenFlag;    /* gates PPU rendering in NES NMI */
extern uint8_t g_CurrentPageLoc;       /* $0750: current page for area parser */
extern uint8_t g_ColumnSets;           /* $0751: column sets remaining to render */
extern uint8_t g_CurrentColumnPos;     /* $0726: current metatile column */
extern uint8_t g_AreaParserTaskNum;    /* $071F: area parser sub-task count */
extern uint8_t g_LoopCommand;          /* $0745: area loop command latch */
extern uint8_t g_MultiLoopCorrectCntr; /* $06D9: multi-part loop progress */
extern uint8_t g_MultiLoopPassCntr;    /* $06DA: multi-part loop passes */
extern uint8_t g_FetchNewGameTimerFlag;/* $0777 */
extern uint8_t g_Hidden1UpFlag;        /* $075D Hidden1UpFlag (active player record) */
extern uint8_t g_Sprite0HitDetectFlag; /* $0776 */
extern uint8_t g_StarFlagTaskControl;  /* $0746 */
extern uint8_t g_BrickCoinTimerFlag;   /* $06BC BrickCoinTimerFlag */
extern uint8_t g_BlockResidualCounter;  /* $03F0 Block_ResidualCounter */
#define g_DemoTimer (g_Timers[TIMER_DEMO])

/* Scroll state (scroll.asm) */
extern uint8_t g_ScreenLeft_X_Pos;     /* X pixel position of left edge of screen */
extern uint8_t g_ScreenLeft_PageLoc;   /* Page number of left edge */
extern uint8_t g_ScreenRight_X_Pos;    /* $071d: right edge from GetScreenPosition */
extern uint8_t g_ScreenRight_PageLoc;  /* $071b: right edge page */
extern uint8_t g_HorizontalScroll;     /* PPU scroll register value (= ScreenLeft_X_Pos) */
extern uint8_t g_ScrollAmount;         /* How much scrolled this frame */
extern uint8_t g_ScrollThirtyTwo;      /* $073d: accumulated scroll threshold */
extern uint8_t g_ScrollLock;           /* Scroll lock flag */
extern uint8_t g_Player_Pos_ForScroll; /* Player's screen X position for scroll calc */

/* Input state */
extern uint8_t g_SavedJoypadBits;
extern uint8_t g_SavedJoypad2Bits;       /* $06FD: second controller latch */
extern uint8_t g_PrevJoypadBits;          /* raw prior saved input (diagnostic) */
extern uint8_t g_JoypadBitMask[2];       /* $074A-$074B: edge-filter latch */
/* PlayerCtrlRoutine's controller subsets ($000A-$000C).  These are
 * separate RAM latches; Down-on-ground clears only the directional latches,
 * not SavedJoypadBits, just as the 6502 routine does. */
extern uint8_t g_A_B_Buttons;             /* $000A */
extern uint8_t g_PreviousA_B_Buttons;     /* $000D: SaveAB edge-history latch */
extern uint8_t g_Up_Down_Buttons;         /* $000B */
extern uint8_t g_Left_Right_Buttons;      /* $000C */

/* Player physics */
extern uint8_t g_Player_X_MoveForce;
extern uint8_t g_Player_X_MF2;
extern uint8_t g_ColorRotateOffset;
extern uint8_t g_Player_X_Scroll;
extern uint8_t g_Player_Y_MoveForce;
extern uint8_t g_Player_YMF_Dummy;
extern uint8_t g_Player_XSpeedAbsolute;
extern uint8_t g_VerticalForce;
extern uint8_t g_FallMForce;
extern uint8_t g_JumpspringForce;    /* $06DB: JumpspringForce */
extern uint8_t g_JumpspringAnimCtrl; /* $070E: JumpspringAnimCtrl */
extern uint8_t g_JumpOrigin_Y;
extern uint8_t g_JumpOrigin_Y_HighPos;
extern uint8_t g_DiffToHaltJump;
extern uint8_t g_RunningSpeed;
extern uint8_t g_PlayerAnimCtrl;
extern uint8_t g_PlayerAnimTimerSet;
extern uint8_t g_FireballThrowingTimer; /* $0711 FireballThrowingTimer */
extern uint8_t g_DeathMusicLoaded;      /* $0712 DeathMusicLoaded */
extern uint8_t g_FireballCounter;       /* $06CE FireballCounter */
extern uint8_t g_CrouchingFlag;
extern uint8_t g_SwimmingFlag;
extern uint8_t g_WhirlpoolFlag;         /* $047D Whirlpool_Flag */
extern uint8_t g_WhirlpoolOffset;       /* $046A Whirlpool_Offset */
extern uint8_t g_MaximumLeftSpeed;
extern uint8_t g_MaximumRightSpeed;

/* Per-frame/interval timers (NES layout, see defs.h TIMER_* indices).
 * The array holds all Timers indexed by TIMER_* symbols; access via the
 * convenience macros below so existing code keeps working.
 */
extern uint8_t g_Timers[TIMERS_ARRAY_SIZE];
extern uint8_t g_TimerControl;
extern uint8_t g_IntervalTimerControl;

/* Convenience accessors — Timers array indexed by NES-offset symbol.
 * These let existing reads/writes of g_ScreenTimer etc. transparently use
 * the array, matching the NES single memory block at $0780.
 */
#define g_ScreenTimer        (g_Timers[TIMER_SCREEN])
#define g_PlayerAnimTimer    (g_Timers[TIMER_PLAYER_ANIM])
#define g_JumpSwimTimer      (g_Timers[TIMER_JUMP_SWIM])
#define g_RunningTimer       (g_Timers[TIMER_RUNNING])
#define g_InjuryTimer        (g_Timers[TIMER_INJURY])
#define g_StarInvincibleTimer (g_Timers[TIMER_STAR_INVINCIBLE])
#define g_SelectTimer        (g_Timers[TIMER_SELECT])
#define g_SideCollisionTimer (g_Timers[TIMER_SIDE_COLLISION])
#define g_BlockBounceTimer   (g_Timers[TIMER_BLOCK_BOUNCE])
#define g_ClimbSideTimer     (g_Timers[TIMER_CLIMB_SIDE])
#define g_GameTimerCtrlTimer (g_Timers[TIMER_GAME_TIMER_CTRL])
#define g_FrenzyEnemyTimer   (g_Timers[TIMER_FRENZY_ENEMY])
#define g_BowserFireBreathTimer (g_Timers[TIMER_BOWSER_FIRE_BREATH])
#define g_StompTimer         (g_Timers[TIMER_STOMP])
#define g_AirBubbleTimer     (g_Timers[TIMER_AIR_BUBBLE])
#define g_ScrollIntervalTimer (g_Timers[TIMER_SCROLL_INTERVAL])
#define g_EnemyFrameTimer    (&g_Timers[TIMER_ENEMY_FRAME_BASE])
#define g_EnemyIntervalTimer (&g_Timers[TIMER_ENEMY_INTERVAL_BASE])
#define g_BrickCoinTimer     (g_Timers[TIMER_BRICK_COIN])
#define g_WorldEndTimer      (g_Timers[TIMER_WORLD_END])

/* VRAM buffers */
extern uint8_t g_VRAM_Buffer1[];
extern uint8_t g_VRAM_Buffer1_Offset;
extern uint8_t g_VRAM_Buffer2[];
extern uint8_t g_VRAM_Buffer2_Offset;

/* Game state variables */
extern uint16_t g_PlayerScore;
extern uint8_t g_CoinTally;
extern uint8_t g_NumberofLives;
extern uint8_t g_CoinTallyFor1Ups; /* $0748, cleared per area */
extern uint8_t g_DigitModifier[7];  /* $0133-$0139, DigitModifier is +1 */
extern uint8_t g_NumberOfPlayers;
/* OnscreenPlayerInfo/OffscreenPlayerInfo: seven-byte records at $075A/$0761
 * (lives, halfway, level, hidden-1up, coins, world, area). */
extern uint8_t g_OnscreenPlayerInfo[7];
extern uint8_t g_OffscreenPlayerInfo[7];
extern uint8_t g_WorldSelectNumber;      /* $076B */
extern uint8_t g_WorldSelectEnableFlag;  /* $07FC */
extern uint8_t g_ContinueWorld;          /* $07FD */
extern uint8_t g_PrimaryHardMode;        /* $076A */
extern uint8_t g_ChangeAreaTimer;         /* $06DE */
extern uint8_t g_WarpZoneControl;         /* $06D6 */
extern uint8_t g_VineHeight;               /* $0399 VineHeight */
extern uint8_t g_VineFlagOffset;           /* $0398 VineFlagOffset */
extern uint8_t g_VineObjOffset[3];         /* $039a VineObjOffset */
extern uint8_t g_VineStartY;               /* $039d VineStart_Y_Position */

/* Timer and display variables */
extern uint8_t g_GameTimerExpiredFlag;
extern uint8_t g_DisplayDigits[36];

/* OAM sprite data (64 sprites × 4 bytes = 256 bytes) */
extern uint8_t g_SpriteData[256];

#endif /* SMB_GLOBALS_H */
