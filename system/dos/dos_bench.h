#ifndef SMB_DOS_BENCH_H
#define SMB_DOS_BENCH_H

#include "../../constants/types.h"

#ifdef DOS_BENCH
#include <time.h>

typedef struct {
    uint32_t frames;
    uint8_t no_audio;
    uint8_t no_wait;
    uint8_t no_compositor;
    uint8_t no_upload;
    uclock_t started;
    uclock_t wait_ticks;
    uclock_t nmi_ticks;
    uclock_t audio_ticks;
    uclock_t compositor_ticks;
    uclock_t upload_ticks;
    uclock_t retrace_ticks;
    uclock_t last_retrace;
    uint32_t retrace_samples;
    uint32_t tiles_plotted;
    uint32_t vga_bytes;
    uint32_t tiles_skipped;
    uint32_t page_repaints;
    uint32_t diagnostic, measured_frames, deadline_overruns, over_14ms;
    uint32_t logic_ticks, max_catchup_ticks;
    uint32_t frame_hist[1024];
    uclock_t max_frame_ticks, vga_probe_ticks;
    uint32_t readback, readback_errors;
    uint32_t profile_renderer,hud_cells,hud_bytes,hud_spans;
    uclock_t hud_content_ticks,hud_diff_ticks,sprite_ticks;
} DosBench;

extern DosBench g_DosBench;

void dos_bench_parse(int argc, char **argv);
void dos_bench_start(void);
void dos_bench_write(uint32_t completed_frames);
void dos_bench_record_frame(uclock_t elapsed);
uclock_t dos_bench_now(void);
#endif

#endif
