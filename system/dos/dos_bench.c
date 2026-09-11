#include "dos_bench.h"

#ifdef DOS_BENCH
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DosBench g_DosBench;
extern uint32_t platform_audio_underruns(void);
extern uint32_t platform_audio_underrun_samples(void);
extern uint32_t platform_audio_overruns(void);
extern uint32_t platform_audio_dma_blocks(void);
extern uint32_t platform_audio_fifo_samples(void);
typedef struct {
    double work_ms, compositor_ms, upload_ms, audio_ms;
    uint32_t bytes, tiles;
    uint32_t hud_cells, hud_bytes, hud_spans;
    double hud_content_ms, hud_diff_ms, sprite_ms;
} FrameTrace;
static FrameTrace *trace;
static uint32_t trace_count;
static uclock_t prev_compositor, prev_upload, prev_audio;
static uint32_t prev_bytes, prev_tiles;
static uint32_t prev_hud_cells, prev_hud_bytes, prev_hud_spans;
static uclock_t prev_hud_content, prev_hud_diff, prev_sprite;

uclock_t dos_bench_now(void) { return uclock(); }

void dos_bench_parse(int argc, char **argv) {
    int i;
    memset(&g_DosBench, 0, sizeof(g_DosBench));
    g_DosBench.frames = 1200;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            g_DosBench.frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--no-audio") == 0)
            g_DosBench.no_audio = 1;
        else if (strcmp(argv[i], "--no-wait") == 0)
            g_DosBench.no_wait = 1;
        else if (strcmp(argv[i], "--no-compositor") == 0)
            g_DosBench.no_compositor = 1;
        else if (strcmp(argv[i], "--no-upload") == 0)
            g_DosBench.no_upload = 1;
        else if (strcmp(argv[i], "--diagnostic") == 0)
            g_DosBench.diagnostic = 1;
        else if (strcmp(argv[i], "--readback") == 0)
            g_DosBench.readback = 1;
        else if (strcmp(argv[i], "--profile-renderer") == 0)
            g_DosBench.profile_renderer = 1;
    }
}

void dos_bench_start(void) {
    if (g_DosBench.frames <= 100000)
        trace = (FrameTrace *)calloc(g_DosBench.frames, sizeof(FrameTrace));
    g_DosBench.started = dos_bench_now();
}

static double seconds(uclock_t ticks) {
    return (double)ticks / (double)UCLOCKS_PER_SEC;
}

void dos_bench_record_frame(uclock_t elapsed) {
    unsigned bin = (unsigned)(elapsed * 10000 / UCLOCKS_PER_SEC);
    if (bin > 1023)
        bin = 1023;
    g_DosBench.frame_hist[bin]++;
    g_DosBench.measured_frames++;
    if (elapsed > g_DosBench.max_frame_ticks)
        g_DosBench.max_frame_ticks = elapsed;
    if (elapsed > UCLOCKS_PER_SEC / 60)
        g_DosBench.deadline_overruns++;
    if (elapsed > UCLOCKS_PER_SEC * 14 / 1000)
        g_DosBench.over_14ms++;
    if (trace && trace_count < g_DosBench.frames) {
        FrameTrace *t = &trace[trace_count++];
        t->work_ms = seconds(elapsed) * 1000;
        t->compositor_ms =
            seconds(g_DosBench.compositor_ticks - prev_compositor) * 1000;
        t->upload_ms = seconds(g_DosBench.upload_ticks - prev_upload) * 1000;
        t->audio_ms = seconds(g_DosBench.audio_ticks - prev_audio) * 1000;
        t->bytes = g_DosBench.vga_bytes - prev_bytes;
        t->tiles = g_DosBench.tiles_plotted - prev_tiles;
        t->hud_cells = g_DosBench.hud_cells - prev_hud_cells;
        t->hud_bytes = g_DosBench.hud_bytes - prev_hud_bytes;
        t->hud_spans = g_DosBench.hud_spans - prev_hud_spans;
        t->hud_content_ms =
            seconds(g_DosBench.hud_content_ticks - prev_hud_content) * 1000;
        t->hud_diff_ms = seconds(g_DosBench.hud_diff_ticks - prev_hud_diff) * 1000;
        t->sprite_ms = seconds(g_DosBench.sprite_ticks - prev_sprite) * 1000;
    }
    prev_compositor = g_DosBench.compositor_ticks;
    prev_upload = g_DosBench.upload_ticks;
    prev_audio = g_DosBench.audio_ticks;
    prev_bytes = g_DosBench.vga_bytes;
    prev_tiles = g_DosBench.tiles_plotted;
    prev_hud_cells = g_DosBench.hud_cells;
    prev_hud_bytes = g_DosBench.hud_bytes;
    prev_hud_spans = g_DosBench.hud_spans;
    prev_hud_content = g_DosBench.hud_content_ticks;
    prev_hud_diff = g_DosBench.hud_diff_ticks;
    prev_sprite = g_DosBench.sprite_ticks;
}

static double percentile(unsigned percent) {
    unsigned i, total = 0;
    for (i = 0; i < 1024; i++) {
        total += g_DosBench.frame_hist[i];
        if (total * 100 >= g_DosBench.measured_frames * percent)
            return (i + 1) * 0.1;
    }
    return 102.4;
}

void dos_bench_write(uint32_t completed_frames) {
    FILE *f;
    uclock_t elapsed = dos_bench_now() - g_DosBench.started;
    f = fopen("BENCH.TXT", "w");
    if (!f)
        return;
    fprintf(f, "frames=%lu\n", (unsigned long)completed_frames);
    fprintf(f, "readback=%lu\nreadback_errors=%lu\n",
            (unsigned long)g_DosBench.readback,
            (unsigned long)g_DosBench.readback_errors);
    fprintf(f, "renderer=hybrid-ring-v7\nbuild=%s %s\nvga_bytes=%lu\n", __DATE__,
            __TIME__, (unsigned long)g_DosBench.vga_bytes);
    fprintf(
        f,
        "profile_renderer=%lu\nhud_cells_rebuilt=%lu\nhud_bytes=%lu\nhud_spans=%"
        "lu\nhud_content_seconds=%.6f\nhud_diff_seconds=%.6f\nsprite_seconds=%.6f\n",
        (unsigned long)g_DosBench.profile_renderer, (unsigned long)g_DosBench.hud_cells,
        (unsigned long)g_DosBench.hud_bytes, (unsigned long)g_DosBench.hud_spans,
        seconds(g_DosBench.hud_content_ticks), seconds(g_DosBench.hud_diff_ticks),
        seconds(g_DosBench.sprite_ticks));
    fprintf(
        f,
        "diagnostic=%lu\nwork_max_ms=%.3f\nwork_p95_ms_bucket=%.1f\nwork_p99_ms_bucket="
        "%.1f\nwork_over_14ms=%lu\nwork_over_16_67ms=%lu\nprobe_1MiB_seconds=%.6f\n",
        (unsigned long)g_DosBench.diagnostic,
        seconds(g_DosBench.max_frame_ticks) * 1000, percentile(95), percentile(99),
        (unsigned long)g_DosBench.over_14ms,
        (unsigned long)g_DosBench.deadline_overruns,
        seconds(g_DosBench.vga_probe_ticks));
    fprintf(f, "elapsed_seconds=%.6f\n", seconds(elapsed));
    fprintf(f, "logic_ticks=%lu\nmax_catchup_ticks=%lu\n",
            (unsigned long)g_DosBench.logic_ticks,
            (unsigned long)g_DosBench.max_catchup_ticks);
    fprintf(f, "audio_underruns=%lu\naudio_underrun_samples=%lu\n"
               "audio_overruns=%lu\naudio_dma_blocks=%lu\naudio_fifo_samples=%lu\n",
            (unsigned long)platform_audio_underruns(),
            (unsigned long)platform_audio_underrun_samples(),
            (unsigned long)platform_audio_overruns(),
            (unsigned long)platform_audio_dma_blocks(),
            (unsigned long)platform_audio_fifo_samples());
    fprintf(f, "game_fps=%.6f\n", elapsed ? completed_frames / seconds(elapsed) : 0.0);
    fprintf(f, "wait_seconds=%.6f\n", seconds(g_DosBench.wait_ticks));
    fprintf(f, "nmi_seconds=%.6f\n", seconds(g_DosBench.nmi_ticks));
    fprintf(f, "audio_seconds=%.6f\n", seconds(g_DosBench.audio_ticks));
    fprintf(f, "compositor_seconds=%.6f\n", seconds(g_DosBench.compositor_ticks));
    fprintf(f, "upload_seconds=%.6f\n", seconds(g_DosBench.upload_ticks));
    fprintf(f, "retrace_samples=%lu\n", (unsigned long)g_DosBench.retrace_samples);
    fprintf(f, "retrace_hz=%.6f\n",
            g_DosBench.retrace_ticks
                ? g_DosBench.retrace_samples / seconds(g_DosBench.retrace_ticks)
                : 0.0);
    fprintf(f, "no_audio=%u\nno_wait=%u\nno_compositor=%u\nno_upload=%u\n",
            g_DosBench.no_audio, g_DosBench.no_wait, g_DosBench.no_compositor,
            g_DosBench.no_upload);
    fprintf(f, "tiles_plotted=%lu\ntiles_skipped=%lu\npage_repaints=%lu\n",
            (unsigned long)g_DosBench.tiles_plotted,
            (unsigned long)g_DosBench.tiles_skipped,
            (unsigned long)g_DosBench.page_repaints);
    fclose(f);
    if (trace) {
        uint32_t i;
        f = fopen("FRAMES.CSV", "w");
        if (f) {
            fprintf(f,
                    "frame,work_ms,compositor_ms,upload_ms,audio_ms,vga_bytes,tiles");
            if (g_DosBench.profile_renderer)
                fprintf(f, ",hud_cells,hud_bytes,hud_spans,hud_content_ms,hud_diff_ms,"
                           "sprite_ms");
            fputc('\n', f);
            for (i = 0; i < trace_count; i++) {
                FrameTrace *t = &trace[i];
                fprintf(f, "%lu,%.3f,%.3f,%.3f,%.3f,%lu,%lu", (unsigned long)i,
                        t->work_ms, t->compositor_ms, t->upload_ms, t->audio_ms,
                        (unsigned long)t->bytes, (unsigned long)t->tiles);
                if (g_DosBench.profile_renderer)
                    fprintf(f, ",%lu,%lu,%lu,%.3f,%.3f,%.3f",
                            (unsigned long)t->hud_cells, (unsigned long)t->hud_bytes,
                            (unsigned long)t->hud_spans, t->hud_content_ms,
                            t->hud_diff_ms, t->sprite_ms);
                fputc('\n', f);
            }
            fclose(f);
        }
        free(trace);
        trace = 0;
    }
}
#endif
