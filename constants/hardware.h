/* hardware.h - NES hardware register definitions */

#ifndef SMB_HARDWARE_H
#define SMB_HARDWARE_H

#include "types.h"

/* These are for the NES build - ignored on SDL */
#ifdef NES

/* PPU Registers ($2000-$2007) */
#define PPU_CTRL_REG1_ADDR         0x2000
#define PPU_CTRL_REG2_ADDR         0x2001
#define PPU_STATUS_ADDR             0x2002
#define PPU_SPR_ADDR_ADDR          0x2003
#define PPU_SPR_DATA_ADDR          0x2004
#define PPU_SCROLL_REG_ADDR        0x2005
#define PPU_ADDRESS_ADDR           0x2006
#define PPU_DATA_ADDR              0x2007

/* APU/I/O Registers */
#define SQUARE1_VOL_ADDR           0x4000
#define SQUARE1_SWEEP_ADDR        0x4001
#define SQUARE1_LO_ADDR           0x4002
#define SQUARE1_HI_ADDR           0x4003

#define SQUARE2_VOL_ADDR           0x4004
#define SQUARE2_SWEEP_ADDR        0x4005
#define SQUARE2_LO_ADDR           0x4006
#define SQUARE2_HI_ADDR           0x4007

#define TRIANGLE_LINEAR_ADDR       0x4008
#define TRIANGLE_LO_ADDR           0x400A
#define TRIANGLE_HI_ADDR          0x400B

#define NOISE_VOL_ADDR             0x400C
#define NOISE_LO_ADDR              0x400E
#define NOISE_HI_ADDR              0x400F

#define DMC_FREQ_ADDR              0x4010
#define DMC_RAW_ADDR               0x4011
#define DMC_START_ADDR             0x4012
#define DMC_LEN_ADDR               0x4013

#define OAM_DMA_ADDR               0x4014
#define JOYPAD_PORT_ADDR           0x4016
#define JOYPAD_PORT1_ADDR          0x4016
#define JOYPAD_PORT2_ADDR          0x4017

#define APU_STATUS_ADDR            0x4015
#define APU_FRAME_COUNTER_ADDR     0x4017

/* PPU Registers ($2000-$2007) - volatile for direct hardware access */
#define PPU_CTRL_REG1     (*(volatile uint8_t*)PPU_CTRL_REG1_ADDR)
#define PPU_CTRL_REG2     (*(volatile uint8_t*)PPU_CTRL_REG2_ADDR)
#define PPU_STATUS        (*(volatile uint8_t*)PPU_STATUS_ADDR)
#define PPU_SPR_ADDR      (*(volatile uint8_t*)PPU_SPR_ADDR_ADDR)
#define PPU_SPR_DATA      (*(volatile uint8_t*)PPU_SPR_DATA_ADDR)
#define PPU_SCROLL_REG    (*(volatile uint8_t*)PPU_SCROLL_REG_ADDR)
#define PPU_ADDRESS       (*(volatile uint8_t*)PPU_ADDRESS_ADDR)
#define PPU_DATA          (*(volatile uint8_t*)PPU_DATA_ADDR)

/* APU/I/O Registers */
#define SPRITE_DMA        (*(volatile uint8_t*)0x4014)
#define JOYPAD_PORT       (*(volatile uint8_t*)0x4016)

#define SQUARE1_VOL       (*(volatile uint8_t*)SQUARE1_VOL_ADDR)
#define SQUARE1_SWEEP     (*(volatile uint8_t*)SQUARE1_SWEEP_ADDR)
#define SQUARE1_LO        (*(volatile uint8_t*)SQUARE1_LO_ADDR)
#define SQUARE1_HI        (*(volatile uint8_t*)SQUARE1_HI_ADDR)

#define SQUARE2_VOL       (*(volatile uint8_t*)SQUARE2_VOL_ADDR)
#define SQUARE2_SWEEP     (*(volatile uint8_t*)SQUARE2_SWEEP_ADDR)
#define SQUARE2_LO        (*(volatile uint8_t*)SQUARE2_LO_ADDR)
#define SQUARE2_HI        (*(volatile uint8_t*)SQUARE2_HI_ADDR)

#define TRIANGLE_LINEAR   (*(volatile uint8_t*)TRIANGLE_LINEAR_ADDR)
#define TRIANGLE_LO       (*(volatile uint8_t*)TRIANGLE_LO_ADDR)
#define TRIANGLE_HI       (*(volatile uint8_t*)TRIANGLE_HI_ADDR)

#define NOISE_VOL         (*(volatile uint8_t*)NOISE_VOL_ADDR)
#define NOISE_LO          (*(volatile uint8_t*)NOISE_LO_ADDR)
#define NOISE_HI          (*(volatile uint8_t*)NOISE_HI_ADDR)

#define DMC_FREQ          (*(volatile uint8_t*)DMC_FREQ_ADDR)
#define DMC_RAW           (*(volatile uint8_t*)DMC_RAW_ADDR)
#define DMC_START         (*(volatile uint8_t*)DMC_START_ADDR)
#define DMC_LEN           (*(volatile uint8_t*)DMC_LEN_ADDR)

#define OAM_DMA           (*(volatile uint8_t*)OAM_DMA_ADDR)

#define APU_STATUS        (*(volatile uint8_t*)APU_STATUS_ADDR)
#define APU_FRAME_COUNTER (*(volatile uint8_t*)APU_FRAME_COUNTER_ADDR)

/* Mirror modes */
#define MIRROR_HORIZ      0
#define MIRROR_VERT       1
#define MIRROR_SINGLE0    2
#define MIRROR_SINGLE1    3

#endif /* NES */

#endif /* SMB_HARDWARE_H */
