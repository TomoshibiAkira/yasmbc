/* platform_sdl.c - SDL2 window, input, and frame pacing */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform.h"
#include "../common/ppu_memory.h"
#include "video_soft.h"
#include "../common/nes_rgb.h"
#include "../../engine/assets.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint32_t *pixel_buffer = NULL;
static InputState current_input;
#ifndef NDEBUG
static int frame_count = 0;
#endif
static int configured_headless = 0;
static int sdl_initialized = 0;
static uint8_t quit_requested = 0;
static int configured_scale = 0;
static int configured_audio_rate = 0;
static int configured_audio_hifi = 0;
static int scale_factor = 3;

#ifndef NDEBUG
static int save_frame_as_ppm(const char *filename)
{
    FILE *f;
    int y, x;
    const uint8_t *idx = video_indices();

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", VIDEO_WIDTH, VIDEO_HEIGHT);
    for (y = 0; y < VIDEO_HEIGHT; y++) {
        for (x = 0; x < VIDEO_WIDTH; x++) {
            const uint8_t *c = nes_rgb[idx[y * VIDEO_WIDTH + x] & 0x3F];
            fwrite(c, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}

void platform_save_frame(void)
{
    char filename[256];
    snprintf(filename, sizeof(filename), "test_output/frame_%04d.ppm", frame_count);
    if (save_frame_as_ppm(filename) == 0) {
        printf("Saved frame %d to %s\n", frame_count, filename);
        frame_count++;
    }
}
#endif

#ifdef NDEBUG
void platform_save_frame(void)
{
}
#endif

void platform_set_headless(uint8_t enabled)
{
    configured_headless = enabled ? 1 : 0;
}

void platform_set_options(const PlatformOptions *options)
{
    configured_scale = 0;
    configured_audio_rate = 0;
    configured_audio_hifi = 0;
    if (!options)
        return;
    if (options->scale >= 1 && options->scale <= 4)
        configured_scale = options->scale;
    if (options->audio_rate == 22050 || options->audio_rate == 44100 ||
        options->audio_rate == 48000)
        configured_audio_rate = options->audio_rate;
    configured_audio_hifi = options->audio_hifi ? 1 : 0;
}

int platform_audio_rate(void)
{
    return configured_audio_rate;
}

int platform_audio_hifi(void)
{
    return configured_audio_hifi;
}

void platform_init(void)
{
    printf("platform_init: Starting...\n");
    quit_requested = 0;
    scale_factor = configured_scale ? configured_scale : 3;

    if (!configured_headless) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return;
        }
        sdl_initialized = 1;
        window = SDL_CreateWindow(
            "Super Mario Bros.",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            VIDEO_WIDTH * scale_factor, VIDEO_HEIGHT * scale_factor,
            SDL_WINDOW_SHOWN);
        if (!window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return;
        }
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return;
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    VIDEO_WIDTH, VIDEO_HEIGHT);
        if (!texture) {
            fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return;
        }
    }

    pixel_buffer = (uint32_t *)malloc(VIDEO_WIDTH * VIDEO_HEIGHT * sizeof(uint32_t));
    if (!pixel_buffer) {
        fprintf(stderr, "Failed to allocate pixel buffer\n");
        return;
    }
    memset(pixel_buffer, 0, VIDEO_WIDTH * VIDEO_HEIGHT * sizeof(uint32_t));

    PPU_Init();
    if (Assets_Init() != 0)
        return;
    if (video_init() != 0)
        return;
    printf("SDL platform initialized\n");
}

void platform_shutdown(void)
{
    platform_audio_shutdown();
    video_shutdown();
    if (pixel_buffer)
        free(pixel_buffer);
    if (texture)
        SDL_DestroyTexture(texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    if (sdl_initialized)
        SDL_Quit();
}

void platform_read_input(InputState *state)
{
    SDL_Event e;
    const uint8_t *keys;
    ControllerInput *player1;
    ControllerInput *player2;

    memset(&current_input, 0, sizeof(current_input));
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            quit_requested = 1;
        else if (e.type == SDL_KEYDOWN &&
                 e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            quit_requested = 1;
    }
    keys = SDL_GetKeyboardState(NULL);
    player1 = &current_input.controllers[0];
    player2 = &current_input.controllers[1];
    player1->a = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_KP_0];
    player1->b = keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_KP_PERIOD];
    player1->select = keys[SDL_SCANCODE_BACKSPACE];
    player1->start = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
    player1->up = keys[SDL_SCANCODE_UP];
    player1->down = keys[SDL_SCANCODE_DOWN];
    player1->left = keys[SDL_SCANCODE_LEFT];
    player1->right = keys[SDL_SCANCODE_RIGHT];
    player2->a = keys[SDL_SCANCODE_N];
    player2->b = keys[SDL_SCANCODE_M];
    player2->select = keys[SDL_SCANCODE_J];
    player2->start = keys[SDL_SCANCODE_K];
    player2->up = keys[SDL_SCANCODE_W];
    player2->down = keys[SDL_SCANCODE_S];
    player2->left = keys[SDL_SCANCODE_A];
    player2->right = keys[SDL_SCANCODE_D];
    *state = current_input;
}

uint8_t platform_should_quit(void)
{
    return quit_requested;
}

#define NES_FRAME_SECONDS (29780.5 / 1789773.0)

static double next_frame_deadline = 0.0;

void platform_wait_frame(void)
{
    const double frequency = (double)SDL_GetPerformanceFrequency();
    double now = (double)SDL_GetPerformanceCounter() / frequency;

    if (next_frame_deadline == 0.0)
        next_frame_deadline = now;
    next_frame_deadline += NES_FRAME_SECONDS;

    for (;;) {
        double remaining;
        now = (double)SDL_GetPerformanceCounter() / frequency;
        remaining = next_frame_deadline - now;
        if (remaining <= 0.0)
            break;
        if (remaining > 0.002)
            SDL_Delay((uint32_t)(remaining * 1000.0) - 1);
        else
            SDL_Delay(0);
    }
    if (now - next_frame_deadline > NES_FRAME_SECONDS * 4.0)
        next_frame_deadline = now;
}

void platform_render_begin(void)
{
    video_render_begin();
}

void platform_render_end(void)
{
    const uint8_t *idx;
    int i;

    video_render_end();
    if (configured_headless || !texture || !renderer || !pixel_buffer)
        return;
    idx = video_indices();
    for (i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++)
        pixel_buffer[i] = nes_argb32(idx[i]);
    SDL_UpdateTexture(texture, NULL, pixel_buffer, VIDEO_WIDTH * (int)sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

const uint8_t *platform_get_frame_indices(void)
{
    return video_indices();
}
