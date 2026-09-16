/* screen.h - Screen routines dispatcher */

#ifndef SCREEN_H
#define SCREEN_H

#include "constants/types.h"
#include "constants/defs.h"

/* Screen routine tasks */
#define TASK_INIT_SCREEN           0
#define TASK_SETUP_INTERMEDIATE    1
#define TASK_WRITE_TOP_STATUS      2
#define TASK_WRITE_BOTTOM_STATUS   3
#define TASK_DISPLAY_TIME_UP       4
#define TASK_RESET_SPRITES_TIMER   5
#define TASK_DISPLAY_INTERMEDIATE  6
#define TASK_RESET_SPRITES_TIMER2  7
#define TASK_AREA_PARSER           8
#define TASK_GET_AREA_PALETTE      9
#define TASK_GET_BG_PLAYER_COLOR   10
#define TASK_GET_ALT_PALETTE       11
#define TASK_DRAW_TITLE            12
#define TASK_CLEAR_BUFFERS_ICON    13
#define TASK_WRITE_TOP_SCORE       14

/* External functions from routines */
void InitScreen(void);
void SetupIntermediate(void);
void WriteTopStatusLine(void);
void WriteBottomStatusLine(void);
void DisplayTimeUp(void);
void ResetSpritesAndScreenTimer(void);
void DisplayIntermediate(void);
void AreaParserTaskControl(void);
void GetAreaPalette(void);
void GetBGPlayerColor(void);
void GetAlternatePalette1(void);
void DrawTitleScreen(void);
void ClearBuffersDrawIcon(void);
void WriteTopScore(void);

/* Helper functions - declared here and defined in screen.c */
void Screen_IncTask(void);
void Screen_ResetTask(void);
void Screen_SetTask(uint8_t task);
uint8_t Screen_GetTask(void);
void Screen_MoveAllSpritesOffscreen(void);

/* External global variables from opermode/globals */
extern uint8_t g_ScreenRoutineTask;
extern uint8_t g_OperMode;
extern uint8_t g_AreaType;
extern uint8_t g_BackgroundColorCtrl;
extern uint8_t g_AreaStyle;
extern uint8_t g_CurrentPlayer;
extern uint8_t g_PlayerStatus;
extern uint8_t g_VRAM_Buffer_AddrCtrl;
extern uint8_t g_VRAM_Buffer1[];
extern uint8_t g_VRAM_Buffer1_Offset;
extern uint8_t g_WorldNumber;
extern uint8_t g_LevelNumber;
extern uint8_t g_NumberOfPlayers;
extern uint8_t g_OperMode_Task;
extern uint8_t g_GameTimerExpiredFlag;
extern uint8_t g_ScreenTimer;
extern uint8_t g_DisplayDigits[];

/* Main screen dispatcher */
void ScreenRoutines(void);

#endif /* SCREEN_H */
