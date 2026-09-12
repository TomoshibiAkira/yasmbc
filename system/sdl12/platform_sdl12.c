/* platform_sdl12.c - SDL 1.2 software frontend for Win9x-class hosts.
 *
 * This backend deliberately avoids SDL 2's window/renderer/texture API.  It
 * presents the shared 256x240 palette-index framebuffer through an
 * SDL_Surface, which keeps the binary usable with the DirectX backend shipped
 * by SDL 1.2 on Windows 98.  Audio is supplied by the companion SDL1.2
 * Nes_Snd_Emu adapter, leaving the video/input surface path in C.
 */

#include <SDL/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../platform.h"
#include "../common/nes_rgb.h"
#include "../common/ppu_memory.h"
#include "../sdl/video_soft.h"
#include "../../engine/assets.h"

static SDL_Surface *screen;
static InputState current_input;
static uint8_t quit_requested;
static int configured_headless;
static int sdl_initialized;
static int scale_factor = 2;
static uint32_t color_lut[64];
static uint32_t next_frame_deadline;
static unsigned frame_fraction;
static int pacing_started;

#ifndef NDEBUG
static int frame_count;

static int save_frame_as_ppm(const char *filename)
{
    FILE *file;
    const uint8_t *indices;
    int x, y;

    file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return -1;
    }
    fprintf(file, "P6\n%d %d\n255\n", VIDEO_WIDTH, VIDEO_HEIGHT);
    indices = video_indices();
    for (y = 0; y < VIDEO_HEIGHT; ++y) {
        for (x = 0; x < VIDEO_WIDTH; ++x) {
            const uint8_t *rgb = nes_rgb[indices[y * VIDEO_WIDTH + x] & 0x3F];
            fwrite(rgb, 1, 3, file);
        }
    }
    fclose(file);
    return 0;
}

void platform_save_frame(void)
{
    char filename[256];

    snprintf(filename, sizeof(filename), "test_output/frame_%04d.ppm",
             frame_count);
    if (save_frame_as_ppm(filename) == 0) {
        printf("Saved frame %d to %s\n", frame_count, filename);
        ++frame_count;
    }
}
#else
void platform_save_frame(void)
{
}
#endif

static int read_scale(void)
{
    const char *value = getenv("SMB_SDL12_SCALE");
    long parsed;
    char *end;

    if (!value || !*value)
        return 2;
    parsed = strtol(value, &end, 10);
    if (*end != '\0' || parsed < 1 || parsed > 4)
        return 2;
    return (int)parsed;
}

static int key_is_down(const Uint8 *keys, int key_count, SDLKey key)
{
    int index = (int)key;
    return index >= 0 && index < key_count && keys[index] != 0;
}

static void set_display_palette(void)
{
    SDL_Color colors[64];
    int i;

    for (i = 0; i < 64; ++i) {
        colors[i].r = nes_rgb[i][0];
        colors[i].g = nes_rgb[i][1];
        colors[i].b = nes_rgb[i][2];
        colors[i].unused = 0;
        /* The source framebuffer already contains palette indices.  Do not
         * ask SDL to quantize those indices before installing the palette;
         * SDL_MapRGB may otherwise return an unrelated default entry. */
        color_lut[i] = screen->format->BytesPerPixel == 1
                           ? (uint32_t)i
                           : SDL_MapRGB(screen->format, colors[i].r,
                                        colors[i].g, colors[i].b);
    }
    if (screen->format->BytesPerPixel == 1)
        SDL_SetPalette(screen, SDL_LOGPAL | SDL_PHYSPAL, colors, 0, 64);
}

static int open_display(void)
{
    Uint32 flags = SDL_SWSURFACE;
    const char *fullscreen = getenv("SMB_SDL12_FULLSCREEN");
    int width = VIDEO_WIDTH * scale_factor;
    int height = VIDEO_HEIGHT * scale_factor;

    if (fullscreen && *fullscreen && strcmp(fullscreen, "0") != 0)
        flags |= SDL_FULLSCREEN;

    /* Win98/VGA machines normally expose an 8-bit surface.  Requesting
     * zero-bpp as a fallback also lets the same binary run on modern hosts
     * whose window manager does not provide an 8-bit visual. */
    screen = SDL_SetVideoMode(width, height, 8, flags);
    if (!screen)
        screen = SDL_SetVideoMode(width, height, 0, flags);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_WM_SetCaption("Super Mario Bros. (SDL 1.2)", NULL);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_EnableKeyRepeat(0, 0);
    set_display_palette();
    return 0;
}

void platform_set_headless(uint8_t enabled)
{
    configured_headless = enabled ? 1 : 0;
}

void platform_init(void)
{
    quit_requested = 0;
    scale_factor = read_scale();
    next_frame_deadline = 0;
    frame_fraction = 0;
    pacing_started = 0;

    if (!configured_headless) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            quit_requested = 1;
            return;
        }
        sdl_initialized = 1;
        if (open_display() < 0) {
            quit_requested = 1;
            return;
        }
    }

    PPU_Init();
    if (Assets_Init() != 0 || video_init() != 0) {
        quit_requested = 1;
        return;
    }
    printf("SDL 1.2 platform initialized (%dx%d, %dbpp)\n",
           VIDEO_WIDTH * scale_factor, VIDEO_HEIGHT * scale_factor,
           screen ? screen->format->BitsPerPixel : 0);
}

void platform_shutdown(void)
{
    platform_audio_shutdown();
    video_shutdown();
    if (screen) {
        SDL_ShowCursor(SDL_ENABLE);
        /* The display surface is owned by SDL_SetVideoMode and is released
         * by SDL_Quit; freeing it here can corrupt SDL 1.2's video state. */
        screen = NULL;
    }
    if (sdl_initialized) {
        SDL_Quit();
        sdl_initialized = 0;
    }
}

void platform_read_input(InputState *state)
{
    SDL_Event event;
    Uint8 *keys;
    int key_count = 0;

    memset(&current_input, 0, sizeof(current_input));
    if (!sdl_initialized) {
        *state = current_input;
        return;
    }
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            quit_requested = 1;
        else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE)
            quit_requested = 1;
    }
    keys = SDL_GetKeyState(&key_count);
    current_input.a = key_is_down(keys, key_count, SDLK_x) ||
                      key_is_down(keys, key_count, SDLK_KP0);
    current_input.b = key_is_down(keys, key_count, SDLK_z) ||
                      key_is_down(keys, key_count, SDLK_KP_PERIOD);
    current_input.select = key_is_down(keys, key_count, SDLK_BACKSPACE);
    current_input.start = key_is_down(keys, key_count, SDLK_RETURN) ||
                          key_is_down(keys, key_count, SDLK_KP_ENTER);
    current_input.up = key_is_down(keys, key_count, SDLK_UP);
    current_input.down = key_is_down(keys, key_count, SDLK_DOWN);
    current_input.left = key_is_down(keys, key_count, SDLK_LEFT);
    current_input.right = key_is_down(keys, key_count, SDLK_RIGHT);
    *state = current_input;
}

uint8_t platform_should_quit(void)
{
    return quit_requested;
}

static void advance_frame_deadline(void)
{
    /* 1000/60 = 16 + 2/3 ms.  Keeping the remainder avoids a cumulative
     * drift while staying within SDL 1.2's millisecond clock API. */
    next_frame_deadline += 16;
    frame_fraction += 2;
    if (frame_fraction >= 3) {
        ++next_frame_deadline;
        frame_fraction -= 3;
    }
}

void platform_wait_frame(void)
{
    Uint32 now;
    Sint32 remaining;

    if (configured_headless || !sdl_initialized)
        return;
    now = SDL_GetTicks();
    if (!pacing_started) {
        next_frame_deadline = now;
        frame_fraction = 0;
        pacing_started = 1;
    }
    advance_frame_deadline();
    for (;;) {
        now = SDL_GetTicks();
        remaining = (Sint32)(next_frame_deadline - now);
        if (remaining <= 0)
            break;
        if (remaining > 2)
            SDL_Delay((Uint32)(remaining - 1));
        else
            SDL_Delay(0);
    }
    if ((Sint32)(now - next_frame_deadline) > 4 * 17) {
        next_frame_deadline = now;
        frame_fraction = 0;
    }
}

void platform_render_begin(void)
{
    video_render_begin();
}

static void write_pixel(uint8_t *destination, uint32_t color)
{
    switch (screen->format->BytesPerPixel) {
    case 1:
        *destination = (uint8_t)color;
        break;
    case 2:
        *(uint16_t *)destination = (uint16_t)color;
        break;
    case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        destination[0] = (uint8_t)(color >> 16);
        destination[1] = (uint8_t)(color >> 8);
        destination[2] = (uint8_t)color;
#else
        destination[0] = (uint8_t)color;
        destination[1] = (uint8_t)(color >> 8);
        destination[2] = (uint8_t)(color >> 16);
#endif
        break;
    default:
        *(uint32_t *)destination = color;
        break;
    }
}

static void present_frame(void)
{
    const uint8_t *indices = video_indices();
    int bytes_per_pixel;
    int x, y, sx, sy;

    if (!screen)
        return;
    bytes_per_pixel = screen->format->BytesPerPixel;
    if (SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) < 0)
        return;

    if (bytes_per_pixel == 1) {
        /* This is the normal Win98/VGA path.  Keep the hot loop in palette
         * indices and avoid a function call and RGB lookup per scaled pixel. */
        for (y = 0; y < VIDEO_HEIGHT; ++y) {
            const uint8_t *source = indices + y * VIDEO_WIDTH;
            uint8_t *row = (uint8_t *)screen->pixels +
                           y * scale_factor * screen->pitch;
            int scaled_width = VIDEO_WIDTH * scale_factor;

            if (scale_factor == 1) {
                memcpy(row, source, VIDEO_WIDTH);
                continue;
            }

            /* Expand one scanline, then duplicate it for the remaining
             * vertical rows.  The previous x*sx loop repeated the address
             * arithmetic for every scaled pixel; on Win9x-class CPUs that
             * made the presentation copy a measurable part of the frame. */
            if (scale_factor == 2 &&
                (((uintptr_t)row & (sizeof(uint32_t) - 1)) == 0)) {
                /* Two source pixels become one naturally aligned 32-bit
                 * store: [a a b b].  This cuts the inner-loop store count
                 * in half on the PII without changing the indexed surface. */
                uint32_t *out = (uint32_t *)row;
                for (x = 0; x < VIDEO_WIDTH; x += 2) {
                    uint32_t a = (uint32_t)(source[x] & 0x3F);
                    uint32_t b = (uint32_t)(source[x + 1] & 0x3F);
                    *out++ = a | (a << 8) | (b << 16) | (b << 24);
                }
            } else if (scale_factor == 4 &&
                       (((uintptr_t)row & (sizeof(uint32_t) - 1)) == 0)) {
                /* The 4x case has the same property for one source pixel. */
                uint32_t *out = (uint32_t *)row;
                for (x = 0; x < VIDEO_WIDTH; ++x) {
                    uint32_t color = (uint32_t)(source[x] & 0x3F);
                    *out++ = color * 0x01010101u;
                }
            } else {
                for (x = 0; x < VIDEO_WIDTH; ++x) {
                    uint8_t color = (uint8_t)(source[x] & 0x3F);
                    for (sx = 0; sx < scale_factor; ++sx)
                        row[x * scale_factor + sx] = color;
                }
            }
            for (sy = 1; sy < scale_factor; ++sy) {
                memcpy(row + sy * screen->pitch, row, (size_t)scaled_width);
            }
        }
    } else if (bytes_per_pixel == 2 &&
               (((uintptr_t)screen->pixels & (sizeof(uint16_t) - 1)) == 0)) {
        /* Some Win98 desktops refuse an 8-bit window and return RGB565.
         * Keep that fallback cheap too: resolve each palette index once and
         * write typed pixels, instead of dispatching write_pixel for every
         * scaled destination pixel. */
        for (y = 0; y < VIDEO_HEIGHT; ++y) {
            const uint8_t *source = indices + y * VIDEO_WIDTH;
            for (sy = 0; sy < scale_factor; ++sy) {
                uint16_t *row = (uint16_t *)((uint8_t *)screen->pixels +
                    (y * scale_factor + sy) * screen->pitch);
                if (scale_factor == 1) {
                    for (x = 0; x < VIDEO_WIDTH; ++x)
                        row[x] = (uint16_t)color_lut[source[x] & 0x3F];
                } else if (scale_factor == 2) {
                    for (x = 0; x < VIDEO_WIDTH; ++x) {
                        uint16_t color =
                            (uint16_t)color_lut[source[x] & 0x3F];
                        row[x * 2] = color;
                        row[x * 2 + 1] = color;
                    }
                } else if (scale_factor == 4) {
                    for (x = 0; x < VIDEO_WIDTH; ++x) {
                        uint16_t color =
                            (uint16_t)color_lut[source[x] & 0x3F];
                        row[x * 4] = color;
                        row[x * 4 + 1] = color;
                        row[x * 4 + 2] = color;
                        row[x * 4 + 3] = color;
                    }
                } else {
                    for (x = 0; x < VIDEO_WIDTH; ++x) {
                        uint16_t color =
                            (uint16_t)color_lut[source[x] & 0x3F];
                        row[x * 3] = color;
                        row[x * 3 + 1] = color;
                        row[x * 3 + 2] = color;
                    }
                }
            }
        }
    } else if (bytes_per_pixel == 4 &&
               (((uintptr_t)screen->pixels & (sizeof(uint32_t) - 1)) == 0)) {
        /* Same fast path for RGB888/ARGB8888 desktop surfaces. */
        for (y = 0; y < VIDEO_HEIGHT; ++y) {
            const uint8_t *source = indices + y * VIDEO_WIDTH;
            for (sy = 0; sy < scale_factor; ++sy) {
                uint32_t *row = (uint32_t *)((uint8_t *)screen->pixels +
                    (y * scale_factor + sy) * screen->pitch);
                if (scale_factor == 1) {
                    for (x = 0; x < VIDEO_WIDTH; ++x)
                        row[x] = color_lut[source[x] & 0x3F];
                } else if (scale_factor == 2) {
                    for (x = 0; x < VIDEO_WIDTH; ++x) {
                        uint32_t color = color_lut[source[x] & 0x3F];
                        row[x * 2] = color;
                        row[x * 2 + 1] = color;
                    }
                } else if (scale_factor == 4) {
                    for (x = 0; x < VIDEO_WIDTH; ++x) {
                        uint32_t color = color_lut[source[x] & 0x3F];
                        row[x * 4] = color;
                        row[x * 4 + 1] = color;
                        row[x * 4 + 2] = color;
                        row[x * 4 + 3] = color;
                    }
                } else {
                    for (x = 0; x < VIDEO_WIDTH; ++x) {
                        uint32_t color = color_lut[source[x] & 0x3F];
                        row[x * 3] = color;
                        row[x * 3 + 1] = color;
                        row[x * 3 + 2] = color;
                    }
                }
            }
        }
    } else {
        for (y = 0; y < VIDEO_HEIGHT; ++y) {
            const uint8_t *source = indices + y * VIDEO_WIDTH;
            for (sy = 0; sy < scale_factor; ++sy) {
                uint8_t *row = (uint8_t *)screen->pixels +
                               (y * scale_factor + sy) * screen->pitch;
                for (x = 0; x < VIDEO_WIDTH; ++x) {
                    uint32_t color = color_lut[source[x] & 0x3F];
                    for (sx = 0; sx < scale_factor; ++sx)
                        write_pixel(row +
                                        (x * scale_factor + sx) * bytes_per_pixel,
                                    color);
                }
            }
        }
    }

    if (SDL_MUSTLOCK(screen))
        SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}

void platform_render_end(void)
{
    video_render_end();
    if (!configured_headless)
        present_frame();
}

const uint8_t *platform_get_frame_indices(void)
{
    return video_indices();
}
