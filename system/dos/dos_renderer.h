#ifndef SMB_DOS_RENDERER_H
#define SMB_DOS_RENDERER_H

#include <stdint.h>

#define DR_PITCH 132
#define DR_PAGE_BYTES (DR_PITCH * 240)
#define DR_RING_TILES 33
#define DR_ROWS 30
#define DR_CELLS (DR_RING_TILES * DR_ROWS)
#define DR_HUD_RUNS (8 * 4 * 32)

typedef struct {
    uint8_t nt[2048], palette[32], oam[256];
    uint8_t scroll, nametable, enabled, split;
    uint32_t generation;
} DosRenderFrame;

typedef struct {
    uint16_t offset;
    uint16_t cell;
    uint8_t uniform;
    uint8_t pixels[64];
} DosTileJob;

typedef struct {
    uint16_t offset;
    uint8_t plane, length;
    const uint8_t *data;
} DosHudRun;

typedef struct {
    DosTileJob tiles[DR_CELLS];
    DosHudRun hud_runs[DR_HUD_RUNS];
    unsigned tile_count;
    unsigned hud_run_count;
    unsigned background_tiles;
    unsigned hud_bytes;
    unsigned hud_spans;
    unsigned hud_cells;
    unsigned bytes;
    int page;
    int pan;
    uint8_t palette[32];
} DosRenderResult;

void dos_renderer_init(const uint8_t *chr);
const DosRenderResult *dos_renderer_prepare(const DosRenderFrame *frame, int page);

#endif
