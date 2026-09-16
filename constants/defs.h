/* defs.h - Game constants for SMB reimplementation */
/* Address-named constants document the original NES RAM layout; all supported
 * frontends use regular C variables and host-side memory abstractions. */

#ifndef SMB_DEFS_H
#define SMB_DEFS_H

#include "types.h"
#include "entity_constants.h"
#include "music_constants.h"
#include "sfx_constants.h"

/* ========================================================================
 * MEMORY MAP (NES RAM - mirrors original layout for TAS compatibility)
 * These are used as memory addresses on NES, ignored on SDL
 * ======================================================================== */

/* Variable address offsets in zero page */
#define VAR_ADDR(x) ((uint16_t)(x))

/* Game Variable Addresses - match the original 6502 addresses */
#define OPERMODE_ADDR               VAR_ADDR(0x00)
#define GAME_ENGINE_SUBROUTINE_ADDR VAR_ADDR(0x01)
#define OPERMODE_TASK_ADDR         VAR_ADDR(0x02)
#define SCREEN_ROUTINE_TASK_ADDR   VAR_ADDR(0x03)
#define VRAM_BUFFER_ADDRCTRL_ADDR  VAR_ADDR(0x06)
#define NMI_FRAME_COUNTER_ADDR     VAR_ADDR(0x07)
#define FRAME_COUNTER_ADDR         VAR_ADDR(0x08)
#define A_B_BUTTONS_ADDR            VAR_ADDR(0x0A)
#define UP_DOWN_BUTTONS_ADDR        VAR_ADDR(0x0B)
#define LEFT_RIGHT_BUTTONS_ADDR     VAR_ADDR(0x0C)
#define PLAYER_COLLISION_BITS_ADDR  VAR_ADDR(0x0490)

/* Player Variable Addresses */
#define PLAYER_X_POS_ADDR          VAR_ADDR(0x86)
#define PLAYER_PAGE_LOC_ADDR       VAR_ADDR(0x87)
#define PLAYER_Y_POS_ADDR         VAR_ADDR(0xCE)
#define PLAYER_Y_HIGH_POS_ADDR    VAR_ADDR(0xCF)
#define PLAYER_SPR_ATTRIB_ADDR    VAR_ADDR(0xD8)
#define PLAYER_SIZE_ADDR          VAR_ADDR(0xD9)
#define PLAYER_STATE_ADDR         VAR_ADDR(0xDC)
#define PLAYER_Y_SPEED_ADDR       VAR_ADDR(0xDD)
#define PLAYER_X_SPEED_ADDR       VAR_ADDR(0xDE)
#define PLAYER_MOVING_DIR_ADDR    VAR_ADDR(0xE3)
#define PLAYER_FACING_DIR_ADDR    VAR_ADDR(0xE6)
#define PLAYER_REL_X_POS_ADDR     VAR_ADDR(0xE7)
#define PLAYER_REL_Y_POS_ADDR     VAR_ADDR(0xEB)

/* ========================================================================
 * GAME CONSTANTS (values, not addresses)
 * ======================================================================== */

/* Operating Modes */
#define TITLE_SCREEN_MODE  0x00
#define GAME_MODE          0x01
#define VICTORY_MODE       0x02
#define GAME_OVER_MODE     0x03

/* Game States */
#define PLAYER_STATE_GROUND   0x00
#define PLAYER_STATE_JUMP     0x01
#define PLAYER_STATE_FALL     0x02
#define PLAYER_STATE_CLIMB    0x03

/* Player Status */
#define PLAYER_STATUS_SMALL  0x00
#define PLAYER_STATUS_BIG    0x01
#define PLAYER_STATUS_FIRE   0x02

/* Player Size (Big/Small) */
#define PLAYER_SIZE_SMALL    0x01  /* NES PlayerSize: 1 = small (main.asm:1483) */
#define PLAYER_SIZE_BIG      0x00

/* Area Types — values match NES (level-load.asm: GetAreaType).
 * AreaType is derived from the 2 MSBs of AreaPointer:
 *   %0xx00000 → %000000xx after rotation.
 * Confirmed by AreaDataHOffsets / AreaDataAddrLow in levels.asm:
 *   AreaType 0 = Water, 1 = Ground (overworld), 2 = Underground, 3 = Castle.
 */
#define AREA_TYPE_WATER        0x00
#define AREA_TYPE_GROUND       0x01
#define AREA_TYPE_OVERWORLD    AREA_TYPE_GROUND   /* ground == overworld */
#define AREA_TYPE_UNDERGROUND  0x02
#define AREA_TYPE_CASTLE       0x03

/* ========================================================================
 * SCREEN CONSTANTS
 * ======================================================================== */

#define SCREEN_WIDTH          256
#define SCREEN_HEIGHT         240
#define STATUS_BAR_H          32
#define TILE_SIZE             16

#define SPRITES_PER_SCANLINE  8
#define SPRITES_PER_FRAME     64

/* ========================================================================
 * TIMER ARRAY INDICES (matches NES Timers at $0780-$07A3)
 * Frame timers (idx 0-20) decrement every frame.
 * Interval timers (idx 21-35) decrement when IntervalTimerControl expires
 * (every 21 frames — see nmi.asm:74-91).
 * The Timers array is indexed by these symbols; see globals.h for the array.
 * ======================================================================== */

/* Frame timers ($0780-$0794) */
#define TIMER_SELECT               0   /* $0780 */
#define TIMER_PLAYER_ANIM          1   /* $0781 */
#define TIMER_JUMP_SWIM            2   /* $0782 */
#define TIMER_RUNNING              3   /* $0783 */
#define TIMER_BLOCK_BOUNCE         4   /* $0784 */
#define TIMER_SIDE_COLLISION       5   /* $0785 */
#define TIMER_JUMPSPRING           6   /* $0786 */
#define TIMER_GAME_TIMER_CTRL      7   /* $0787 */
/* $0788 reserved */
#define TIMER_CLIMB_SIDE           9   /* $0789 */
#define TIMER_ENEMY_FRAME_BASE    10   /* $078A (EnemyFrameTimer[6] -> 10-15) */
#define TIMER_FRENZY_ENEMY        15   /* $078F */
#define TIMER_BOWSER_FIRE_BREATH  16   /* $0790 */
#define TIMER_STOMP               17   /* $0791 */
#define TIMER_AIR_BUBBLE          18   /* $0792 */
/* $0793, $0794 reserved */

/* Interval timers ($0795-$07A3) */
#define TIMER_SCROLL_INTERVAL     21   /* $0795 */
#define TIMER_ENEMY_INTERVAL_BASE 22   /* $0796 (EnemyIntervalTimer[7] -> 22-28) */
/* $079D reserved */
#define TIMER_BRICK_COIN          29   /* $079D */
#define TIMER_INJURY              30   /* $079E */
#define TIMER_STAR_INVINCIBLE     31   /* $079F */
#define TIMER_SCREEN              32   /* $07A0 */
#define TIMER_WORLD_END           33   /* $07A1 */
#define TIMER_DEMO                34   /* $07A2 */
#define TIMER_RESERVED_35         35   /* $07A3: DecTimers still visits it */

#define TIMERS_ARRAY_SIZE        36    /* DecTimers indexes offsets $00..$23 */

/* DecTimers control values (nmi.asm:74-91) */
#define TIMER_CONTROL_GATE       0     /* if TimerControl==0, decrement frame timers */
#define INTERVAL_TIMER_RELOAD   20     /* $14 — IntervalTimerControl reload */

/* VRAM_AddrTable entries (smb1-disasm/system/nmi.asm:5-20).  Entries 0 and
 * 5 both read the primary buffer data in this SDL representation: the NES
 * entry 5 pointer starts at VRAM_Buffer1_Offset ($0300), whose following byte
 * is the first VRAM_Buffer1 data byte ($0301).  Entries 8-18 are the
 * palette/message streams owned by the SDL platform flush. */
#define VRAM_ADDR_BUFFER1        0
#define VRAM_ADDR_WATER_PALETTE  1
#define VRAM_ADDR_GROUND_PALETTE 2
#define VRAM_ADDR_UNDERGROUND_PALETTE 3
#define VRAM_ADDR_CASTLE_PALETTE 4
#define VRAM_ADDR_BUFFER1_OFFSET 5
#define VRAM_ADDR_BUFFER2        6
#define VRAM_ADDR_BUFFER2_ALT    7
#define VRAM_ADDR_CORE_COUNT     8
#define VRAM_ADDR_BOWSER_PALETTE 8
#define VRAM_ADDR_SNOW_DAY       9
#define VRAM_ADDR_SNOW_NIGHT     10
#define VRAM_ADDR_MUSHROOM       11
#define VRAM_ADDR_MARIO_THANKS   12
#define VRAM_ADDR_LUIGI_THANKS   13
#define VRAM_ADDR_RETAINER       14
#define VRAM_ADDR_PRINCESS_1     15
#define VRAM_ADDR_PRINCESS_2     16
#define VRAM_ADDR_WORLD_SELECT_1 17
#define VRAM_ADDR_WORLD_SELECT_2 18
#define VRAM_ADDR_TABLE_COUNT    19

/* ========================================================================
 * OAM (Sprite) DATA
 * ======================================================================== */

#define SPRITE_DATA        0x0300
#define SPRITE_Y_POS      0x0300
#define SPRITE_TILE       0x0301
#define SPRITE_ATTRIB     0x0302
#define SPRITE_X_POS      0x0303

/* ========================================================================
 * VRAM BUFFERS
 * ======================================================================== */

#define VRAM_BUFFER1      0x0300
#define VRAM_BUFFER1_OFF  0x06
#define VRAM_BUFFER2      0x0600
#define VRAM_BUFFER2_OFF  0x1A

/* ========================================================================
 * LEVEL CONSTANTS
 * ======================================================================== */

#define WORLD_1  0x00
#define WORLD_2  0x01
#define WORLD_3  0x02
#define WORLD_4  0x03
#define WORLD_5  0x04
#define WORLD_6  0x05
#define WORLD_7  0x06
#define WORLD_8  0x07

/* Backward compatibility */
#define World5 WORLD_5
#define World8 WORLD_8

/* ========================================================================
 * BACKWARD COMPATIBILITY ALIASES
 * ======================================================================== */

/* ========================================================================
 * CONTROLLER BUTTONS
 * ======================================================================== */

#define BTN_A      0x80
#define BTN_B      0x40
#define BTN_SELECT 0x20
#define BTN_START  0x10
#define BTN_UP     0x08
#define BTN_DOWN   0x04
#define BTN_LEFT   0x02
#define BTN_RIGHT  0x01

/* Controller buttons (original asm names) */
#define Up_Dir              BTN_UP

#endif /* SMB_DEFS_H */
