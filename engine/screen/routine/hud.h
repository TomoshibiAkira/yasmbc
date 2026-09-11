/* hud.h - Heads-up display routines */

#ifndef HUD_H
#define HUD_H

#include <stdint.h>

void WriteTopStatusLine(void);
void WriteBottomStatusLine(void);
void WriteTopScore(void);

#endif /* HUD_H */

/* Per-frame HUD refresh (UpdateNumber path, main.asm:4070-4086) */
void UpdateHUD(void);

/* Game timer display init (Entrance_GameTimerSetup) */
void HUD_SetGameTimer(uint8_t hundreds);
void HUD_PrintStatusBarNumbers(uint8_t nybbles);
void HUD_UpdateNumber(uint8_t nybbles);
int Hud_LoadRom(void);
