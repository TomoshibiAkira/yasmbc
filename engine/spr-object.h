/* Shared SprObject arithmetic (main.asm:4532-4778). */
#ifndef SMB_SPR_OBJECT_H
#define SMB_SPR_OBJECT_H

#include "constants/types.h"

/* Adapter over the owner’s parallel NES RAM fields; this is not a new store. */
typedef struct SprObjectView {
    uint8_t *page_loc;
    uint8_t *x_position;
    uint8_t *y_high_pos;
    uint8_t *y_position;
    uint8_t *x_speed;
    uint8_t *x_move_force;
    uint8_t *y_speed;
    uint8_t *y_move_force;
    uint8_t *y_mf_dummy;
    uint8_t *rel_x;
    uint8_t *rel_y;
    uint8_t *offscreen_bits;
} SprObjectView;

typedef struct SprScreenEdges {
    uint8_t left_page;
    uint8_t left_x;
    uint8_t right_page;
    uint8_t right_x;
} SprScreenEdges;

uint8_t SprObject_MoveHorizontally(SprObjectView *object);
/* Same as unsigned 6502 ADC of lo + delta into page:x (no signed high-byte). */
void pos_add_u8(uint8_t *page, uint8_t *x, uint8_t delta);
void pos_sub_u8(uint8_t *page, uint8_t *x, uint8_t delta);
/* The position/forced-carry half of ImposeGravity (main.asm:4706-4724).
 * It owns the shared YMF-dummy, Y position, and Y high-position updates. */
void SprObject_MoveVertically(SprObjectView *object);
void SprObject_ImposeGravity(SprObjectView *object,
                             uint8_t downward, uint8_t upward,
                             uint8_t max_speed, uint8_t correct_upward);
void SprObject_GetRelativePosition(SprObjectView *object,
                                   uint8_t screen_left_x);

/* GetScreenPosition/GetOffScreenBitsSet and the original ROM tables. */
void SprObject_SetScreenEdges(SprScreenEdges *edges,
                              uint8_t left_page, uint8_t left_x);
uint8_t SprObject_GetOffscreenBits(SprObjectView *object,
                                   const SprScreenEdges *edges);
uint8_t SprObject_GetXOffscreenBits(const SprObjectView *object,
                                    const SprScreenEdges *edges);
/* Direct GetXOffscreenBits consumers retain the ROM table byte, including
 * the $fe/$ff completely-offscreen values used by platform bounding boxes. */
uint8_t SprObject_GetRawXOffscreenBits(const SprObjectView *object,
                                       const SprScreenEdges *edges);
int SprObject_LoadRom(void);

/* SprObjectOffscrChk and its player/block column/row helpers.  Bases are
 * sprite indices within the 256-byte OAM image, not world coordinates. */
void SprObject_MaskEnemyOAM(uint8_t *sprite_data, uint16_t sprite_base,
                            uint8_t offscreen_bits);
void SprObject_MaskPlayerOAM(uint8_t *sprite_data, uint16_t sprite_base,
                             uint8_t offscreen_bits);
void SprObject_MaskBlockOAM(uint8_t *sprite_data, uint16_t sprite_base,
                            uint8_t offscreen_bits);

#endif
