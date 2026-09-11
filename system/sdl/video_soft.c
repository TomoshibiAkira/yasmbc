/* Index-only NES compositor: CHR decode, nametable blit, sprites. */

#include "video_soft.h"
#include "../platform.h"
#include "../common/ppu_memory.h"
#include "../../engine/assets.h"
#ifdef DOS
#include "../dos/dos_vga.h"
#endif

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define TILE_SIZE 16
#define NUM_TILES 512
#define TILES_PER_BANK 256
#define BG_MASK_SIZE (VIDEO_WIDTH * VIDEO_HEIGHT)

extern uint8_t g_RenderEnabledLatch;
extern uint8_t g_RenderScrollX;
extern uint8_t g_RenderNT;
extern uint8_t g_RenderOAM[256];
extern uint8_t g_RenderSprite0Split;

static uint8_t frame_index_buffer[BG_MASK_SIZE];
static uint8_t bg_opaque_mask[BG_MASK_SIZE];
static uint8_t sprite_claimed[BG_MASK_SIZE];
static uint8_t sprite_claim_gen;
static uint8_t sprite_row_mask[64];
static int drawing_sprite_index = -1;
static uint8_t chr_tiles[NUM_TILES * TILE_SIZE];
static uint8_t chr_texels[NUM_TILES * TILE_WIDTH * TILE_HEIGHT];
static uint8_t chr_opaque[NUM_TILES * TILE_WIDTH * TILE_HEIGHT];
#ifndef DOS
static uint8_t rendered_tiles[8][NUM_TILES * TILE_WIDTH * TILE_HEIGHT];
#endif
static uint8_t cached_expanded_palette[8][4];
#ifndef DOS
static uint8_t palette_cache_valid[8];
#endif
static uint8_t bg_pattern_table = 1;
static uint8_t video_ready;
static uint8_t *color_dest;
static uint8_t *opaque_dest;
static uint32_t dirty_mask[VIDEO_HEIGHT];
static uint32_t bg_serial = 1;
static uint16_t world_cam;
#ifdef DOS
static uint8_t persistent_opaque[BG_MASK_SIZE];
static uint8_t persistent_scroll;
static uint8_t persistent_nt;
static uint8_t persistent_enabled;
static uint8_t persistent_valid;
#endif

static uint8_t tile_pattern_color(const uint8_t *chr_data, int row, int col)
{
    uint8_t plane0 = chr_data[row];
    uint8_t plane1 = chr_data[row + 8];
    uint8_t bit = (uint8_t)(7 - col);
    return (uint8_t)((((plane1 >> bit) & 1) << 1) | ((plane0 >> bit) & 1));
}

static uint8_t palette_byte(uint8_t palette_idx, uint8_t color_idx)
{
    uint8_t palette_entry;
    if (color_idx == 0) {
        if (palette_idx >= 4)
            return 0;
        palette_entry = PPU_ReadPalette(0);
    } else {
        palette_entry = PPU_ReadPalette((uint8_t)(palette_idx * 4 + color_idx));
    }
    if (palette_entry >= 64)
        palette_entry = 0x30;
    return (uint8_t)(palette_entry & 0x3F);
}

#ifndef DOS
static void rebuild_rendered_palette(int palette_idx)
{
    int tile, pixel;
    for (pixel = 0; pixel < 4; ++pixel)
        cached_expanded_palette[palette_idx][pixel] =
            palette_byte((uint8_t)palette_idx, (uint8_t)pixel);
    for (tile = 0; tile < NUM_TILES; ++tile) {
        const uint8_t *src = chr_texels + tile * 64;
        uint8_t *dst = rendered_tiles[palette_idx] + tile * 64;
        for (pixel = 0; pixel < 64; ++pixel)
            dst[pixel] = cached_expanded_palette[palette_idx][src[pixel]];
    }
    palette_cache_valid[palette_idx] = 1;
}

static void refresh_palette_cache(void)
{
    int palette_idx, color;
    for (palette_idx = 0; palette_idx < 8; ++palette_idx) {
        if (!palette_cache_valid[palette_idx]) {
            rebuild_rendered_palette(palette_idx);
            continue;
        }
        for (color = 0; color < 4; ++color) {
            if (cached_expanded_palette[palette_idx][color] !=
                palette_byte((uint8_t)palette_idx, (uint8_t)color)) {
                rebuild_rendered_palette(palette_idx);
                break;
            }
        }
    }
}
#else
static void refresh_palette_luts(void)
{
    int palette_idx, color;
    for (palette_idx = 0; palette_idx < 8; ++palette_idx)
        for (color = 0; color < 4; ++color)
            cached_expanded_palette[palette_idx][color] =
                palette_byte((uint8_t)palette_idx, (uint8_t)color);
}
#endif

#ifdef DOS
static void set_draw_target(uint8_t *color, uint8_t *opaque)
{
    color_dest = color;
    opaque_dest = opaque;
}
#endif

static void dirty_reset(void)
{
    memset(dirty_mask, 0, sizeof(dirty_mask));
}

#ifndef DOS
static void mark_dirty_all(void)
{
    int y;
    for (y = 0; y < VIDEO_HEIGHT; y++)
        dirty_mask[y] = 0xFFFFFFFFu;
}
#endif

int video_init(void)
{
    int tile, row, col;
    if (Assets_Copy("tiles.chr", chr_tiles, sizeof(chr_tiles)) != 0) {
        fprintf(stderr, "Failed to load assets/tiles.chr\n");
        return -1;
    }
    memset(frame_index_buffer, 0, sizeof(frame_index_buffer));
    memset(bg_opaque_mask, 0, sizeof(bg_opaque_mask));
#ifndef DOS
    memset(palette_cache_valid, 0, sizeof(palette_cache_valid));
#endif
    for (tile = 0; tile < NUM_TILES; ++tile) {
        const uint8_t *chr = chr_tiles + tile * TILE_SIZE;
        uint8_t *texels = chr_texels + tile * 64;
        for (row = 0; row < TILE_HEIGHT; ++row)
            for (col = 0; col < TILE_WIDTH; ++col)
                texels[row * 8 + col] = tile_pattern_color(chr, row, col);
        for (row = 0; row < 64; ++row)
            chr_opaque[tile * 64 + row] = texels[row] != 0;
    }
#ifdef DOS
    set_draw_target(NULL, persistent_opaque);
    memset(persistent_opaque, 0, sizeof(persistent_opaque));
#endif
    dirty_reset();
    video_ready = 1;
    printf("Loaded %u bytes (%d tiles)\n",
           (unsigned)sizeof(chr_tiles), (int)(sizeof(chr_tiles) / TILE_SIZE));
    return 0;
}

void video_shutdown(void)
{
    video_ready = 0;
}

const uint8_t *video_indices(void)
{
    return frame_index_buffer;
}

const uint32_t *video_dirty_mask(void)
{
    return dirty_mask;
}

uint32_t video_bg_serial(void)
{
    return bg_serial;
}

uint16_t video_world_cam(void)
{
    return world_cam;
}

void platform_draw_tile_at(uint8_t tile, uint8_t palette, int screen_x, int screen_y)
{
    uint16_t chr_index;
    int row, col;
    const uint8_t *texels;
    const uint8_t *opaque;
#ifndef DOS
    const uint8_t *pixels;
    uint8_t *color = color_dest ? color_dest : frame_index_buffer;
#endif
    uint8_t *mask = opaque_dest ? opaque_dest : bg_opaque_mask;
#ifdef DOS
    const uint8_t *lut;
#else
    int write_color = 1;
#endif

    if (!video_ready)
        return;
    chr_index = (uint16_t)tile + (bg_pattern_table ? TILES_PER_BANK : 0);
    if (chr_index >= NUM_TILES)
        return;
    texels = &chr_texels[chr_index * 64];
    opaque = &chr_opaque[chr_index * 64];
#ifndef DOS
    pixels = &rendered_tiles[palette & 7][chr_index * 64];
#endif

#ifdef DOS
    lut = cached_expanded_palette[palette & 7];
    /* VGA page residency says nothing about this frame's screen-space
     * opacity mask. Refresh it even when the hidden page needs no writes. */
    dos_vga_blit_tile(screen_x, screen_y, tile, palette, texels, lut);
#endif

    for (row = 0; row < TILE_HEIGHT; row++) {
        int dest_y = screen_y + row;
        if (dest_y < 0 || dest_y >= VIDEO_HEIGHT)
            break;
        if (screen_x >= 0 && screen_x + TILE_WIDTH <= VIDEO_WIDTH) {
            int pix = dest_y * VIDEO_WIDTH + screen_x;
#ifndef DOS
            if (write_color)
                memcpy(color + pix, pixels + row * 8, 8);
#endif
            memcpy(mask + pix, opaque + row * 8, 8);
            continue;
        }
        for (col = 0; col < TILE_WIDTH; col++) {
            int dest_x = screen_x + col;
            int pix;
            if (dest_x < 0 || dest_x >= VIDEO_WIDTH)
                continue;
            pix = dest_y * VIDEO_WIDTH + dest_x;
#ifndef DOS
            if (write_color)
                color[pix] = pixels[row * 8 + col];
#endif
            mask[pix] = texels[row * 8 + col] != 0;
        }
    }
}

static void video_draw_sprite(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr)
{
    uint16_t chr_index;
    uint8_t palette = (uint8_t)(attr & 0x03);
    uint8_t hflip = (uint8_t)((attr >> 6) & 1);
    uint8_t vflip = (uint8_t)((attr >> 7) & 1);
    const uint8_t *opaque_src = opaque_dest ? opaque_dest : bg_opaque_mask;
    int row, col;

    if (!video_ready)
        return;
    chr_index = tile;
    if (chr_index >= TILES_PER_BANK)
        return;

    for (row = 0; row < TILE_HEIGHT; row++) {
        int src_row = vflip ? (TILE_HEIGHT - 1 - row) : row;
        int dest_y = y + 1 + row;
        if (dest_y >= VIDEO_HEIGHT || dest_y < 0)
            continue;
        if (drawing_sprite_index >= 0 &&
            !(sprite_row_mask[drawing_sprite_index] & (uint8_t)(1u << row)))
            continue;
        for (col = 0; col < TILE_WIDTH; col++) {
            int src_col = hflip ? (TILE_WIDTH - 1 - col) : col;
            int dest_x = x + col;
            uint8_t color_idx;
            int pix;
            if (dest_x >= VIDEO_WIDTH || dest_x < 0)
                continue;
            color_idx = chr_texels[chr_index * 64 + src_row * 8 + src_col];
            if (color_idx == 0)
                continue;
            pix = dest_y * VIDEO_WIDTH + dest_x;
            if (sprite_claimed[pix] == sprite_claim_gen)
                continue;
            sprite_claimed[pix] = sprite_claim_gen;
            if ((attr & 0x20) && opaque_src[pix])
                continue;
#ifdef DOS
            dos_vga_sprite_pixel(dest_x, dest_y,
                                 cached_expanded_palette[palette + 4][color_idx]);
#else
            frame_index_buffer[pix] =
                rendered_tiles[palette + 4][chr_index * 64 +
                                             src_row * 8 + src_col];
#endif
        }
    }
}

static void video_draw_sprites(uint8_t *oam_data, uint16_t count)
{
    uint8_t line_count[VIDEO_HEIGHT];
    int n = (count < 64) ? (int)count : 64;
    int i, row;

    sprite_claim_gen++;
    if (sprite_claim_gen == 0) {
        memset(sprite_claimed, 0, sizeof(sprite_claimed));
        sprite_claim_gen = 1;
    }
    memset(sprite_row_mask, 0, sizeof(sprite_row_mask));
    memset(line_count, 0, sizeof(line_count));

    for (i = 0; i < n; i++) {
        uint8_t sy = oam_data[i * 4];
        unsigned top;
        if (sy == 0xff)
            continue;
        top = (unsigned)sy + 1;
        if (top >= VIDEO_HEIGHT)
            continue;
        for (row = 0; row < TILE_HEIGHT; row++) {
            unsigned y = top + (unsigned)row;
            if (y >= VIDEO_HEIGHT)
                break;
            if (line_count[y] < 8)
                sprite_row_mask[i] |= (uint8_t)(1u << row);
            line_count[y]++;
        }
    }

    for (i = 0; i < n; i++) {
        uint8_t sy = oam_data[i * 4];
        if (sy == 0xFF)
            continue;
        if ((unsigned)sy + 1 >= VIDEO_HEIGHT)
            continue;
        if (!sprite_row_mask[i])
            continue;
#ifdef DOS
        dos_vga_sprite_begin(i, oam_data[i * 4 + 3], (int)sy + 1);
#endif
        drawing_sprite_index = i;
        video_draw_sprite(oam_data[i * 4 + 3], sy, oam_data[i * 4 + 1],
                          oam_data[i * 4 + 2]);
    }
    drawing_sprite_index = -1;
}

static void video_clear_target(uint8_t *color, uint8_t *opaque)
{
    uint8_t bg_idx = (uint8_t)(PPU_ReadPalette(0) & 0x3F);
    if (color)
        memset(color, bg_idx, BG_MASK_SIZE);
    memset(opaque, 0, BG_MASK_SIZE);
}

#ifdef DOS
static int compute_scroll_delta(void)
{
    int old_c = (int)persistent_nt * 256 + persistent_scroll;
    int new_c = (int)g_RenderNT * 256 + g_RenderScrollX;
    int d = new_c - old_c;
    if (d < -224)
        d += 512;
    if (d > 224)
        d -= 512;
    return d;
}

static void full_rebuild_bg(int bump_serial)
{
    if (bump_serial)
        ++bg_serial;
    dos_vga_on_camera(world_cam, bg_serial, 0, 1);
    video_clear_target(NULL, persistent_opaque);
    set_draw_target(NULL, persistent_opaque);
    if (g_RenderEnabledLatch) {
        PPU_SetScroll(g_RenderScrollX, 0);
        PPU_SetScroll(0, 0);
        PPU_RenderNametable(g_RenderNT);
    } else {
        PPU_ClearDirtyTiles();
        dos_vga_fill_backdrop();
    }
}

static void compose_persistent_bg(void)
{
    int delta = 0;
    int split;
    int page_reset;
    uint8_t pal_dirty = PPU_TakePaletteDirty();
    dos_vga_update_palette(pal_dirty);
    if (persistent_valid) {
        delta = compute_scroll_delta();
        world_cam = (uint16_t)(world_cam + delta);
    }

    if (!g_RenderEnabledLatch) {
        int transition = !persistent_valid || persistent_enabled;
        if (transition)
            ++bg_serial;
        dos_vga_on_camera(world_cam, bg_serial, 0, transition);
        video_clear_target(NULL, persistent_opaque);
        set_draw_target(NULL, persistent_opaque);
        PPU_ClearDirtyTiles();
        dos_vga_fill_backdrop();
        persistent_valid = 1;
        persistent_scroll = g_RenderScrollX;
        persistent_nt = g_RenderNT;
        persistent_enabled = 0;
        return;
    }

    if (!persistent_valid || persistent_enabled != g_RenderEnabledLatch) {
        full_rebuild_bg(1);
        persistent_valid = 1;
        persistent_scroll = g_RenderScrollX;
        persistent_nt = g_RenderNT;
        persistent_enabled = g_RenderEnabledLatch;
        return;
    }

    if (delta < 0 || delta > 16) {
        full_rebuild_bg(1);
        persistent_scroll = g_RenderScrollX;
        persistent_nt = g_RenderNT;
        return;
    }

    page_reset = dos_vga_on_camera(world_cam, bg_serial, delta, 0);
    PPU_SetScroll(g_RenderScrollX, 0);
    PPU_SetScroll(0, 0);
    set_draw_target(NULL, persistent_opaque);
    if (page_reset) {
        video_clear_target(NULL, persistent_opaque);
        PPU_RenderNametable(g_RenderNT);
        persistent_scroll = g_RenderScrollX;
        persistent_nt = g_RenderNT;
        persistent_enabled = g_RenderEnabledLatch;
        return;
    }
    split = g_RenderSprite0Split ? 32 : 0;

    if (delta > 0) {
        int y;
        for (y = split; y < VIDEO_HEIGHT; y++) {
            memmove(persistent_opaque + y * VIDEO_WIDTH,
                    persistent_opaque + y * VIDEO_WIDTH + delta,
                    (size_t)(VIDEO_WIDTH - delta));
            memset(persistent_opaque + y * VIDEO_WIDTH + VIDEO_WIDTH - delta, 0,
                   (size_t)delta);
        }
    }

    /* Dirty flags are global, but the DOS compositor owns two persistent VGA
     * pages.  Scan the current nametable on every page visit and let the
     * page-local tile cache reject unchanged cells; this prevents one page
     * retaining the pre-hit/pre-break version of a tile. */
    PPU_RenderNametable(g_RenderNT);
    persistent_scroll = g_RenderScrollX;
    persistent_nt = g_RenderNT;
    persistent_enabled = g_RenderEnabledLatch;
}
#endif

void video_render_begin(void)
{
    if (!video_ready)
        return;
    dirty_reset();
#ifdef DOS
    refresh_palette_luts();
    compose_persistent_bg();
    dos_vga_flush_tiles();
#else
    refresh_palette_cache();
    video_clear_target(frame_index_buffer, bg_opaque_mask);
    if (g_RenderEnabledLatch) {
        PPU_SetScroll(g_RenderScrollX, 0);
        PPU_SetScroll(0, 0);
        PPU_RenderNametable(g_RenderNT);
    }
    mark_dirty_all();
#endif
}

void video_render_end(void)
{
    if (!video_ready)
        return;
#ifdef DOS
    dos_vga_flush_tiles();
    dos_vga_present_hud();
#endif
    if (g_RenderEnabledLatch)
        video_draw_sprites(g_RenderOAM, 64);
}
