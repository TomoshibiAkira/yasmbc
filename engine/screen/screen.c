/* screen.c - Screen routines dispatcher (from routines.asm) */

#include "screen.h"
#include "routine/init.h"
#include "routine/colors.h"
#include "routine/intermediate.h"
#include "routine/hud.h"
#include "routine/title.h"
#include "routine/area_parser.h"
#include "constants/globals.h"

/* Jump table for screen routines */
typedef void (*ScreenRoutine)(void);

static const ScreenRoutine g_ScreenRoutineTable[] = {
    InitScreen,                   /* 0 */
    SetupIntermediate,            /* 1 */
    WriteTopStatusLine,           /* 2 */
    WriteBottomStatusLine,        /* 3 */
    DisplayTimeUp,                /* 4 */
    ResetSpritesAndScreenTimer,   /* 5 */
    DisplayIntermediate,          /* 6 */
    ResetSpritesAndScreenTimer,   /* 7 (same as 5) */
    AreaParserTaskControl,        /* 8 */
    GetAreaPalette,               /* 9 */
    GetBGPlayerColor,             /* 10 */
    GetAlternatePalette1,         /* 11 */
    DrawTitleScreen,              /* 12 */
    ClearBuffersDrawIcon,         /* 13 */
    WriteTopScore                 /* 14 */
};

/* ========================================================================
 * MAIN SCREEN DISPATCHER
 * ======================================================================== */

void ScreenRoutines(void) {
    /* Run the current screen routine based on task number */
    uint8_t task = g_ScreenRoutineTask;
    uint8_t max_task = sizeof(g_ScreenRoutineTable) / sizeof(ScreenRoutine);

    if (task < max_task) {
        g_ScreenRoutineTable[task]();
    }
    /* If task >= max_task, just skip (task will be reset by caller) */
}

/* ========================================================================
 * HELPER FUNCTIONS
 * ======================================================================== */

void Screen_IncTask(void) {
    g_ScreenRoutineTask++;
}

void Screen_ResetTask(void) {
    g_ScreenRoutineTask = TASK_INIT_SCREEN;
}

void Screen_SetTask(uint8_t task) {
    g_ScreenRoutineTask = task;
}

uint8_t Screen_GetTask(void) {
    return g_ScreenRoutineTask;
}
