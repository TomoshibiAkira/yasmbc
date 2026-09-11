/* hud.c - Heads-up display routines (from status-lines.asm) */

#include "screen/screen.h"
#include "screen/routine/intermediate.h"
#include "assets.h"
#include "constants/globals.h"

static uint8_t StatusBarData[12];
static uint8_t StatusBarOffset[6];
static uint8_t StatusBarNybbles[2];
static uint8_t TopStatusBarLine[39];

int Hud_LoadRom(void) {
    if (Assets_Copy("tables/status_bar_data.bin", StatusBarData,
                    sizeof(StatusBarData)) ||
        Assets_Copy("tables/status_bar_offset.bin", StatusBarOffset,
                    sizeof(StatusBarOffset)) ||
        Assets_Copy("tables/status_bar_nybbles.bin", StatusBarNybbles,
                    sizeof(StatusBarNybbles)) ||
        Assets_Copy("tables/top_status_bar.bin", TopStatusBarLine,
                    sizeof(TopStatusBarLine)))
        return -1;
    return 0;
}

/* ========================================================================
 * OUTPUT NUMBERS (from main.asm OutputNumbers / PrintStatusBarNumbers)
 * ======================================================================== */

/* OutputNumbers: write digit tiles to VRAM buffer
 * nybble: index into StatusBarOffset/StatusBarData (after +1 and &0x0F)
 */
static void OutputNumbers(uint8_t nybble) {
    nybble = (nybble + 1) & 0x0F;
    if (nybble >= 6) return;

    uint8_t y = nybble * 2;  /* index into StatusBarData */
    uint8_t x = g_VRAM_Buffer1_Offset;

    /* Address high byte: $22 for top score on title screen, $20 otherwise */
    g_VRAM_Buffer1[x] = (y == 0) ? 0x22 : 0x20;
    /* Address low byte and length from StatusBarData */
    g_VRAM_Buffer1[x + 1] = StatusBarData[y];
    g_VRAM_Buffer1[x + 2] = StatusBarData[y + 1];

    uint8_t length = StatusBarData[y + 1];

    /* Calculate starting index into DisplayDigits.
     * ASM does: lda StatusBarOffset,x; sec; sbc StatusBarData+1,y; tay
     * Unconditional subtract with unsigned wraparound.
     */
    uint8_t digit_offset = StatusBarOffset[nybble] - length;

    /* Copy digit tiles to buffer */
    for (uint8_t i = 0; i < length; i++) {
        g_VRAM_Buffer1[x + 3 + i] = g_DisplayDigits[digit_offset + i];
    }

    /* Null terminator */
    g_VRAM_Buffer1[x + 3 + length] = 0x00;

    /* NES: null goes at buffer[x+3+length]; the saved offset afterwards is
     * x_start + length + 3 (main.asm:1313-1325). */
    g_VRAM_Buffer1_Offset = x + length + 3;
}

/* PrintStatusBarNumbers: print coins (low nybble) then score (high nybble) */
static void PrintStatusBarNumbers(uint8_t nybbles) {
    /* First: low nybble = coin display */
    OutputNumbers(nybbles & 0x0F);
    /* Second: high nybble = score display */
    OutputNumbers(nybbles >> 4);
}

/* UpdateNumber: print status bar numbers and zero-suppress the leading
 * score digit (main.asm:4077-4086 UpdateNumber; observed FCEUX behavior
 * suppresses the highest digit: title top score col 16 and player score
 * col 2 both render blank when zero). The leading digit sits 9 bytes
 * before the updated buffer offset on every call path. */
static void UpdateNumber(uint8_t value) {
    PrintStatusBarNumbers(value);

    /* Zero-suppress: NES reads VRAM_Buffer1-6,y with the final offset,
     * which lands on the score entry's first digit (main.asm:4079-4083). */
    uint8_t offset = g_VRAM_Buffer1_Offset;
    if (offset >= 6 && g_VRAM_Buffer1[offset - 6] == 0) {
        g_VRAM_Buffer1[offset - 6] = 0x24;  /* replace with space tile */
    }
}

/* GetSBNybbles: get status bar nybbles for current player, print with
 * zero suppression (NES falls through into UpdateNumber, main.asm:4070) */
static void GetSBNybbles(void) {
    uint8_t nybbles = StatusBarNybbles[g_CurrentPlayer & 1];
    UpdateNumber(nybbles);
}


/* Per-frame HUD refresh used by the title menu and gameplay cores.
 * NES GameCoreRoutine calls UpdateNumber with the player's nybbles. */
void UpdateHUD(void) {
    g_VRAM_Buffer1_Offset = 0;
    UpdateNumber(StatusBarNybbles[g_CurrentPlayer & 1]);
}

/* Public AddToScore/GetSBNybbles boundary.  Score routines use this exact
 * UpdateNumber path so zero suppression and VRAM-buffer ownership remain in
 * the HUD producer rather than being duplicated by gameplay callers. */
void HUD_UpdateNumber(uint8_t nybbles) {
    UpdateNumber(nybbles);
}

/* ========================================================================
 * SCREEN ROUTINE IMPLEMENTATIONS
 * ======================================================================== */

/* Write top status line (MARIO, WORLD, TIME labels) */
void WriteTopStatusLine(void) {
    /* WriteTopStatusLine (status-lines.asm) is a direct WriteGameText(0)
     * call.  Keep the shared CheckPlayerName fixup: unlike a raw asset copy,
     * it changes MARIO to LUIGI when CurrentPlayer is the second player. */
    Screen_WriteGameText(0);
    Screen_IncTask();
}

/* Write bottom status line (score digits, coins, world-level number) */
void WriteBottomStatusLine(void) {
    /* First: write score and coin digits to buffer */
    GetSBNybbles();

    /* Then: write world-level number at $2073 */
    uint8_t offset = g_VRAM_Buffer1_Offset;
    g_VRAM_Buffer1[offset++] = 0x20;  /* addr high */
    g_VRAM_Buffer1[offset++] = 0x73;  /* addr low */
    g_VRAM_Buffer1[offset++] = 0x03;  /* length */
    g_VRAM_Buffer1[offset++] = g_WorldNumber + 1;
    g_VRAM_Buffer1[offset++] = 0x28;  /* dash tile */
    g_VRAM_Buffer1[offset++] = g_LevelNumber + 1;
    g_VRAM_Buffer1[offset++] = 0x00;  /* null terminator */
    g_VRAM_Buffer1_Offset = offset;

    Screen_IncTask();
}

/* Write top score display on title screen */
void WriteTopScore(void) {
    /* On NES: lda #$FA; jsr UpdateNumber; then IncModeTask_B */
    UpdateNumber(0xFA);
    g_OperMode_Task++;  /* IncModeTask_B: exit screen routines */
}

/* Public wrapper: RunGameTimer updates the TIME digits via
 * PrintStatusBarNumbers($a4) (main.asm:3386-3387). */
void HUD_PrintStatusBarNumbers(uint8_t nybbles) {
    PrintStatusBarNumbers(nybbles);
}

/* Game timer init from Entrance_GameTimerSetup (game-timer-setup.asm:56-62):
 * DisplayDigits[33..35] = $07F8-$07FA = GameTimerDisplay.
 * Digits: hundreds from header, tens 0, ones 1 (NES quirk: shows N01). */
void HUD_SetGameTimer(uint8_t hundreds) {
    g_DisplayDigits[33] = hundreds;
    g_DisplayDigits[34] = 0;
    g_DisplayDigits[35] = 1;
}
