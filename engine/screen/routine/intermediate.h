/* intermediate.h - Intermediate screen setup routines */

#ifndef INTERMEDIATE_H
#define INTERMEDIATE_H

#include "constants/types.h"

void SetupIntermediate(void);
void Screen_WriteGameText(uint8_t textnum);
void DisplayIntermediate(void);
void DisplayTimeUp(void);
void ResetSpritesAndScreenTimer(void);
int Intermediate_LoadRom(void);

#endif /* INTERMEDIATE_H */
