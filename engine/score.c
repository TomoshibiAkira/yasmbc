/* score.c - the shared score/coin/floatey chain from main.asm
 *
 * Source routines:
 *   FloateyNumbersRoutine  $00b0-$00ff
 *   DigitsMathRoutine      main.asm:1331-1364
 *   UpdateTopScore         main.asm:1368-1392
 *   GiveOneCoin/AddToScore main.asm:4048-4074
 *
 * DisplayDigits ($07d7) is six top-score digits, two six-digit player-score
 * banks, then the game timer.  DigitModifier ($0134-$0139) is represented by
 * g_DigitModifier[1..6]; index zero is the byte at $0133 which the 6502
 * carry/borrow branches touch as DigitModifier-1.
 */

#include <string.h>

#include "score.h"
#include "assets.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "screen/routine/hud.h"

uint8_t Score_FloateyNumTileData[24];
uint8_t ScoreUpdateData[12];
static uint8_t CoinTallyOffsets[2];
static uint8_t ScoreOffsets[2];
static uint8_t StatusBarNybbles[2];

int Score_LoadRomTables(void) {
    if (Assets_Copy("tables/floatey_num_tiles.bin", Score_FloateyNumTileData,
                    sizeof(Score_FloateyNumTileData)) ||
        Assets_Copy("tables/score_update.bin", ScoreUpdateData,
                    sizeof(ScoreUpdateData)) ||
        Assets_Copy("tables/coin_tally_offsets.bin", CoinTallyOffsets,
                    sizeof(CoinTallyOffsets)) ||
        Assets_Copy("tables/score_offsets.bin", ScoreOffsets,
                    sizeof(ScoreOffsets)) ||
        Assets_Copy("tables/status_bar_nybbles.bin", StatusBarNybbles,
                    sizeof(StatusBarNybbles)))
        return -1;
    return 0;
}

void Score_Reset(void) {
    memset(g_DigitModifier, 0, sizeof(g_DigitModifier));
}

void Score_SetDigitModifier(uint8_t digit, uint8_t amount) {
    if (digit < 6) g_DigitModifier[digit + 1] = amount;
}

/* DigitsMathRoutine.  The loop order and the signed-result test are kept
 * byte-for-byte in spirit: ADC wraps to eight bits, BMI is tested before the
 * carry-at-ten branch, and the preceding modifier byte receives carry or
 * borrow. */
void Score_DigitsMath(uint8_t display_lsd) {
    int x;
    uint8_t y = display_lsd;

    if (g_OperMode != TITLE_SCREEN_MODE) {
        for (x = 5; x >= 0; x--, y--) {
            uint8_t sum = (uint8_t)(g_DigitModifier[x + 1] +
                                    g_DisplayDigits[y]);
            if ((sum & 0x80) != 0) {
                g_DigitModifier[x]--;
                sum = 9;
            } else if (sum >= 10) {
                sum = (uint8_t)(sum - 10);
                g_DigitModifier[x]++;
            }
            g_DisplayDigits[y] = sum;
        }
    }

    /* EraseDMods clears DigitModifier-1 through DigitModifier+5 even when
     * title mode suppresses the arithmetic. */
    memset(g_DigitModifier, 0, sizeof(g_DigitModifier));
}

/* AddToScore falls through to GetSBNybbles/UpdateNumber in the ROM. */
void Score_AddToScore(void) {
    uint8_t player = (uint8_t)(g_CurrentPlayer & 1);
    Score_DigitsMath(ScoreOffsets[player]);
    HUD_UpdateNumber(StatusBarNybbles[player]);
}

/* AwardGameTimerPoints (main.asm:7663-7694): subtract one timer unit,
 * award 50 points to the active score bank, then select the timer and score
 * status-bar entries with A=(CurrentPlayer << 4)|$04.  The latter selector is
 * intentionally not the generic AddToScore $02/$13 pair. */
void Score_AwardGameTimerPoints(void) {
    uint8_t player = (uint8_t)(g_CurrentPlayer & 1);

    Score_DigitsMath(ScoreOffsets[player]);
    HUD_UpdateNumber((uint8_t)((player << 4) | 0x04));
}

void Score_AwardPoints(uint8_t digit, uint8_t amount) {
    Score_SetDigitModifier(digit, amount);
    Score_AddToScore();
}

/* GiveOneCoin (main.asm:4048-4062): update the active player's coin digits
 * and the CoinTally byte at $075e, then award 200 points.  The SDL scalar
 * remains convenient for callers, but the seven-byte player record is the
 * physical NES owner that survives the next NMI and player transpose. */
void Score_GiveOneCoin(void) {
    uint8_t player = (uint8_t)(g_CurrentPlayer & 1);

    Score_SetDigitModifier(5, 1);
    Score_DigitsMath(CoinTallyOffsets[player]);

    g_CoinTally++;
    if (g_CoinTally == 100) {
        g_CoinTally = 0;
        g_NumberofLives++;
        /* GiveOneCoin's INC NumberofLives targets active RAM $075a. */
        g_OnscreenPlayerInfo[0] = g_NumberofLives;
        /* SoundEngine is excluded; preserve the original queue write. */
        g_Square2SoundQueue = Sfx_ExtraLife;
    }
    g_OnscreenPlayerInfo[4] = g_CoinTally; /* CoinTally=$075e */

    Score_SetDigitModifier(4, 2);
    Score_AddToScore();
}

/* CoinTallyFor1Ups ($0748) is an area-local hidden tally, distinct from the
 * active player's onscreen CoinTally ($075e). */
void Score_IncrementAreaCoinTally(void) {
    g_CoinTallyFor1Ups++;
}

/* FloateyNumbersRoutine's timer-$2b reward half. */
void Score_ProcessFloateyReward(uint8_t control) {
    uint8_t code;
    uint8_t digit;

    if (control >= 0x0b) control = 0x0b;
    if (control == 0) return;

    if (control == 0x0b) {
        g_NumberofLives++;
        g_OnscreenPlayerInfo[0] = g_NumberofLives; /* NumberofLives=$075a */
        /* SoundEngine is excluded; preserve the original queue write. */
        g_Square2SoundQueue = Sfx_ExtraLife;
    }

    code = ScoreUpdateData[control];
    digit = (uint8_t)(code >> 4);
    Score_SetDigitModifier(digit, (uint8_t)(code & 0x0f));
    Score_AddToScore();
}

/* UpdateTopScore/TopScoreCheck.  The comparison starts at the least
 * significant digit and carries the 6502 borrow across all six digits. */
void Score_UpdateTopScore(void) {
    static const uint8_t player_offsets[2] = {6, 12};
    unsigned player;

    for (player = 0; player < 2; player++) {
        int borrow = 0;
        int i;
        uint8_t offset = player_offsets[player];

        for (i = 5; i >= 0; i--) {
            int diff = (int)g_DisplayDigits[offset + i] -
                       (int)g_DisplayDigits[i] - borrow;
            borrow = (diff < 0);
        }
        if (!borrow) {
            for (i = 0; i < 6; i++)
                g_DisplayDigits[i] = g_DisplayDigits[offset + i];
        }
    }
}
