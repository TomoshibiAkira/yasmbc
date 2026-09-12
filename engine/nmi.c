/* nmi.c - NES NMI-equivalent per-frame tick (from system/nmi.asm)
 *
 * NMI_Tick order (live; nmi.asm NonMaskableInterrupt):
 *   joypad latch → OAM/scroll/screen latch → platform_vram_flush →
 *   Level_FlushDeferred → SoundEngine → pause → (if unpaused) DecTimers /
 *   FrameCounter / PRNG / sprite shuffle → OperMode_Tasks
 *
 * Long tasks (InitializeGame, InitializeArea) run past the NMI disable at
 * handler start, which blacks out subsequent NMIs for their duration;
 * g_NMIBusy reproduces those skipped ticks.
 */
#include <string.h>

#include "constants/defs.h"
#include "system/platform.h"
#include "opermode.h"
#include "nmi.h"
#include "sprite-offsets.h"
#include "timers.h"
#include "score.h"
#include "audio.h"
#include "level/level.h"
#include "constants/globals.h"

uint8_t g_RenderEnabledLatch = 0;
uint8_t g_NMIBusy = 0;

/* PauseRoutine (main.asm:30-64). The queue is consumed by Audio_SoundEngine
 * at the original NMI boundary immediately before this routine. */
static void NMI_PauseRoutine(void) {
    int eligible = (g_OperMode == VICTORY_MODE) ||
                   (g_OperMode == GAME_MODE && g_OperMode_Task == 3);
    if (!eligible) return;

    if (g_GamePauseTimer != 0) {
        g_GamePauseTimer--;
        return;
    }

    if (g_SavedJoypadBits & BTN_START) {
        if (g_GamePauseStatus & 0x80) return;
        g_GamePauseTimer = 0x2B; /* main.asm:50 */
        g_PauseSoundQueue = (uint8_t)(g_GamePauseStatus + 1);
        g_GamePauseStatus = (uint8_t)((g_GamePauseStatus ^ 0x01) | 0x80);
    } else {
        g_GamePauseStatus &= 0x7F;
    }
}

/* The seven-byte rotate chain in NonMaskableInterrupt (nmi.asm:95-110).
 * The first carry is XOR(bit 1 of bytes 0 and 1); each ROR then passes its
 * outgoing bit as carry to the next byte. */
static void NMI_AdvancePseudoRandom(void) {
    uint8_t carry = (uint8_t)(((g_PseudoRandomBitReg[0] ^
                                g_PseudoRandomBitReg[1]) & 0x02) != 0);
    unsigned i;
    for (i = 0; i < 7; i++) {
        uint8_t next_carry = g_PseudoRandomBitReg[i] & 0x01;
        g_PseudoRandomBitReg[i] =
            (uint8_t)((g_PseudoRandomBitReg[i] >> 1) | (carry << 7));
        carry = next_carry;
    }
}

/* ReadJoypads (main.asm:1156-1189) keeps Select/Start edge-triggered in
 * JoypadBitMask[$074A/$074B].  The controller latch itself is updated only
 * from an NMI; skipped long-task NMIs therefore do not consume a movie
 * record. */
static void NMI_ReadJoypads(uint8_t raw_input0, uint8_t raw_input1) {
    uint8_t raw[2] = {raw_input0, raw_input1};
    uint8_t *saved[2] = {&g_SavedJoypadBits, &g_SavedJoypad2Bits};
    unsigned i;

    for (i = 0; i < 2; i++) {
        *saved[i] = raw[i];
        if ((uint8_t)(raw[i] & (BTN_SELECT | BTN_START) & g_JoypadBitMask[i]))
            *saved[i] &= (uint8_t)~(BTN_SELECT | BTN_START);
        else
            g_JoypadBitMask[i] = raw[i];
    }
}

/* OAM snapshot used for rendering (PPU OAM after NMI's SPR DMA). The game
 * writes g_SpriteData (the $0200 mirror); the NMI prologue DMAs it to the
 * PPU, then MoveSpritesOffscreen clears mirror slots 1+ so the game core
 * rewrites them (nmi.asm:45-47, 117-121). */
uint8_t g_RenderOAM[256];
uint8_t g_RenderScrollX = 0;
uint8_t g_RenderNT = 0;
uint8_t g_RenderSprite0Split = 0;

void NMI_Tick(uint8_t raw_input0, uint8_t raw_input1) {
    static uint32_t audio_nmi_ordinal;
    AudioVerifierState audio_state;
    if (g_NMIBusy) {
        /* A long operation has disabled the next NMI, but the PPU field is
         * still rendered.  Carry the Sprite0 branch's RAM-owned state into
         * that field; this is the non-CPU side of the nmi.asm:111-130
         * SkipSprite0 path, not a movie/frame special case. */
        g_RenderSprite0Split = g_Sprite0HitDetectFlag ? 1 : 0;
        g_NMIBusy--;
        return;
    }

    /* ReadJoypads is inside NonMaskableInterrupt (nmi.asm:67-68), so the
     * latches change only when this NMI body actually runs.  A skipped NMI
     * must retain the previous SavedJoypadBits through the long-task
     * interval. */
    g_PrevJoypadBits = g_SavedJoypadBits;
    NMI_ReadJoypads(raw_input0, raw_input1);

    /* NMI prologue: SPR DMA uploads the mirror, then MoveSpritesOffscreen
     * clears slots 1+ (sprite-0 hit detection active). The scroll/NT
     * registers written at the END of the PREVIOUS NMI govern this
     * frame's picture; latch current values before the core runs. */
    memcpy(g_RenderOAM, g_SpriteData, 256);
    g_RenderScrollX = g_HorizontalScroll;
    /* The NES renders from Mirror_PPU_CTRL_REG1's nametable bit, not from
     * the camera page directly.  InitializeNameTables, SecondaryGameSetup,
     * and ScrollScreen are the routines that own writes to that mirror;
     * NMI only latches it for the current picture. */
    g_RenderNT = g_MirrorPPUCtrl1 & 1;
    g_RenderSprite0Split = g_Sprite0HitDetectFlag ? 1 : 0;
    /* Sample screen-disable flag for this frame's render */
    g_RenderEnabledLatch = (g_DisableScreenFlag == 0) ? 1 : 0;

    /* UpdateScreen (nmi.asm:48-64): DMA has already latched OAM above;
     * consume the selected VRAM stream and clear its owning header before
     * any game logic runs.  Level's direct-write queue is a temporary bridge
     * for the P2 parser translation and is flushed after the canonical queue. */
    platform_vram_flush();
    Level_FlushDeferred();

    /* SoundEngine follows UpdateScreen at the original NMI boundary. */
    Audio_GetVerifierState(&audio_state);
    platform_audio_trace_begin(audio_nmi_ordinal++, (const uint8_t *)&audio_state,
                               (uint16_t)sizeof(audio_state));
    Audio_SoundEngine();
    Audio_GetVerifierState(&audio_state);
    platform_audio_trace_end((const uint8_t *)&audio_state,
                             (uint16_t)sizeof(audio_state));
    NMI_PauseRoutine();
    Score_UpdateTopScore();

    /* DecTimers + FrameCounter are skipped while GamePauseStatus bit 0 is
     * set (nmi.asm:71-94). */
    if ((g_GamePauseStatus & 0x01) == 0) {
        Timers_DecTimers();
        g_FrameCounter++;
    }

    NMI_AdvancePseudoRandom();

    /* Sprite0 path owns MoveSpritesOffscreen/SpriteShuffler.  In the ASM
     * these are not unconditional: pause skips them, and a zero
     * Sprite0HitDetectFlag skips the whole branch. */
    if (g_Sprite0HitDetectFlag && (g_GamePauseStatus & 0x01) == 0) {
        int i;
        for (i = 4; i < 256; i += 4) g_SpriteData[i] = 0xF8;
        SpriteOffsets_Shuffle();
    }

    /* OperModeExecutionTree: one task call, skipped while paused. */
    if ((g_GamePauseStatus & 0x01) == 0) OperMode_Tasks();

}
