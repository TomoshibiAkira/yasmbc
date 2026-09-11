#include "dos_renderer.h"

#include <string.h>

#ifdef DOS_BENCH
#include "dos_bench.h"
#endif

#define INVALID_KEY 0xffffu
#define HUD_PIXELS (256 * 32)

typedef struct {
    uint16_t keys[DR_CELLS];
    uint8_t oam[256], sprite_rows[64];
    uint32_t generation;
    int camera, phase, fine, ring_base;
    uint8_t valid, split, output_enabled;
} PageState;

static uint8_t texels[512][64];
static uint8_t bg_planar[2][4][256][4][16];
static uint8_t bg_uniform[2][4][256];
static PageState pages[2];
static DosRenderFrame prepared_frame;
static DosRenderResult result;
static uint16_t dirty_stamp[DR_CELLS], dirty_cells[DR_CELLS],
    job_stamp[DR_CELLS];
static int16_t job_for_cell[DR_CELLS];
static unsigned dirty_count;
static uint16_t dirty_epoch;
static uint8_t prepared_masks[64], prepared_active[64], prepared_active_count;
static int prepared_page;
static uint8_t prepared_split;
static int renderer_ready, previous_camera, phase;

static struct {
    uint16_t keys[128];
    uint8_t oam[256], pixels[HUD_PIXELS];
    uint8_t valid;
} hud;
static uint8_t hud_phase_cache[2][4][4][32 * 65];
static uint32_t hud_cache_generation[2][4];
static uint32_t hud_generation, hud_page_generation[2];
static int hud_page_pan[2];
static uint8_t hud_shadow[2][4][32 * DR_PITCH];
static uint8_t hud_shadow_valid[2];

static int wrap_mod(int value, int modulus) {
    value %= modulus;
    return value < 0 ? value + modulus : value;
}

static void mark_dirty(int idx) {
    if (dirty_stamp[idx] == dirty_epoch)
        return;
    dirty_stamp[idx] = dirty_epoch;
    dirty_cells[dirty_count++] = (uint16_t)idx;
}

static int camera_delta(int current, int old) {
    int delta = current - old;
    if (delta > 256)
        delta -= 512;
    if (delta < -256)
        delta += 512;
    return delta;
}

static uint16_t background_key(const DosRenderFrame *f, int row, int column,
                               int fixed) {
    int c = fixed ? column : wrap_mod(f->nametable * 32 + f->scroll / 8 + column, 64);
    int base = (c / 32) * 1024, x = c & 31;
    int attr = f->nt[base + 960 + (row / 4) * 8 + x / 4];
    return (uint16_t)(f->nt[base + row * 32 + x] |
                      (((attr >> (((row & 2) << 1) | (x & 2))) & 3) << 8));
}

static void sprite_row_masks(const DosRenderFrame *f, uint8_t masks[64]) {
    uint8_t count[240] = {0};
    int s, y;
    memset(masks, 0, 64);
    prepared_active_count = 0;
    if (!f->enabled)
        return;
    for (s = 0; s < 64; s++) {
        int top = (int)f->oam[s * 4] + 1;
        if (top >= 240)
            continue;
        for (y = top; y < top + 8 && y < 240; y++)
            if (count[y]++ < 8)
                masks[s] |= (uint8_t)(1u << (y - top));
        if (masks[s])
            prepared_active[prepared_active_count++] = (uint8_t)s;
    }
}

static void mark_sprite_cells(const uint8_t oam[256], const uint8_t masks[64],
                              int sprite_phase, int fine, int split) {
    int s, row, col;
    int ring_base = (sprite_phase - fine) / 8;
    for (s = 0; s < 64; s++) {
        int top, left, first_row, last_row, first_col, last_col;
        if (!masks[s])
            continue;
        top = (int)oam[s * 4] + 1;
        left = oam[s * 4 + 3];
        first_row = top / 8;
        last_row = (top + 7) / 8;
        first_col = (left + fine) / 8;
        last_col = (left + 7 + fine) / 8;
        if (last_row >= DR_ROWS)
            last_row = DR_ROWS - 1;
        if (last_col >= DR_RING_TILES)
            last_col = DR_RING_TILES - 1;
        for (row = first_row; row <= last_row; row++) {
            if (split && row < 4)
                continue;
            for (col = first_col; col <= last_col; col++)
                mark_dirty(row * DR_RING_TILES +
                           wrap_mod(ring_base + col, DR_RING_TILES));
        }
    }
}

static void set_cell(PageState *page, const DosRenderFrame *f, int row, int col,
                     int force) {
    int ring_base = (result.pan - (f->scroll & 7)) / 8;
    int slot = wrap_mod(ring_base + col, DR_RING_TILES);
    uint16_t key = background_key(f, row, col, 0);
    int idx = row * DR_RING_TILES + slot;
    if (force || page->keys[idx] != key) {
        page->keys[idx] = key;
        mark_dirty(idx);
    }
}

static void refresh_visible(PageState *page, const DosRenderFrame *f, int force) {
    int row, col, first_row = f->split ? 4 : 0;
    for (row = first_row; row < DR_ROWS; row++)
        for (col = 0; col < DR_RING_TILES; col++)
            set_cell(page, f, row, col, force);
}

static void refresh_entering(PageState *page, const DosRenderFrame *f,
                             int old_ring, int new_ring, int delta) {
    int steps = delta >= 0 ? delta : -delta;
    int row, step, first_row = f->split ? 4 : 0;
    if (steps >= DR_RING_TILES) {
        refresh_visible(page, f, 1);
        return;
    }
    for (step = 1; step <= steps; step++) {
        int col = delta > 0 ? DR_RING_TILES - steps + step - 1 : step - 1;
        int slot = delta > 0 ? wrap_mod(old_ring + step - 1, DR_RING_TILES)
                             : wrap_mod(old_ring - steps + step - 1,
                                        DR_RING_TILES);
        for (row = first_row; row < DR_ROWS; row++) {
            uint16_t key = background_key(f, row, col, 0);
            int idx = row * DR_RING_TILES + slot;
            page->keys[idx] = key;
            mark_dirty(idx);
        }
    }
    (void)new_ring;
}

static void render_hud(const DosRenderFrame *f) {
    uint8_t opaque[HUD_PIXELS / 8] = {0};
    uint8_t claimed[HUD_PIXELS / 8] = {0};
    int row, col, y, x, s;
    memset(opaque, 0, sizeof(opaque));
    for (row = 0; row < 4; row++)
        for (col = 0; col < 32; col++) {
            uint16_t key = background_key(f, row, col, 1);
            const uint8_t *src = texels[256 + (key & 255)];
            unsigned base = (key >> 8) * 4;
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++) {
                    int dst = (row * 8 + y) * 256 + col * 8 + x;
                    hud.pixels[dst] = (uint8_t)(base | src[y * 8 + x]);
                    if (src[y * 8 + x])
                        opaque[dst >> 3] |= (uint8_t)(1u << (dst & 7));
                }
        }
    for (s = 0; s < 64; s++) {
        int top = (int)f->oam[s * 4] + 1, left = f->oam[s * 4 + 3];
        int tile = f->oam[s * 4 + 1], attr = f->oam[s * 4 + 2];
        for (y = 0; y < 8; y++) {
            int sy = top + y, src_y = (attr & 0x80) ? 7 - y : y;
            if (sy < 0 || sy >= 32 || !(prepared_masks[s] & (1u << y)))
                continue;
            for (x = 0; x < 8; x++) {
                int sx = left + x, src_x = (attr & 0x40) ? 7 - x : x;
                int dst;
                uint8_t color;
                if (sx < 0 || sx >= 256)
                    continue;
                color = texels[tile][src_y * 8 + src_x];
                dst = sy * 256 + sx;
                if (!color ||
                    (claimed[dst >> 3] & (uint8_t)(1u << (dst & 7))))
                    continue;
                claimed[dst >> 3] |= (uint8_t)(1u << (dst & 7));
                if ((attr & 0x20) &&
                    (opaque[dst >> 3] & (uint8_t)(1u << (dst & 7))))
                    continue;
                hud.pixels[dst] = (uint8_t)(16 + (attr & 3) * 4 + color);
            }
        }
    }
}

static void prepare_hud(const DosRenderFrame *f, int page) {
    uint8_t filtered_oam[256];
    uint16_t keys[128];
    int row, col, s, p, changed, changed_cells = 0;
    memset(filtered_oam, 255, sizeof(filtered_oam));
    for (s = 0; s < 64; s++)
        if (f->oam[s * 4] < 31)
            memcpy(filtered_oam + s * 4, f->oam + s * 4, 4);
    for (row = 0; row < 4; row++)
        for (col = 0; col < 32; col++) {
            int i = row * 32 + col;
            keys[i] = background_key(f, row, col, 1);
            if (!hud.valid || keys[i] != hud.keys[i])
                changed_cells++;
        }
    changed = !hud.valid || changed_cells ||
              memcmp(filtered_oam, hud.oam, sizeof(filtered_oam)) != 0;
    if (!changed)
        return;
    render_hud(f);
    memcpy(hud.keys, keys, sizeof(keys));
    memcpy(hud.oam, filtered_oam, sizeof(filtered_oam));
    hud.valid = 1;
    result.hud_cells = changed_cells ? (unsigned)changed_cells : 1;
    hud_generation++;
    for (p = 0; p < 2; p++)
        hud_page_generation[p] = 0;
    (void)page;
}

static void build_hud_runs(int page) {
    int ph = phase & 3, start = phase / 4, p, y, i;
    if (!prepared_split)
        return;
    if (hud_page_pan[page] == phase &&
        hud_page_generation[page] == hud_generation)
        return;
    if (hud_cache_generation[page][ph] != hud_generation) {
        for (p = 0; p < 4; p++)
            for (y = 0; y < 32; y++)
                for (i = 0; i < 65; i++) {
                    int x = i * 4 + p - ph;
                    hud_phase_cache[page][ph][p][y * 65 + i] =
                        (uint8_t)((x >= 0 && x < 256
                                       ? hud.pixels[y * 256 + x]
                                       : 0) |
                                  page * 32);
                }
        hud_cache_generation[page][ph] = hud_generation;
    }
    if (!hud_shadow_valid[page])
        memset(hud_shadow[page], 255, sizeof(hud_shadow[page]));
    for (y = 0; y < 32; y++) {
        for (p = 0; p < 4; p++) {
            uint8_t *shadow = hud_shadow[page][p] + y * DR_PITCH + start;
            const uint8_t *src = hud_phase_cache[page][ph][p] + y * 65;
            int chunk = 0;
            while (chunk < 17) {
                int first, last, gap = 0, different;
                if (chunk == 16)
                    different = shadow[64] != src[64];
                else {
                    uint32_t a, b;
                    memcpy(&a, shadow + chunk * 4, 4);
                    memcpy(&b, src + chunk * 4, 4);
                    different = a != b;
                }
                if (!different) {
                    chunk++;
                    continue;
                }
                first = last = chunk++;
                while (chunk < 17) {
                    if (chunk == 16)
                        different = shadow[64] != src[64];
                    else {
                        uint32_t a, b;
                        memcpy(&a, shadow + chunk * 4, 4);
                        memcpy(&b, src + chunk * 4, 4);
                        different = a != b;
                    }
                    if (different) {
                        last = chunk;
                        gap = 0;
                    } else if (++gap > 1) {
                        break;
                    }
                    chunk++;
                }
                {
                    int lo = first * 4;
                    int hi = last == 16 ? 65 : (last + 1) * 4;
                    memcpy(shadow + lo, src + lo, (size_t)(hi - lo));
                    result.hud_runs[result.hud_run_count].plane = (uint8_t)p;
                    result.hud_runs[result.hud_run_count].offset =
                        (uint16_t)(page * DR_PAGE_BYTES + y * DR_PITCH +
                                   start + lo);
                    result.hud_runs[result.hud_run_count].length =
                        (uint8_t)(hi - lo);
                    result.hud_runs[result.hud_run_count].data = shadow + lo;
                    result.hud_run_count++;
                    result.hud_bytes += (unsigned)(hi - lo);
                }
            }
        }
    }
    hud_shadow_valid[page] = 1;
    hud_page_pan[page] = phase;
    hud_page_generation[page] = hud_generation;
    result.hud_spans = result.hud_run_count;
}

static void stage_dirty_cells(const DosRenderFrame *f, PageState *page) {
    uint8_t claimed[DR_CELLS][8];
    int idx, p, s, y, x, fine = f->scroll & 7;
    unsigned dirty_no;
    for (dirty_no = 0; dirty_no < dirty_count; dirty_no++) {
        DosTileJob *job;
        uint16_t key;
        idx = dirty_cells[dirty_no];
        if (page->keys[idx] == INVALID_KEY)
            continue;
        job_for_cell[idx] = (int16_t)result.tile_count;
        job_stamp[idx] = dirty_epoch;
        job = &result.tiles[result.tile_count++];
        key = page->keys[idx];
        job->cell = (uint16_t)idx;
        job->offset =
            (uint16_t)(prepared_page * DR_PAGE_BYTES +
                       (idx / DR_RING_TILES) * 8 * DR_PITCH +
                       (idx % DR_RING_TILES) * 2);
        memcpy(job->pixels, bg_planar[prepared_page][key >> 8][key & 255],
               sizeof(job->pixels));
        job->uniform = bg_uniform[prepared_page][key >> 8][key & 255];
    }
    if (!result.tile_count) {
        result.bytes = result.hud_bytes;
        return;
    }
    memset(claimed, 0, result.tile_count * 8);
    for (idx = 0; idx < prepared_active_count; idx++) {
        s = prepared_active[idx];
        int top = (int)f->oam[s * 4] + 1, left = f->oam[s * 4 + 3];
        int tile = f->oam[s * 4 + 1], attr = f->oam[s * 4 + 2];
        if (!prepared_masks[s] || top >= 240)
            continue;
        for (y = 0; y < 8; y++) {
            int sy = top + y, src_y = (attr & 0x80) ? 7 - y : y;
            int physical_left, first_slot, within_left, cell_row;
            int job_no[2] = {-1, -1};
            int slot, cell, visible_x, split_x;
            if (sy >= 240 || (f->split && sy < 32) ||
                !(prepared_masks[s] & (1u << y)))
                continue;
            physical_left = left + fine;
            first_slot = page->ring_base + (physical_left >> 3);
            if (first_slot >= DR_RING_TILES)
                first_slot -= DR_RING_TILES;
            within_left = physical_left & 7;
            cell_row = (sy >> 3) * DR_RING_TILES;
            cell = cell_row + first_slot;
            if (job_stamp[cell] == dirty_epoch)
                job_no[0] = job_for_cell[cell];
            slot = first_slot + 1;
            if (slot == DR_RING_TILES)
                slot = 0;
            cell = cell_row + slot;
            if (job_stamp[cell] == dirty_epoch)
                job_no[1] = job_for_cell[cell];
            visible_x = 256 - left;
            if (visible_x > 8)
                visible_x = 8;
            split_x = 8 - within_left;
            for (x = 0; x < visible_x; x++) {
                int side = x >= split_x;
                int target = job_no[side];
                int src_x = (attr & 0x40) ? 7 - x : x;
                int within;
                uint8_t color;
                uint8_t *pixel;
                if (target < 0)
                    continue;
                color = texels[tile][src_y * 8 + src_x];
                if (!color)
                    continue;
                within = within_left + x - side * 8;
                if (claimed[target][sy & 7] & (uint8_t)(1u << within))
                    continue;
                claimed[target][sy & 7] |= (uint8_t)(1u << within);
                p = within & 3;
                pixel = &result.tiles[target]
                             .pixels[p * 16 + (sy & 7) * 2 + (within >> 2)];
                if ((attr & 0x20) && (*pixel & 3))
                    continue;
                {
                    uint8_t value = (uint8_t)(prepared_page * 32 + 16 +
                                              (attr & 3) * 4 + color);
                    if (*pixel != value) {
                        *pixel = value;
                        result.tiles[target].uniform = 0;
                    }
                }
            }
        }
    }
    /* A dirty cell is a coherence unit.  Do not maintain a second per-byte
     * shadow beside the two persistent VGA pages: a missed invalidation left
     * old sprite pixels in one plane even though write-only readback passed.
     * Full 8x8 writes retain tile-level dirty tracking and make restoration
     * atomic across all four planes and both ring aliases. */
    result.bytes = result.hud_bytes;
    for (dirty_no = 0; dirty_no < result.tile_count; dirty_no++) {
        DosTileJob *job = &result.tiles[dirty_no];
        /* One all-plane VGA write is sufficient when every plane carries
         * the same bytes (most backdrop/empty cells).  Non-uniform cells
         * retain four independent plane writes. */
        result.bytes += job->uniform ? 32u : 128u;
    }
    result.background_tiles = result.tile_count;
}

void dos_renderer_init(const uint8_t *chr) {
    int t, y, x, p, page, pal;
    memset(pages, 0, sizeof(pages));
    for (p = 0; p < 2; p++)
        for (t = 0; t < DR_CELLS; t++)
            pages[p].keys[t] = INVALID_KEY;
    memset(&hud, 0, sizeof(hud));
    memset(hud_cache_generation, 0, sizeof(hud_cache_generation));
    memset(hud_page_generation, 0, sizeof(hud_page_generation));
    memset(hud_shadow_valid, 0, sizeof(hud_shadow_valid));
    renderer_ready = 0;
    memset(dirty_stamp, 0, sizeof(dirty_stamp));
    memset(job_stamp, 0, sizeof(job_stamp));
    dirty_count = 0;
    dirty_epoch = 0;
    phase = 0;
    hud_generation = 0;
    for (t = 0; t < 512; t++)
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                texels[t][y * 8 + x] =
                    (uint8_t)(((chr[t * 16 + y] >> (7 - x)) & 1) |
                              (((chr[t * 16 + y + 8] >> (7 - x)) & 1) << 1));
    for (page = 0; page < 2; page++)
        for (pal = 0; pal < 4; pal++)
            for (t = 0; t < 256; t++)
                for (p = 0; p < 4; p++)
                    for (y = 0; y < 8; y++)
                        for (x = 0; x < 2; x++)
                            bg_planar[page][pal][t][p][y * 2 + x] =
                                (uint8_t)(page * 32 + pal * 4 +
                                          texels[256 + t][y * 8 + x * 4 + p]);
    for (page = 0; page < 2; page++)
        for (pal = 0; pal < 4; pal++)
            for (t = 0; t < 256; t++)
                bg_uniform[page][pal][t] =
                    memcmp(bg_planar[page][pal][t][0],
                           bg_planar[page][pal][t][1], 16) == 0 &&
                    memcmp(bg_planar[page][pal][t][0],
                           bg_planar[page][pal][t][2], 16) == 0 &&
                    memcmp(bg_planar[page][pal][t][0],
                           bg_planar[page][pal][t][3], 16) == 0;
}

const DosRenderResult *dos_renderer_prepare(const DosRenderFrame *f, int page_no) {
    PageState *page = &pages[page_no];
    int output_enabled = f->enabled;
    int camera = f->nametable * 256 + f->scroll;
    int delta = renderer_ready ? camera_delta(camera, previous_camera) : 0;
    int new_ring, ring_delta, sprites_changed;
    result.tile_count = 0;
    result.hud_run_count = 0;
    result.background_tiles = 0;
    result.hud_bytes = 0;
    result.hud_spans = 0;
    result.hud_cells = 0;
    result.bytes = 0;
    if (++dirty_epoch == 0) {
        memset(dirty_stamp, 0, sizeof(dirty_stamp));
        memset(job_stamp, 0, sizeof(job_stamp));
        dirty_epoch = 1;
    }
    dirty_count = 0;
    prepared_page = page_no;
    result.page = page_no;
    if (!renderer_ready) {
        renderer_ready = 1;
        phase = camera % 264;
    } else {
        phase = wrap_mod(phase + delta, 264);
    }
    previous_camera = camera;
    result.pan = phase;
    memcpy(result.palette, f->palette, 32);
    for (delta = 0; delta < 4; delta++)
        result.palette[delta * 4] = f->palette[0];
    if (!f->enabled) {
        /* Keep both hidden pages coherent while PPU output is blanked.  The
         * DAC still maps every slot to the backdrop, so the in-progress
         * nametable cannot become visible.  When output is enabled again the
         * expensive cold-page repaint has already happened off screen. */
        memset(result.palette, f->palette[0], sizeof(result.palette));
        prepared_frame = *f;
        prepared_frame.enabled = 1;
        prepared_frame.split = 0;
        memset(prepared_frame.oam, 255, sizeof(prepared_frame.oam));
        f = &prepared_frame;
    }
    prepared_split = f->split;
    sprite_row_masks(f, prepared_masks);
    new_ring = (phase - (f->scroll & 7)) / 8;
    if (!page->valid) {
        refresh_visible(page, f, 1);
    } else {
        int page_delta = camera_delta(camera, page->camera);
        if (page_delta >= 8 * (DR_RING_TILES / 2) ||
            page_delta <= -8 * (DR_RING_TILES / 2)) {
            refresh_visible(page, f, 1);
        } else {
            ring_delta = new_ring - page->ring_base;
            if (ring_delta > DR_RING_TILES / 2)
                ring_delta -= DR_RING_TILES;
            if (ring_delta < -DR_RING_TILES / 2)
                ring_delta += DR_RING_TILES;
            if (ring_delta)
                refresh_entering(page, f, page->ring_base, new_ring, ring_delta);
        }
        if (page->generation != f->generation)
            refresh_visible(page, f, 0);
        /* A hidden-page warmup updates cache keys before that page has ever
         * participated in an enabled presentation.  Re-establish physical
         * residency on each page's first enabled visit so title/intermediate
         * columns cannot survive as apparently-clean world cells.  This is a
         * scene lifecycle boundary, not a per-level or per-frame exception. */
        if (output_enabled && !page->output_enabled)
            refresh_visible(page, f, 1);
        if (page->split && !f->split) {
            int row, col;
            for (row = 0; row < 4; row++)
                for (col = 0; col < DR_RING_TILES; col++)
                    set_cell(page, f, row, col, 1);
        }
    }
    sprites_changed = !page->valid || page->phase != phase ||
                      page->fine != (f->scroll & 7) || page->split != f->split ||
                      memcmp(page->oam, f->oam, 256) != 0 ||
                      memcmp(page->sprite_rows, prepared_masks, 64) != 0;
    if (sprites_changed && page->valid)
        mark_sprite_cells(page->oam, page->sprite_rows, page->phase, page->fine,
                          page->split);
    if (sprites_changed)
        mark_sprite_cells(f->oam, prepared_masks, phase, f->scroll & 7, f->split);
    if (f->split) {
#ifdef DOS_BENCH
        uclock_t hud_started =
            g_DosBench.profile_renderer ? dos_bench_now() : 0;
#endif
        prepare_hud(f, page_no);
#ifdef DOS_BENCH
        if (g_DosBench.profile_renderer)
            g_DosBench.hud_content_ticks += dos_bench_now() - hud_started;
#endif
    } else {
        hud.valid = 0;
        hud_page_generation[page_no] = 0;
        hud_shadow_valid[page_no] = 0;
    }
    memcpy(page->oam, f->oam, 256);
    memcpy(page->sprite_rows, prepared_masks, 64);
    page->camera = camera;
    page->phase = phase;
    page->fine = f->scroll & 7;
    page->ring_base = new_ring;
    page->split = f->split;
    page->output_enabled = (uint8_t)output_enabled;
    page->generation = f->generation;
    page->valid = 1;
#ifdef DOS_BENCH
    {
        uclock_t started =
            g_DosBench.profile_renderer ? dos_bench_now() : 0;
#endif
    build_hud_runs(page_no);
#ifdef DOS_BENCH
        if (g_DosBench.profile_renderer)
            g_DosBench.hud_diff_ticks += dos_bench_now() - started;
        started = g_DosBench.profile_renderer ? dos_bench_now() : 0;
#endif
    stage_dirty_cells(f, page);
#ifdef DOS_BENCH
        if (g_DosBench.profile_renderer)
            g_DosBench.sprite_ticks += dos_bench_now() - started;
    }
#endif
    return &result;
}
