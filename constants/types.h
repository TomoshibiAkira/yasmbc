/* types.h - Basic types for SMB reimplementation */

#ifndef SMB_TYPES_H
#define SMB_TYPES_H

#include <stdint.h>

/* Boolean types */
typedef uint8_t bool;
#define true  1
#define false 0

/* Pointer types */
typedef uint8_t* puint8;
typedef uint16_t* puint16;

/* 6502 addressing mode flags */
#define ZP      0x0100  /* Zero page addressing */
#define STACK   0x0100  /* Stack addressing */
#define ABS     0x0000  /* Absolute addressing */
#define REL     0x0000  /* Relative addressing (for branches) */

#endif /* SMB_TYPES_H */
