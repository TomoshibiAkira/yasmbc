/* platform.h - Platform abstraction layer - shared API */

#ifndef SMB_PLATFORM_H
#define SMB_PLATFORM_H

#include "../constants/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t a, b, select, start;
    uint8_t up, down, left, right;
} ControllerInput;

typedef struct {
    ControllerInput controllers[2];
} InputState;

/* Common command-line configuration shared by the PC frontends.  Zero means
 * use the backend default; audio_hifi is a request and may be ignored by a
 * backend whose mixer is always high quality (the SDL backend, for example). */
typedef struct {
    int scale;
    int audio_rate;
    int audio_hifi;
} PlatformOptions;

void platform_init(void);
void platform_shutdown(void);
void platform_set_headless(uint8_t enabled);
void platform_set_options(const PlatformOptions *options);
int platform_audio_rate(void);
int platform_audio_hifi(void);

void platform_read_input(InputState* state);
uint8_t platform_should_quit(void);

void platform_wait_frame(void);

void platform_render_begin(void);
void platform_render_end(void);

/* Canonical 256x240 NES palette-index framebuffer (one byte per pixel). */
const uint8_t* platform_get_frame_indices(void);

/* Headless frame output (debug builds only) */
void platform_save_frame(void);

/* Background tile blit used by the software nametable renderer. */
void platform_draw_tile_at(uint8_t tile, uint8_t palette, int screen_x, int screen_y);

void platform_audio_init(void);
void platform_audio_shutdown(void);
void platform_apu_write(uint16_t address, uint8_t value);
void platform_audio_frame(void);
void platform_audio_trace_begin(uint32_t nmi_ordinal, const uint8_t *state,
                                uint16_t state_size);
void platform_audio_trace_end(const uint8_t *state, uint16_t state_size);

void platform_vram_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* SMB_PLATFORM_H */
