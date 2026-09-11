/* engine/misc.h - air-bubble and whirlpool object ownership */

#ifndef SMB_MISC_H
#define SMB_MISC_H

#include "constants/types.h"

void Misc_Reset(void);
int Misc_LoadRom(void);
void Misc_SetupEntranceBubble(uint8_t slot);
void Misc_SetupEntranceBubbleAlias(void);
void Misc_ProcAirBubbles(void);
/* DrawTitleScreen/ClearBuffersDrawIcon also address the physical
 * Bubble_YMF_Dummy bytes at $042c-$042e through their VRAM copy/clear loops. */
void Misc_SetBubbleYmf(uint8_t slot, uint8_t value);

typedef struct MiscBubbleState {
    uint8_t page;       /* Bubble_PageLoc ($83+x) */
    uint8_t x;          /* Bubble_X_Position ($9c+x) */
    uint8_t y_high;     /* Bubble_Y_HighPos ($cb+x) */
    uint8_t y;          /* Bubble_Y_Position ($e4+x) */
    uint8_t y_mf_dummy; /* Bubble_YMF_Dummy ($042c+x) */
} MiscBubbleState;

void Misc_GetBubbleState(MiscBubbleState states[3]);
void Misc_RegisterWhirlpool(uint8_t page, uint8_t left, uint8_t length);
typedef struct MiscCannonRecord {
    uint8_t page;       /* Cannon_PageLoc / Whirlpool_PageLoc ($046b) */
    uint8_t x_or_left;  /* Cannon_X_Position / Whirlpool_LeftExtent ($0471) */
    uint8_t y_or_length;/* Cannon_Y_Position / Whirlpool_Length ($0477) */
    uint8_t timer;      /* Cannon_Timer / Whirlpool_Flag ($047d) */
} MiscCannonRecord;

void Misc_RegisterCannon(uint8_t page, uint8_t x, uint8_t y);
const MiscCannonRecord *Misc_GetCannon(uint8_t slot);
void Misc_SetCannonTimer(uint8_t slot, uint8_t value);
void Misc_ProcessWhirlpools(void);

#endif /* SMB_MISC_H */
