/* nmi.h - NES NMI-equivalent per-frame tick (from nmi.asm)
 *
 * On NES, ALL game logic (VRAM flush, timers, OperMode tree) runs inside
 * the NMI handler. Long tasks keep NMIs disabled across video frames,
 * which skips flush/timer/task work for those frames. This module models
 * that scheduling so frame timing matches FCEUX cycle-for-cycle output.
 */

#ifndef ENGINE_NMI_H
#define ENGINE_NMI_H

#include <stdint.h>

/* Latch sampled at NMI start from DisableScreenFlag; controls whether the
 * CURRENT output frame shows nametable/sprites (1-frame sampling delay). */
extern uint8_t g_RenderEnabledLatch;

/* Long-task NMI blackout counter: while nonzero, whole NMI ticks are
 * skipped (no flush, no timers, no task, no FrameCounter++). */
extern uint8_t g_NMIBusy;

/* OAM snapshot used for rendering (post-NMI SPR DMA state). */
extern uint8_t g_RenderOAM[256];

/* PPU scroll latch sampled at NMI start (used for this frame's picture). */
extern uint8_t g_RenderScrollX;
extern uint8_t g_RenderNT;
/* Whether the NMI's Sprite0Hit path owns the HUD scroll split for the
 * current output field (nmi.asm:111-130). */
extern uint8_t g_RenderSprite0Split;

void NMI_Tick(uint8_t raw_input0, uint8_t raw_input1);

#endif
