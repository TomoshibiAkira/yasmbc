/* DOS host; direct persistent Mode X renderer, no software framebuffer. */
#include "../../engine/assets.h"
#include "dos_bench.h"
#include "dos_renderer.h"
#include "../common/nes_rgb.h"
#include "../platform.h"
#include "../common/ppu_memory.h"
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <string.h>
#include <sys/nearptr.h>
#include <time.h>
#define VGA_SEQ 0x3C4
#define VGA_CRTC 0x3D4
#define VGA_GC 0x3CE
#define VGA_IS1 0x3DA
#define VGA_AC 0x3C0
#define PITCH_BYTES DR_PITCH
static uint8_t *vga, quit_requested;
static volatile uint8_t key_down[128];
static _go32_dpmi_seginfo old_kb, new_kb;
static int kb_installed, nearptr_ok, graphics_on, vga_plane;
static int vis_page, back_page = 1, ready, page_pan[2];
static uint8_t dac_colors[64], dac_valid[2], chr[8192];
static DosRenderFrame frame;
static const DosRenderResult *pending;
static uint32_t frame_memory_generation;
#ifdef DOS_BENCH
static unsigned diagnostic_frame;
static void diagnostic_snapshot(void) {
    unsigned n = diagnostic_frame++, i;
    memset(&frame, 0, sizeof(frame));
    memset(frame.oam, 255, 256);
    frame.enabled = 1;
    frame.split = 1;
    frame.scroll = n & 255;
    frame.nametable = (n >> 8) & 1;
    for (i = 0; i < 2048; i++)
        frame.nt[i] = (i / 32 < 30) ? (i & 31) : 0;
    for (i = 0; i < 32; i++)
        frame.palette[i] =
            (i % 4 == 0) ? 0x0f : ((i % 3 == 0) ? 0x30 : (i % 3 == 1 ? 0x16 : 0x2a));
    /* Fixed HUD ruler, scrolling background, visible per-frame bit counter. */
    for (i = 0; i < 16; i++)
        frame.nt[64 + i] = (n & (1u << i)) ? 1 : 0;
    frame.oam[0] = 29;
    frame.oam[1] = 7;
    frame.oam[2] = 0;
    frame.oam[3] = n & 255;
}
#endif
extern uint8_t g_RenderEnabledLatch, g_RenderScrollX, g_RenderNT, g_RenderSprite0Split,
    g_RenderOAM[256];
extern void dos_direct_tile(uint8_t *, const uint8_t *);
extern void dos_copy_span(uint8_t *, const uint8_t *, unsigned);
static void crtc_write(uint8_t index, uint8_t value) {
    outportb(VGA_CRTC, index);
    outportb(VGA_CRTC + 1, value);
}
static void seq_write(uint8_t index, uint8_t value) {
    if (index == 2)
        vga_plane = -99;
    outportb(VGA_SEQ, index);
    outportb(VGA_SEQ + 1, value);
}
static void seq_plane(int plane) {
    uint8_t mask;
    if (plane == vga_plane)
        return;
    vga_plane = plane;
    mask = (plane < 0) ? 0x0F : (uint8_t)(1 << plane);
    outportb(VGA_SEQ, 0x02);
    outportb(VGA_SEQ + 1, mask);
}
static void set_mode13(void) {
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0013;
    __dpmi_int(0x10, &r);
}
static void set_text_mode(void) {
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
}
static void set_mode_x_320x240(void) {
    uint8_t protect;
    set_mode13();
    seq_write(0x04, 0x06);
    outportb(VGA_GC, 0x05);
    outportb(VGA_GC + 1, 0x40);
    outportb(VGA_GC, 0x06);
    outportb(VGA_GC + 1, 0x05);
    seq_write(0x00, 0x01);
    outportb(0x3C2, 0xE3);
    seq_write(0x00, 0x03);
    outportb(VGA_CRTC, 0x11);
    protect = inportb(VGA_CRTC + 1);
    crtc_write(0x11, (uint8_t)(protect & 0x7F));
    /* 64 character clocks at four pixels/clock in 256-color mode.  The
     * remaining horizontal interval is overscan, so camera panning cannot
     * drag stale VRAM into a software-drawn border. */
    crtc_write(0x01, 0x3F);
    crtc_write(0x06, 0x0D);
    crtc_write(0x07, 0x3E);
    crtc_write(0x09, 0x41);
    crtc_write(0x10, 0xEA);
    crtc_write(0x11, 0xAC);
    crtc_write(0x12, 0xDF);
    crtc_write(0x14, 0x00);
    crtc_write(0x15, 0xE7);
    crtc_write(0x16, 0x06);
    crtc_write(0x17, 0xE3);
    crtc_write(0x13, (uint8_t)(PITCH_BYTES / 2));
}
static void set_pel_pan(uint8_t pan) {
    inportb(VGA_IS1);
    outportb(VGA_AC, 0x33);
    /* In 256-color mode AR13 bit 0 must remain clear: values 0,2,4,6
     * select the four pixel phases within one display byte. */
    outportb(VGA_AC, (uint8_t)((pan & 3) << 1));
}
static void key_irq(void) {
    uint8_t sc = inportb(0x60);
    uint8_t ack;
    if (sc != 0xE0) {
        uint8_t make = (uint8_t)((sc & 0x80) == 0);
        uint8_t code = (uint8_t)(sc & 0x7F);
        if (code < 128)
            key_down[code] = make;
        if (code == 0x01)
            quit_requested = 1;
    }
    ack = inportb(0x61);
    outportb(0x61, (uint8_t)(ack | 0x80));
    outportb(0x61, ack);
    outportb(0x20, 0x20);
}
static void install_keyboard(void) {
    _go32_dpmi_get_protected_mode_interrupt_vector(0x09, &old_kb);
    new_kb.pm_offset = (unsigned long)key_irq;
    new_kb.pm_selector = _go32_my_cs();
    _go32_dpmi_allocate_iret_wrapper(&new_kb);
    _go32_dpmi_set_protected_mode_interrupt_vector(0x09, &new_kb);
    kb_installed = 1;
}
static void remove_keyboard(void) {
    if (!kb_installed)
        return;
    _go32_dpmi_set_protected_mode_interrupt_vector(0x09, &old_kb);
    _go32_dpmi_free_iret_wrapper(&new_kb);
    kb_installed = 0;
}
void platform_set_headless(uint8_t enabled) { (void)enabled; }
void platform_set_options(const PlatformOptions *options) { (void)options; }
int platform_audio_rate(void) { return 0; }
int platform_audio_hifi(void) { return 0; }
void platform_read_input(InputState *state) {
    ControllerInput *player1 = &state->controllers[0];
    ControllerInput *player2 = &state->controllers[1];

    memset(state, 0, sizeof(*state));
    player1->a = key_down[0x2D] || key_down[0x52];
    player1->b = key_down[0x2C] || key_down[0x53];
    player1->select = key_down[0x0E];
    player1->start = key_down[0x1C];
    player1->up = key_down[0x48];
    player1->down = key_down[0x50];
    player1->left = key_down[0x4B];
    player1->right = key_down[0x4D];
    player2->a = key_down[0x31];      /* N */
    player2->b = key_down[0x32];      /* M */
    player2->select = key_down[0x24]; /* J */
    player2->start = key_down[0x25];  /* K */
    player2->up = key_down[0x11];     /* W */
    player2->down = key_down[0x1F];   /* S */
    player2->left = key_down[0x1E];   /* A */
    player2->right = key_down[0x20];  /* D */
    if (key_down[0x01])
        quit_requested = 1;
}
uint8_t platform_should_quit(void) { return quit_requested; }

static void upload(const DosRenderResult *b) {
    int i, p;
    for (i = 0; i < 32; i++) {
        unsigned c = b->palette[i];
        if (c >= 64)
            c = 0x30;
        if (dac_valid[b->page] && dac_colors[b->page * 32 + i] == c)
            continue;
        dac_colors[b->page * 32 + i] = c;
        outportb(0x3C8, b->page * 32 + i);
        outportb(0x3C9, nes_rgb[c][0] >> 2);
        outportb(0x3C9, nes_rgb[c][1] >> 2);
        outportb(0x3C9, nes_rgb[c][2] >> 2);
    }
    dac_valid[b->page] = 1;
    /* Uniform cells are common in sky and cleared transition pages.  Select
     * all four maps once so the cell is restored atomically and with one
     * quarter of the host/VGA traffic. */
    seq_plane(-1);
    for (i = 0; i < (int)b->tile_count; i++) {
        const DosTileJob *job = &b->tiles[i];
        if (job->uniform)
            dos_direct_tile(vga + job->offset, job->pixels);
    }
    for (p = 0; p < 4; p++) {
        seq_plane(p);
        for (i = 0; i < (int)b->tile_count; i++) {
            const DosTileJob *job = &b->tiles[i];
            if (!job->uniform)
                dos_direct_tile(vga + job->offset, job->pixels + p * 16);
        }
        for (i = 0; i < (int)b->hud_run_count; i++) {
            const DosHudRun *run = &b->hud_runs[i];
            if (run->plane == p)
                dos_copy_span(vga + run->offset, run->data, run->length);
        }
    }
#ifdef DOS_BENCH
    if (g_DosBench.readback) {
        volatile uint8_t *memory = vga;
        int x, y;
        for (p = 0; p < 4; p++) {
            outportb(VGA_GC, 4);
            outportb(VGA_GC + 1, p);
            for (i = 0; i < (int)b->tile_count; i++)
                for (y = 0; y < 8; y++)
                    for (x = 0; x < 2; x++) {
                        unsigned a = b->tiles[i].offset + y * DR_PITCH + x;
                        uint8_t expected =
                            b->tiles[i].pixels[p * 16 + y * 2 + x];
                        if (memory[a] != expected)
                            g_DosBench.readback_errors++;
                        if (memory[a + 66] != expected)
                            g_DosBench.readback_errors++;
                    }
            for (i = 0; i < (int)b->hud_run_count; i++) {
                const DosHudRun *run = &b->hud_runs[i];
                if (run->plane == p)
                    for (x = 0; x < run->length; x++)
                        if (memory[run->offset + x] != run->data[x])
                            g_DosBench.readback_errors++;
            }
        }
    }
#endif
    page_pan[b->page] = b->pan;
    ready = 1;
}
void platform_init(void) {
    int i;
    PPU_Init();
    frame_memory_generation = 0;
    if (Assets_Init() != 0 || Assets_Copy("tiles.chr", chr, sizeof(chr)) != 0) {
        quit_requested = 1;
        return;
    }
#ifdef DOS_BENCH
    if (g_DosBench.diagnostic) {
        for (i = 0; i < 8192; i++)
            chr[i] = ((i / 16) & 1) ? 0xff : ((i & 7) == 0 ? 0xff : 0x81);
    }
#endif
    dos_renderer_init(chr);
    if (!__djgpp_nearptr_enable()) {
        quit_requested = 1;
        return;
    }
    nearptr_ok = 1;
    vga = (uint8_t *)(0xA0000 + __djgpp_conventional_base);
    install_keyboard();
    set_mode_x_320x240();
    seq_plane(-1);
    memset(vga, 0, 65536);
    memset(vga + DR_PAGE_BYTES, 32, DR_PAGE_BYTES);
    outportb(VGA_GC, 8);
    outportb(VGA_GC + 1, 255);
    outportb(0x3C8, 0);
    for (i = 0; i < 768; i++)
        outportb(0x3C9, 0);
    inportb(VGA_IS1);
    outportb(VGA_AC, 0x31);
    outportb(VGA_AC, 64);
    inportb(VGA_IS1);
    outportb(VGA_AC, 0x20);
    set_pel_pan(0);
    crtc_write(12, 0);
    crtc_write(13, 0);
    graphics_on = 1;
    vis_page = 0;
    back_page = 1;
#ifdef DOS_BENCH
    if (g_DosBench.diagnostic) {
        uclock_t t = dos_bench_now();
        int k;
        volatile uint8_t *probe = vga + DR_PAGE_BYTES;
        seq_plane(0);
        for (k = 0; k < 64; k++)
            for (i = 0; i < 16384; i++)
                probe[i] = (uint8_t)i;
        g_DosBench.vga_probe_ticks = dos_bench_now() - t;
        seq_plane(-1);
        memset(vga + DR_PAGE_BYTES, 0, DR_PAGE_BYTES);
    }
#endif
}
void platform_wait_frame(void) {
    unsigned offset;
#ifdef DOS_BENCH
    uclock_t start = dos_bench_now(), edge;
#endif
    if (!graphics_on || !ready)
        return;
    offset = back_page * DR_PAGE_BYTES + page_pan[back_page] / 4;
#ifdef DOS_BENCH
    if (!g_DosBench.no_wait)
#endif
    {
        while (inportb(VGA_IS1) & 1)
            ;
    }
    /* VGA start address latches on the next vertical retrace.  Stage it
     * during active display, then update the immediately-effective pel pan
     * at that retrace edge so both describe the same frame. */
    crtc_write(12, offset >> 8);
    crtc_write(13, offset & 255);
#ifdef DOS_BENCH
    if (!g_DosBench.no_wait)
#endif
    {
        while (inportb(VGA_IS1) & 8)
            ;
        while (!(inportb(VGA_IS1) & 8))
            ;
    }
    set_pel_pan(page_pan[back_page] & 3);
    vis_page = back_page;
    back_page ^= 1;
    ready = 0;
#ifdef DOS_BENCH
    edge = dos_bench_now();
    g_DosBench.wait_ticks += edge - start;
    if (g_DosBench.last_retrace) {
        g_DosBench.retrace_ticks += edge - g_DosBench.last_retrace;
        g_DosBench.retrace_samples++;
    }
    g_DosBench.last_retrace = edge;
#endif
}
void platform_render_begin(void) {
    uint32_t memory_generation;
#ifdef DOS_BENCH
    uclock_t start;
    if (g_DosBench.no_compositor)
        return;
    start = dos_bench_now();
#endif
    memory_generation = PPU_GetRenderGeneration();
    if (memory_generation != frame_memory_generation) {
        PPU_CopyRenderMemory(frame.nt, frame.palette);
        frame_memory_generation = memory_generation;
    }
    memcpy(frame.oam, g_RenderOAM, 256);
    frame.scroll = g_RenderScrollX;
    frame.nametable = g_RenderNT;
    frame.enabled = g_RenderEnabledLatch;
    frame.split = g_RenderSprite0Split;
    frame.generation = PPU_GetNametableGeneration();
#ifdef DOS_BENCH
    if (g_DosBench.diagnostic)
        diagnostic_snapshot();
#endif
    pending = dos_renderer_prepare(&frame, back_page);
#ifdef DOS_BENCH
    g_DosBench.compositor_ticks += dos_bench_now() - start;
#endif
}
void platform_render_end(void) {
#ifdef DOS_BENCH
    uclock_t start;
    if (g_DosBench.no_compositor || g_DosBench.no_upload)
        return;
    start = dos_bench_now();
#endif
    if (pending)
        upload(pending);
#ifdef DOS_BENCH
    g_DosBench.upload_ticks += dos_bench_now() - start;
    if (pending) {
        g_DosBench.tiles_plotted += pending->background_tiles;
        g_DosBench.vga_bytes += pending->bytes;
        g_DosBench.hud_cells += pending->hud_cells;
        g_DosBench.hud_bytes += pending->hud_bytes;
        g_DosBench.hud_spans += pending->hud_spans;
    }
#endif
}
#ifdef DOS_BENCH
static void dump_visible_ppm(void) {
    static uint8_t planes[4][DR_PAGE_BYTES];
    FILE *f;
    int p, x, y;
    if (!graphics_on)
        return;
    for (p = 0; p < 4; p++) {
        outportb(VGA_GC, 4);
        outportb(VGA_GC + 1, p);
        memcpy(planes[p], vga + vis_page * DR_PAGE_BYTES, DR_PAGE_BYTES);
    }
    f = fopen("VGA.PPM", "wb");
    if (!f)
        return;
    fprintf(f, "P6\n256 240\n255\n");
    for (y = 0; y < 240; y++)
        for (x = 0; x < 256; x++) {
            int vx = page_pan[vis_page] + x;
            unsigned c = planes[vx & 3][y * DR_PITCH + vx / 4];
            c = c < 64 ? dac_colors[c] : 15;
            fwrite(nes_rgb[c], 1, 3, f);
        }
    fclose(f);
}
#endif
void platform_shutdown(void) {
#ifdef DOS_BENCH
    if (ready)
        platform_wait_frame();
    dump_visible_ppm();
#endif
    platform_audio_shutdown();
    remove_keyboard();
    if (graphics_on)
        set_text_mode();
    graphics_on = 0;
    if (nearptr_ok)
        __djgpp_nearptr_disable();
}
const uint8_t *platform_get_frame_indices(void) {
    /* Explicit debug readback; never used in the rendering hot path. */
    static uint8_t indices[256 * 240];
    int p, x, y, page = ready ? back_page : vis_page;
    if (!graphics_on)
        return 0;
    for (p = 0; p < 4; p++) {
        outportb(VGA_GC, 4);
        outportb(VGA_GC + 1, p);
        for (y = 0; y < 240; y++)
            for (x = 0; x < 256; x++) {
                int vx = page_pan[page] + x;
                if ((vx & 3) == p) {
                    unsigned c = vga[page * DR_PAGE_BYTES + y * DR_PITCH + vx / 4];
                    indices[y * 256 + x] = c < 64 ? dac_colors[c] : 15;
                }
            }
    }
    return indices;
}
void platform_save_frame(void) {}
/* Legacy PPU drawing API is unused by the snapshot-based DOS renderer. */
void platform_draw_tile_at(uint8_t tile, uint8_t pal, int x, int y) {
    (void)tile;
    (void)pal;
    (void)x;
    (void)y;
}
