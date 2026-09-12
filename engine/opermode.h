/* opermode.h - Operating mode handler declarations */

#ifndef SMB_OPMODE_H
#define SMB_OPMODE_H

#include "constants/types.h"

/* ========================================================================
 * GLOBAL VARIABLES (regular C variables for SDL, 
 * mapped to memory addresses for NES)
 * ======================================================================== */

/* Current operating mode (title, game, victory, game over) */
extern uint8_t g_OperMode;

/* Current game engine subroutine */
extern uint8_t g_GameEngineSubroutine;

/* Current task within operating mode */
extern uint8_t g_OperMode_Task;

/* Screen-specific task */
extern uint8_t g_ScreenRoutineTask;

/* VRAM buffer control */
extern uint8_t g_VRAM_Buffer_AddrCtrl;

/* Frame counter */
extern uint16_t g_FrameCounter;

/* ========================================================================
 * FUNCTION DECLARATIONS
 * ======================================================================== */

/* Set the current operating mode */
void OperMode_SetMode(uint8_t mode);

/* Execute tasks for current operating mode */
void OperMode_Tasks(void);

/* Individual mode handlers */
void OperMode_TitleScreen(void);
void OperMode_GameMode(void);
void OperMode_VictoryMode(void);
void OperMode_GameOverMode(void);

/* ========================================================================
 * GAME STATE VARIABLES
 * ======================================================================== */

/* Player data */
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

/* Game state */
extern uint8_t g_NumberOfPlayers;
extern uint8_t g_CurrentPlayer;
extern uint8_t g_WorldNumber;
extern uint8_t g_LevelNumber;
extern uint8_t g_NumberofLives;
extern uint16_t g_PlayerScore;
extern uint8_t g_CoinTally;

#endif /* SMB_OPMODE_H */
