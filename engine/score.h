/* score.h - score, coin, floatey-number and top-score routines
 *
 * The display digits are the NES representation of score state.  These
 * routines intentionally operate on that RAM bank instead of introducing a
 * host-side numeric score as a second source of truth.
 */

#ifndef SCORE_H
#define SCORE_H

#include <stdint.h>

extern uint8_t Score_FloateyNumTileData[24];
extern uint8_t ScoreUpdateData[12];
int Score_LoadRomTables(void);

void Score_Reset(void);
void Score_SetDigitModifier(uint8_t digit, uint8_t amount);
void Score_DigitsMath(uint8_t display_lsd);
void Score_AddToScore(void);
void Score_AwardGameTimerPoints(void);
void Score_AwardPoints(uint8_t digit, uint8_t amount);
void Score_GiveOneCoin(void);
void Score_IncrementAreaCoinTally(void);
void Score_ProcessFloateyReward(uint8_t control);
void Score_UpdateTopScore(void);

#endif /* SCORE_H */
