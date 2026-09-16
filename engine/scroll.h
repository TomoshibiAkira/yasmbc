/* engine/scroll.h - Camera scrolling system declarations */

#ifndef SMB_SCROLL_H
#define SMB_SCROLL_H

#include "constants/types.h"

/* ========================================================================
 * EXTERNAL VARIABLES
 * ======================================================================== */

extern uint8_t g_Camera_X_Position;
extern uint8_t g_Camera_Y_Position;
extern uint8_t g_Camera_PageLoc;
extern uint8_t g_ScrollLock;
extern uint8_t g_ScrollLockVertical;
extern int16_t g_Background_X;

/* ========================================================================
 * SCROLL CONSTANTS
 * ======================================================================== */

#define SCROLL_LOCK_NONE      0x00
#define SCROLL_LOCK_LEFT      0x01
#define SCROLL_LOCK_RIGHT     0x02
#define SCROLL_LOCK_VERTICAL  0x04

#define SCROLL_BOUNDARY_LEFT  0x40
#define SCROLL_THRESHOLD_RIGHT 0x80

/* ========================================================================
 * FUNCTION DECLARATIONS
 * ======================================================================== */

void Scroll_Update(void);
int Scroll_LoadRom(void);
int Scroll_AreaParserDue(void);
void Scroll_RunParserTask(void);
void Scroll_AreaParserSelectColumn(uint8_t index);
void Scroll_AreaParserRender(uint8_t right_half);
void Scroll_AreaParserComplete(void);
void Scroll_Reset(void);
void Scroll_ApplyLoopback(void);
void Scroll_Advance(uint8_t amount);

#endif /* SMB_SCROLL_H */
