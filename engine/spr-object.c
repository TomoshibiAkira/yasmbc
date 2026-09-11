/* Shared generic SprObject routines translated from main.asm. */
#include "spr-object.h"
#include "assets.h"

void pos_add_u8(uint8_t *page, uint8_t *x, uint8_t delta) {
    uint16_t sum = (uint16_t)*x + delta;
    *x = (uint8_t)sum;
    *page = (uint8_t)(*page + (uint8_t)(sum >> 8));
}

void pos_sub_u8(uint8_t *page, uint8_t *x, uint8_t delta) {
    uint8_t borrow = (*x < delta) ? 1 : 0;
    *x = (uint8_t)(*x - delta);
    *page = (uint8_t)(*page - borrow);
}

uint8_t SprObject_MoveHorizontally(SprObjectView *object) {
    uint8_t speed = *object->x_speed;
    uint8_t fractional = (uint8_t)(speed << 4);
    uint8_t integer = (uint8_t)(speed >> 4);
    uint8_t force_carry;
    uint8_t position_carry;
    uint16_t sum;

    if (integer >= 0x08) integer |= 0xf0;
    sum = (uint16_t)*object->x_move_force + fractional;
    *object->x_move_force = (uint8_t)sum;
    force_carry = (uint8_t)(sum >> 8);
    sum = (uint16_t)*object->x_position + integer + force_carry;
    *object->x_position = (uint8_t)sum;
    position_carry = (uint8_t)(sum >> 8);
    *object->page_loc = (uint8_t)(*object->page_loc +
                                  ((integer & 0x80) ? 0xff : 0x00) +
                                  position_carry);
    return (uint8_t)(integer + force_carry);
}

void SprObject_MoveVertically(SprObjectView *object) {
    uint8_t speed = *object->y_speed;
    uint8_t carry;
    uint16_t sum;

    /* ImposeGravity enters with the carry from
     * SprObject_YMF_Dummy + SprObject_Y_MoveForce still live.  The 6502
     * preserves that carry through the sign adder and consumes the next
     * carry when it updates Y_HighPos. */
    sum = (uint16_t)*object->y_mf_dummy + *object->y_move_force;
    *object->y_mf_dummy = (uint8_t)sum;
    carry = (uint8_t)(sum >> 8);

    sum = (uint16_t)*object->y_position + speed + carry;
    *object->y_position = (uint8_t)sum;
    carry = (uint8_t)(sum >> 8);

    sum = (uint16_t)*object->y_high_pos +
          ((speed & 0x80) ? 0xff : 0x00) + carry;
    *object->y_high_pos = (uint8_t)sum;
}

void SprObject_ImposeGravity(SprObjectView *object,
                             uint8_t downward, uint8_t upward,
                             uint8_t max_speed, uint8_t correct_upward) {
    uint8_t speed = *object->y_speed;
    uint8_t carry;
    uint16_t sum;

    SprObject_MoveVertically(object);

    sum = (uint16_t)*object->y_move_force + downward;
    *object->y_move_force = (uint8_t)sum;
    carry = (uint8_t)(sum >> 8);
    *object->y_speed = (uint8_t)(speed + carry);
    /* `bmi ChkUpM` tests N on the 8-bit CMP result, not a C signed
     * comparison of the operands.  Preserve that flag behavior for the
     * full $00-$ff speed state, including wrapped negative values. */
    if (!((uint8_t)(*object->y_speed - max_speed) & 0x80) &&
        *object->y_move_force >= 0x80) {
        *object->y_speed = max_speed;
        *object->y_move_force = 0;
    }

    if (correct_upward) {
        uint8_t old_force = *object->y_move_force;
        uint8_t borrow = old_force < upward;
        uint8_t negative_limit = (uint8_t)(0 - max_speed);
        *object->y_move_force = (uint8_t)(old_force - upward);
        *object->y_speed = (uint8_t)(*object->y_speed - borrow);
        /* `bpl ExVMove` likewise branches on the N bit of
         * speed-minus-limit, followed by unsigned BCS on the force. */
        if (((uint8_t)(*object->y_speed - negative_limit) & 0x80) &&
            *object->y_move_force < 0x80) {
            *object->y_speed = negative_limit;
            *object->y_move_force = 0xff;
        }
    }
}

void SprObject_GetRelativePosition(SprObjectView *object,
                                   uint8_t screen_left_x) {
    /* GetObjRelativePosition (main.asm:12189-12196) stores both relative
     * coordinates unconditionally; a zero prior relative value is not a
     * skip flag. */
    *object->rel_y = *object->y_position;
    *object->rel_x = (uint8_t)(*object->x_position - screen_left_x);
}

static uint8_t XOffscreenBitsData[16];
static uint8_t YOffscreenBitsData[9];
static uint8_t DefaultXOnscreenOfs[3];
static uint8_t DefaultYOnscreenOfs[3];
static uint8_t HighPosUnitData[2];

int SprObject_LoadRom(void) {
    if (Assets_Copy("tables/x_offscreen_bits.bin", XOffscreenBitsData,
                    sizeof(XOffscreenBitsData)) ||
        Assets_Copy("tables/y_offscreen_bits.bin", YOffscreenBitsData,
                    sizeof(YOffscreenBitsData)) ||
        Assets_Copy("tables/default_x_onscreen_ofs.bin", DefaultXOnscreenOfs,
                    sizeof(DefaultXOnscreenOfs)) ||
        Assets_Copy("tables/default_y_onscreen_ofs.bin", DefaultYOnscreenOfs,
                    sizeof(DefaultYOnscreenOfs)) ||
        Assets_Copy("tables/high_pos_unit.bin", HighPosUnitData,
                    sizeof(HighPosUnitData)))
        return -1;
    return 0;
}

void SprObject_SetScreenEdges(SprScreenEdges *edges,
                              uint8_t left_page, uint8_t left_x) {
    uint16_t right = (uint16_t)left_x + 0xff;
    edges->left_page = left_page;
    edges->left_x = left_x;
    edges->right_x = (uint8_t)right;
    edges->right_page = (uint8_t)(left_page + (right >> 8));
}

static uint8_t divide_pixel_difference(uint8_t difference, uint8_t preset) {
    uint8_t offset;
    if (difference >= preset) return 0xff;
    offset = (uint8_t)((difference >> 3) & 0x07);
    /* DividePDiff returns only the three-bit quotient.  Its CPY #$01
     * precedes ADC $05, so the left/bottom edge adders are applied by the
     * caller when it selects the second half of the offscreen table; they
     * are not part of this quotient. */
    return offset;
}

static uint8_t get_x_offscreen_bits_raw(const SprObjectView *object,
                                        const SprScreenEdges *edges) {
    uint8_t edge_x, edge_page, difference, index, offset;
    int8_t page_delta;
    int edge;

    /* GetXOffscreenBits checks right edge (index 1), then left (index 0). */
    for (edge = 1; edge >= 0; edge--) {
        edge_x = edge ? edges->right_x : edges->left_x;
        edge_page = edge ? edges->right_page : edges->left_page;
        difference = (uint8_t)(edge_x - *object->x_position);
        page_delta = (int8_t)(uint8_t)(edge_page - *object->page_loc -
                                       (edge_x < *object->x_position));
        index = DefaultXOnscreenOfs[edge];
        if (page_delta >= 0) {
            index = DefaultXOnscreenOfs[edge + 1];
            if (page_delta == 0 && difference < 0x38) {
                offset = divide_pixel_difference(difference, 0x38);
                if (offset != 0xff) index = (uint8_t)(offset +
                    (edge ? 0 : 8));
            }
        }
        if (XOffscreenBitsData[index] != 0)
            return XOffscreenBitsData[index];
    }
    return 0;
}

static uint8_t get_x_offscreen_bits(const SprObjectView *object,
                                    const SprScreenEdges *edges) {
    /* GetOffScreenBits stores the X result in the low nibble.  The direct
     * 6502 GetXOffscreenBits callers (platform bounding boxes and the OAM
     * stacker) consume the unshifted table byte instead. */
    return (uint8_t)(get_x_offscreen_bits_raw(object, edges) >> 4);
}

static uint8_t get_y_offscreen_bits(const SprObjectView *object) {
    uint8_t edge_y, difference, index, offset;
    uint8_t edge_high = 1;
    int8_t high_delta;
    int edge;

    /* GetYOffscreenBits checks top (index 1), then bottom (index 0). */
    for (edge = 1; edge >= 0; edge--) {
        edge_y = HighPosUnitData[edge];
        difference = (uint8_t)(edge_y - *object->y_position);
        high_delta = (int8_t)(uint8_t)(edge_high - *object->y_high_pos -
                                       (edge_y < *object->y_position));
        index = DefaultYOnscreenOfs[edge];
        if (high_delta >= 0) {
            index = DefaultYOnscreenOfs[edge + 1];
            if (high_delta == 0 && difference < 0x20) {
                offset = divide_pixel_difference(difference, 0x20);
                if (offset != 0xff) index = (uint8_t)(offset +
                    (edge ? 0 : 4));
            }
        }
        if (YOffscreenBitsData[index] != 0) return YOffscreenBitsData[index];
    }
    return 0;
}

uint8_t SprObject_GetOffscreenBits(SprObjectView *object,
                                   const SprScreenEdges *edges) {
    uint8_t bits = (uint8_t)((get_y_offscreen_bits(object) << 4) |
                             get_x_offscreen_bits(object, edges));
    if (object->offscreen_bits != 0) *object->offscreen_bits = bits;
    return bits;
}

uint8_t SprObject_GetXOffscreenBits(const SprObjectView *object,
                                    const SprScreenEdges *edges) {
    return get_x_offscreen_bits(object, edges);
}

uint8_t SprObject_GetRawXOffscreenBits(const SprObjectView *object,
                                       const SprScreenEdges *edges) {
    return get_x_offscreen_bits_raw(object, edges);
}

static void mask_y(uint8_t *sprite_data, uint16_t sprite_index) {
    sprite_data[sprite_index * 4] = 0xf8;
}

void SprObject_MaskEnemyOAM(uint8_t *sprite_data, uint16_t base,
                            uint8_t bits) {
    uint8_t row, col;
    if (bits & 0x04) for (row = 0; row < 3; row++) mask_y(sprite_data,
        base + row * 2 + 1);
    if (bits & 0x08) for (row = 0; row < 3; row++) mask_y(sprite_data,
        base + row * 2);
    if (bits & 0x20) for (col = 0; col < 2; col++) mask_y(sprite_data,
        base + 4 + col);
    /* SprObjectOffscrChk (main.asm:11360-11403) sends each vertical-row bit
     * through MoveESprRowOffscreen at offsets $10/$08/$00; MoveESprRowOffscreen
     * (main.asm:11417-11422) always jumps to DumpTwoSpr (main.asm:10629-10647),
     * so d5/d6/d7 mask exactly the third/second/first two-sprite row. */
    if (bits & 0x40) for (col = 0; col < 2; col++) mask_y(sprite_data,
        base + 2 + col);
    if (bits & 0x80) for (col = 0; col < 2; col++) mask_y(sprite_data,
        base + col);
}

void SprObject_MaskPlayerOAM(uint8_t *sprite_data, uint16_t base,
                             uint8_t bits) {
    uint8_t row;
    for (row = 0; row < 4; row++) {
        if (bits & (uint8_t)(0x80 >> row)) {
            mask_y(sprite_data, base + row * 2);
            mask_y(sprite_data, base + row * 2 + 1);
        }
    }
}

void SprObject_MaskBlockOAM(uint8_t *sprite_data, uint16_t base,
                            uint8_t bits) {
    if (bits & 0x04) {
        mask_y(sprite_data, base + 1);
        mask_y(sprite_data, base + 3);
    }
    if (bits & 0x08) {
        mask_y(sprite_data, base);
        mask_y(sprite_data, base + 2);
    }
}
