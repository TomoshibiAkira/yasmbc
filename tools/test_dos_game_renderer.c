/* End-to-end differential test for the persistent DOS renderer.
 *
 * Runs the real no-input title/demo sequence and compares every presented
 * DOS Mode X page with the canonical software compositor.  The deliberately
 * uneven tick schedule models frames dropped by a slow host during startup.
 */
#include "../constants/defs.h"
#include "../constants/globals.h"
#include "../engine/assets.h"
#include "../engine/nmi.h"
#include "../engine/opermode.h"
#include "../engine/score.h"
#include "../system/common/ppu_memory.h"
#include "../system/dos/dos_renderer.h"
#include "../system/platform.h"
#include "../system/sdl/video_soft.h"

#include <stdio.h>
#include <string.h>

static uint8_t chr[8192];
static uint8_t planes[4][65536];
static DosRenderFrame frame;
static const DosRenderResult *prepared;
static int page;
static unsigned output_frame;
static int failed;

void platform_init(void) {}
void platform_shutdown(void) {}
void platform_set_headless(uint8_t enabled) { (void)enabled; }
void platform_read_input(InputState *state) { memset(state, 0, sizeof(*state)); }
uint8_t platform_should_quit(void) { return 0; }
void platform_wait_frame(void) {}
const uint8_t *platform_get_frame_indices(void) { return video_indices(); }
void platform_save_frame(void) {}
void platform_audio_init(void) {}
void platform_audio_shutdown(void) {}
void platform_apu_write(uint16_t address, uint8_t value) {
    (void)address;
    (void)value;
}
void platform_audio_frame(void) {}
void platform_audio_trace_begin(uint32_t ordinal, const uint8_t *state,
                                uint16_t size) {
    (void)ordinal;
    (void)state;
    (void)size;
}
void platform_audio_trace_end(const uint8_t *state, uint16_t size) {
    (void)state;
    (void)size;
}

static void apply_jobs(const DosRenderResult *b) {
    unsigned i;
    int p, y, x;
    for (p = 0; p < 4; p++) {
        for (i = 0; i < b->tile_count; i++) {
            const DosTileJob *job = &b->tiles[i];
            for (y = 0; y < 8; y++)
                for (x = 0; x < 2; x++) {
                    unsigned a = job->offset + y * DR_PITCH + x;
                    uint8_t value = job->pixels[p * 16 + y * 2 + x];
                    planes[p][a] = value;
                    planes[p][a + 66] = value;
                }
        }
        for (i = 0; i < b->hud_run_count; i++) {
            const DosHudRun *run = &b->hud_runs[i];
            if (run->plane == p)
                memcpy(planes[p] + run->offset, run->data, run->length);
        }
    }
}

void platform_render_begin(void) {
    PPU_CopyRenderMemory(frame.nt, frame.palette);
    memcpy(frame.oam, g_RenderOAM, sizeof(frame.oam));
    frame.scroll = g_RenderScrollX;
    frame.nametable = g_RenderNT;
    frame.enabled = g_RenderEnabledLatch;
    frame.split = g_RenderSprite0Split;
    frame.generation = PPU_GetNametableGeneration();
    prepared = dos_renderer_prepare(&frame, page);
    apply_jobs(prepared);
    video_render_begin();
}

void platform_render_end(void) {
    const uint8_t *expected;
    int x, y;
    video_render_end();
    expected = video_indices();
    for (y = 0; y < 240; y++)
        for (x = 0; x < 256; x++) {
            int vx = prepared->pan + x;
            unsigned index =
                planes[vx & 3][page * DR_PAGE_BYTES + y * DR_PITCH + vx / 4];
            unsigned got = prepared->palette[index & 31];
            if (got >= 64)
                got = 0x30;
            if (got != expected[y * 256 + x]) {
                fprintf(stderr,
                        "output=%u logic=%u page=%d enabled=%u split=%u camera=%u:%u "
                        "pixel=%d,%d index=%u DOS=%u SDL=%u\n",
                        output_frame, (unsigned)g_FrameCounter, page, frame.enabled,
                        frame.split, frame.nametable, frame.scroll, x, y, index, got,
                        expected[y * 256 + x]);
                failed = 1;
                return;
            }
        }
    page ^= 1;
}

int main(int argc, char **argv) {
    /* Derived from the 486DX/66 v3 FRAMES.CSV: each long render causes the
     * next presentation to catch the corresponding number of NTSC ticks. */
    static const uint8_t startup_ticks[] = {
        1, 4, 4, 5, 5, 4, 3, 3, 2, 1, 2, 2,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1
    };
    unsigned logic_frame = 0;
    int press_start = argc > 1 && strcmp(argv[1], "--start") == 0;
    Assets_SetArgv0(argv[0]);
    PPU_Init();
    if (Assets_Init() != 0 || Assets_Copy("tiles.chr", chr, sizeof(chr)) != 0 ||
        video_init() != 0)
        return 2;
    dos_renderer_init(chr);
    memset(planes, 0, sizeof(planes));
    memset(planes[0] + DR_PAGE_BYTES, 32, DR_PAGE_BYTES);

    g_NumberOfPlayers = 0;
    g_CurrentPlayer = 0;
    g_WorldNumber = WORLD_1;
    g_LevelNumber = 0;
    g_NumberofLives = 3;
    g_OnscreenPlayerInfo[0] = 3;
    g_OffscreenPlayerInfo[0] = 3;
    g_CoinTally = 0;
    g_PlayerScore = 0;
    Score_Reset();
    OperMode_SetMode(TITLE_SCREEN_MODE);
    g_GameEngineSubroutine = 0;
    g_FrameCounter = 0;

    for (output_frame = 0; output_frame < 1800; output_frame++) {
        unsigned ticks = output_frame < sizeof(startup_ticks)
                             ? startup_ticks[output_frame]
                             : 1;
        if (press_start && output_frame >= 120 && output_frame < 260)
            ticks = (output_frame * 5u + 3u) % 8u + 1u;
        while (ticks--) {
            if (logic_frame >= 5)
                NMI_Tick(press_start && logic_frame == 180 ? BTN_START : 0, 0);
            logic_frame++;
        }
        platform_render_begin();
        platform_render_end();
        if (failed)
            return 1;
    }
    printf("PASS real title/%s sequence with skipped startup presentations\n",
           press_start ? "start-game" : "attract-demo");
    return 0;
}
