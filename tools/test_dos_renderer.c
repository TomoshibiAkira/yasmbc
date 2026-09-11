/* Native differential test: SDL compositor vs persistent DOS planar pages. */
#include "../system/dos/dos_renderer.h"
#include "../system/common/ppu_memory.h"
#include "../system/sdl/video_soft.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t g_RenderEnabledLatch, g_RenderScrollX, g_RenderNT, g_RenderSprite0Split,
    g_RenderOAM[256];
static uint8_t chr[8192], planes[4][65536];
static unsigned rng = 1234567;
static unsigned random_byte(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 24;
}
int Assets_Copy(const char *name, void *dst, size_t len) {
    (void)name;
    assert(len == sizeof(chr));
    memcpy(dst, chr, len);
    return 0;
}
int main(void) {
    DosRenderFrame f;
    unsigned long bytes = 0;
    int n, i, p, y, x;
    for (i = 0; i < 8192; i++)
        chr[i] = random_byte();
    PPU_Init();
    video_init();
    dos_renderer_init(chr);
    for (p = 0; p < 4; p++)
        memset(planes[p] + DR_PAGE_BYTES, 32, DR_PAGE_BYTES);
    PPU_SetFullAddr(0x2000);
    for (i = 0; i < 2048; i++)
        PPU_WriteData(random_byte());
    for (n = 0; n < 2200; n++) {
        int camera = (n < 200 ? 0 : (n < 1500 ? n * 3 : 6600 - n * 4)) & 511;
        g_RenderScrollX = camera & 255;
        g_RenderNT = camera >> 8;
        g_RenderSprite0Split = (n % 197) < 160;
        g_RenderEnabledLatch = n % 151 > 2;
        for (i = 0; i < 32; i++) {
            PPU_SetFullAddr(0x3f00 + i);
            PPU_WriteData(random_byte() & 63);
        }
        if (n % 7 == 0) {
            PPU_SetFullAddr(0x2000 + (rng % 2048));
            PPU_WriteData(random_byte());
        }
        if (n < 100 || n >= 200) {
            memset(g_RenderOAM, 255, 256);
            for (i = 0; i < 64; i++) {
                g_RenderOAM[i * 4] = random_byte();
                g_RenderOAM[i * 4 + 1] = random_byte();
                g_RenderOAM[i * 4 + 2] = random_byte();
                g_RenderOAM[i * 4 + 3] = random_byte();
            }
            if (n % 9 == 0)
                for (i = 0; i < 12; i++) {
                    g_RenderOAM[i * 4] = 29;
                    g_RenderOAM[i * 4 + 3] = 250;
                }
        }
        PPU_CopyRenderMemory(f.nt, f.palette);
        memcpy(f.oam, g_RenderOAM, 256);
        f.scroll = g_RenderScrollX;
        f.nametable = g_RenderNT;
        f.split = g_RenderSprite0Split;
        f.enabled = g_RenderEnabledLatch;
        f.generation = PPU_GetNametableGeneration();
        const DosRenderResult *b = dos_renderer_prepare(&f, n & 1);
        for (p = 0; p < 4; p++) {
            for (i = 0; i < (int)b->tile_count; i++)
                for (y = 0; y < 8; y++)
                    for (x = 0; x < 2; x++) {
                        unsigned a = b->tiles[i].offset + y * DR_PITCH + x;
                        uint8_t value = b->tiles[i].pixels[p * 16 + y * 2 + x];
                        planes[p][a] = planes[p][a + 66] = value;
                    }
            for (i = 0; i < (int)b->hud_run_count; i++) {
                const DosHudRun *run = &b->hud_runs[i];
                if (run->plane == p)
                    memcpy(planes[p] + run->offset, run->data, run->length);
            }
        }
        video_render_begin();
        video_render_end();
        const uint8_t *expected = video_indices();
        for (y = 0; y < 240; y++)
            for (x = 0; x < 256; x++) {
                int vx = b->pan + x;
                unsigned index =
                    planes[vx & 3][b->page * DR_PAGE_BYTES + y * DR_PITCH + vx / 4];
                assert(index / 32 == (unsigned)b->page);
                unsigned got = b->palette[index % 32];
                if (got >= 64)
                    got = 0x30;
                if (got != expected[y * 256 + x]) {
                    fprintf(stderr,
                            "frame=%d camera=%d split=%d enabled=%d pixel=%d,%d DOS=%u "
                            "SDL=%u\n",
                            n, camera, f.split, f.enabled, x, y, got,
                            expected[y * 256 + x]);
                    return 1;
                }
            }
        bytes += b->bytes;
    }
    dos_renderer_init(chr);
    memset(&f, 0, sizeof(f));
    memset(f.oam, 255, sizeof(f.oam));
    f.enabled = 1;
    f.split = 1;
    for (n = 0; n < 4; n++) {
        const DosRenderResult *b = dos_renderer_prepare(&f, n & 1);
        if (n > 0)
            assert(b->hud_cells == 0);
        if (n > 1)
            assert(b->hud_bytes == 0 && b->hud_spans == 0);
    }
    for (n = 1; n < 1100; n++) {
        f.scroll = n & 255;
        f.nametable = (n >> 8) & 1;
        assert(dos_renderer_prepare(&f, n & 1)->hud_cells == 0);
    }
    f.nt[66] ^= 1;
    f.generation++;
    assert(dos_renderer_prepare(&f, 0)->hud_cells != 0);
    printf("PASS cache invariants: stationary upload=0, pure scrolling rebuild=0, one "
           "changed tile rebuild=1\n");
    memset(chr, 0, sizeof(chr));
    dos_renderer_init(chr);
    memset(&f, 0, sizeof(f));
    memset(f.oam, 255, sizeof(f.oam));
    f.enabled = 1;
    {
        const DosRenderResult *solid = dos_renderer_prepare(&f, 0);
        assert(solid->tile_count == DR_CELLS);
        assert(solid->bytes == DR_CELLS * 32u);
        for (i = 0; i < (int)solid->tile_count; i++)
            assert(solid->tiles[i].uniform);
    }
    puts("PASS uniform cells use one atomic all-plane upload");
    dos_renderer_init(chr);
    memset(&f, 0, sizeof(f));
    memset(f.oam, 255, sizeof(f.oam));
    dos_renderer_prepare(&f, 0);
    dos_renderer_prepare(&f, 1);
    f.enabled = 1;
    assert(dos_renderer_prepare(&f, 0)->tile_count == DR_CELLS);
    assert(dos_renderer_prepare(&f, 1)->tile_count == DR_CELLS);
    puts("PASS both warmed pages refresh at the enabled-scene boundary");
    printf("PASS 2200 frames / 135168000 pixels vs SDL; random CHR/palettes/OAM, "
           "overlap, HUD, disable, reverse scroll, ring wrap. bytes=%lu\n",
           bytes);
    return 0;
}
