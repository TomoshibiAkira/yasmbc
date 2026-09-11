/* platform_nes.c - NES/CC65 implementation of platform layer */
/* This file is compiled only for the NES build */

#ifdef NES

#include <cc65.h>
#include <6502.h>
#include "../../constants/defs.h"
#include "../platform.h"

static uint8_t joypad_buffer = 0;
static unsigned char index;

void platform_init(void) {
    SEI();

    PPU_CTRL_REG1 = 0x00;
    PPU_CTRL_REG2 = 0x00;

    PPU_SPR_ADDR = 0x00;
    for (index = 0; index < 256; ++index) {
        PPU_SPR_DATA = 0xFF;
    }

    SEI();
}

void platform_shutdown(void) {
    PPU_CTRL_REG1 = 0x00;
    PPU_CTRL_REG2 = 0x00;
}

void platform_set_headless(uint8_t enabled) {
    (void)enabled;
}

static void read_joypad(void) {
    JOYPAD_PORT = 0x01;
    JOYPAD_PORT = 0x00;
    joypad_buffer = 0;
}

void platform_read_input(InputState* state) {
    read_joypad();

    state->a = (joypad_buffer & 0x80) ? 1 : 0;
    state->b = (joypad_buffer & 0x40) ? 1 : 0;
    state->select = (joypad_buffer & 0x20) ? 1 : 0;
    state->start = (joypad_buffer & 0x10) ? 1 : 0;
    state->up = (joypad_buffer & 0x08) ? 1 : 0;
    state->down = (joypad_buffer & 0x04) ? 1 : 0;
    state->left = (joypad_buffer & 0x02) ? 1 : 0;
    state->right = (joypad_buffer & 0x01) ? 1 : 0;
}

uint8_t platform_should_quit(void) {
    return 0;
}

void platform_wait_frame(void) {
    while (!(PPU_STATUS & 0x80)) {
    }
}

void platform_render_begin(void) {
}

void platform_render_end(void) {
    PPU_SPR_ADDR = 0x00;
    OAM_DMA = 0x02;
}

const uint8_t* platform_get_frame_indices(void) {
    return 0;
}

void platform_save_frame(void) {
}

void platform_draw_tile_at(uint8_t tile, uint8_t palette, int screen_x, int screen_y) {
    (void)palette;
    (void)screen_x;
    (void)screen_y;
    (void)tile;
}

void platform_audio_init(void) {
    APU_STATUS = 0x0F;
}

void platform_audio_shutdown(void) {
    APU_STATUS = 0;
}

void platform_apu_write(uint16_t address, uint8_t value) {
    *(volatile uint8_t*)address = value;
}

void platform_audio_trace_begin(uint32_t nmi_ordinal, const uint8_t *state,
                                uint16_t state_size) {
    (void)nmi_ordinal; (void)state; (void)state_size;
}

void platform_audio_trace_end(const uint8_t *state, uint16_t state_size) {
    (void)state; (void)state_size;
}

void platform_audio_frame(void) {
}

void platform_vram_flush(void) {
    uint8_t* buffer = (uint8_t*)VRAM_Buffer1;
    uint16_t offset = 0;

    while (buffer[offset] != 0x00 && offset < 0x0300) {
        PPU_ADDRESS = buffer[offset++];
        PPU_ADDRESS = buffer[offset++];
        PPU_DATA = buffer[offset++];
    }

    VRAM_Buffer1_Offset = 0;
}

#endif /* NES */
