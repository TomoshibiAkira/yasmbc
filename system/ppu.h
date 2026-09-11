/* ppu.h - PPU (Picture Processing Unit) hardware abstraction */
/* NES PPU registers - referenced from constants/hardware.h */

#ifndef PPU_H
#define PPU_H

#include "../constants/hardware.h"

/* PPU Registers - use hardware.h definitions for NES */
/* These are used directly on NES, ignored on SDL */

#ifdef NES

/* Set PPU address (16-bit address in two writes) */
static inline void PPU_SetAddr(uint16_t addr) {
    PPU_ADDRESS = (addr >> 8) & 0x3F;
    PPU_ADDRESS = addr & 0xFF;
}

/* Write to PPU data */
static inline void PPU_Write(uint8_t data) {
    PPU_DATA = data;
}

/* Read from PPU data */
static inline uint8_t PPU_Read(void) {
    return PPU_DATA;
}

/* Wait for VBlank */
static inline void PPU_WaitVBlank(void) {
    while (!(PPU_STATUS & 0x80)) {
        /* Wait for vblank flag */
    }
}

/* Reset PPU address latch */
static inline void PPU_ResetAddr(void) {
    volatile uint8_t dummy = PPU_STATUS;  /* Reading resets vblank and address latch */
    (void)dummy;
}

#endif /* NES */

#endif /* PPU_H */
