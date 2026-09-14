# DOS platform

This is the single design and maintenance document for the DOS frontend. It
describes the `hybrid-ring-v7` implementation. Superseded renderer and
timing experiments were consolidated here so they cannot be mistaken for the
active design.

The DOS build is a playable DJGPP protected-mode host for the shared C engine.
It targets VGA and optional Sound Blaster audio. It is not the reference
comparison host: gameplay correctness remains the responsibility of
SDL/headless tests.

## Build and delivery

The default cross-toolchain prefix is `$HOME/djgpp`; override it with
`DJGPP_PREFIX=`. C code is built with `-O2 -march=i386 -mtune=i486
-fomit-frame-pointer`. Nes_Snd_Emu uses C++11 with exceptions and RTTI disabled.

```bash
make extract ROM=/path/to/smb1.nes
make dos             # dosdist/SMB2.EXE and support files
make dos-bench       # dosdist/BENCH.EXE
make dos-floppy      # dosdist/SMB2.IMG, non-bootable FAT12 data disk
```

The delivery contains `SMB2.EXE`, packed `ASSETS.DAT`, `CWSDPMI.EXE`, and
`README.TXT`. `ASSETS.DAT` preserves normal asset paths inside one
8.3-compatible file. Boot DOS separately, then run from the floppy or copy the
files to a writable hard disk.

The executable uses a 386-compatible instruction set and needs about 4 MiB RAM,
but that is a compatibility floor, not a full-speed specification. There is no
single CPU-only performance floor for this renderer: Mode X upload time depends
on the CPU and chipset, bus width and clock, VGA aperture behavior, display
adapter, and the emulator's model of all of them.

Historical 86Box runs reported about 58 presented FPS on one 486DX/66 setup at
a measured 58 Hz retrace rate, and roughly 40--42 presented FPS on a different
486DX/33 setup during the 1,200-frame workload. These numbers describe those
complete virtual-machine configurations and are not a processor scaling curve
or a promise for every card carrying the same CPU. The logic clock can remain
near NTSC speed by running catch-up ticks while presentation falls behind; see
the benchmark contract below.

## Architecture and frame flow

The engine, NMI order, PPU memory, game logic, and APU register stream are
shared with SDL. DOS replaces presentation, input, asset packaging, and PCM:

```text
engine + common PPU/VRAM flush
                |
        DosRenderFrame snapshot
                |
     dos_renderer_prepare()       normal RAM
                |
       DosRenderResult jobs
                |
 platform_dos.c + dos_blit.S      VGA aperture
```

`DosRenderFrame` contains both nametables and attributes, palette RAM, latched
OAM, scroll and nametable selection, screen-enable state, sprite-0 HUD split,
and a nametable generation counter. Rendering never changes game state.

Presentation is double buffered with one frame of latency:

1. `platform_wait_frame()` stages the CRTC address, waits for vertical
   retrace, commits fine pel pan, and swaps visible/back pages.
2. The shared loop advances one NMI and produces the next state.
3. `platform_render_begin()` snapshots PPU/OAM and prepares dirty work in RAM.
4. `platform_render_end()` uploads it to the hidden VGA page.

Staging CRTC during active display and changing the immediately effective
Attribute Controller pel pan at retrace prevents the two values from describing
different frames. The former ordering caused colored flashes and tearing on
ET4000-class VGA.

## Mode X layout

The host derives unchained 256x240 Mode X at approximately 60 Hz from BIOS mode
13h. Resolved NES color indices occupy separate 32-entry DAC banks for the two
pages, so the inactive page's palette can be changed safely.

Each scanline stores a 264-pixel ring twice:

```text
logical ring        264 pixels (33 tiles)
physical scanline   528 pixels (two aliases)
per-plane pitch     132 bytes
page size           31,680 bytes per plane
two pages           63,360 of each plane's 65,536 bytes
```

The 256-pixel window moves around the ring. Coarse motion changes the CRTC start
address and the remaining zero-to-three pixels use pel pan. Every dirty tile is
written to both aliases, so ring wrap never needs a full-page copy.

## Persistent renderer

VGA is the persistent playfield, not the target of a full software framebuffer.
Each page owns background descriptors, camera/ring state, sprite footprints,
and generation metadata. At startup all CHR tiles and planar background
palette/page variants are predecoded.

Per frame, the renderer:

- marks entering columns when the camera crosses tile boundaries;
- rescans descriptors only after nametable generation changes;
- dirties the union of old and new sprite footprints;
- applies NES eight-sprites-per-scanline and OAM-priority rules;
- recomposes every dirty 8x8 cell completely in RAM;
- emits complete tile jobs and sparse HUD spans.

The 8x8 cell is the coherence unit. Earlier per-byte shadows and partial sprite
restoration could miss one plane and leave background or sprite trails even
when write-only readback passed. Whole-cell writes make restoration atomic
while retaining tile-level dirty tracking.

Upload is plane-major: select each VGA plane once, write all tile jobs, then
all HUD spans. The small cdecl i386 kernel `dos_direct_tile` writes one plane of
an 8x8 tile to both aliases; `dos_copy_span` handles the HUD. Keeping only these
predictable stores in assembly makes their access pattern explicit. The RAM
compositor remains C: DJGPP `-O2 -mtune=i486` already produces reasonable code
for these loops. Equal-work profiling showed useful but small compositor gains
from the final C-side changes; the remaining end-to-end cost can instead be
dominated by VGA aperture writes on a slow adapter/bus combination. Rewriting
the whole renderer in assembly would add substantial maintenance cost without
removing those writes or their wait states.

## HUD and sprite-0 split

SMB fixes the top 32 lines while the playfield scrolls. VGA Line Compare cannot
directly implement top-fixed/bottom-scrolling output, and a scanline "copper"
would waste too much time polling port 3DAh.

The renderer instead composes the HUD in screen coordinates, caches four
fine-pan phases, tracks content generations, keeps per-page/per-plane shadows,
and coalesces changed bytes into spans. Sprites crossing the HUD use the same
priority and scanline-limit rules as the playfield. This avoids rebuilding and
uploading the full HUD on every scrolling frame.

## Sound Blaster audio

The shared sound engine feeds NES register writes into Nes_Snd_Emu. DOS
synthesizes at 22,420 Hz to remain slightly ahead of DSP time constant 211
(about 22,222 Hz); FIFO feedback sheds the small surplus. Alternate frames
advance 29,780/29,781 clocks at the 1,789,773 Hz NTSC CPU rate.

The sink parses `BLASTER` (default `A220 I5 D1`), allocates conventional memory
without crossing a 64 KiB DMA boundary, and uses four 512-byte blocks of 8-bit
auto-init DMA. The IRQ counts completed blocks; foreground service refills
released blocks from an 8 KiB FIFO. It waits while another block remains
playable rather than prematurely inserting silence. This replaced the original
one-command-per-frame path that produced frequent underruns during scrolling.

If Sound Blaster detection fails, the game continues silently. DMC sample ROM
is a flat reader because SMB does not use PRG DPCM data. Benchmarks report
underruns, missing samples, overruns, DMA blocks, and FIFO occupancy.

## Input, assets, and shutdown

An IRQ 1 handler records make/break scancodes so simultaneous controls work.
X/keypad-0 is A, Z/keypad-dot is B, Enter is Start, Backspace is Select, arrows
are the D-pad. Player 2 uses W/A/S/D for the D-pad, N/M for A/B, and K/J for
Start/Select. Escape quits. Shutdown restores keyboard and Sound Blaster
vectors/PIC masks, releases DMA memory, restores text mode, and disables DJGPP
near pointers.

`tools/pack_assets.py` validates each generated file against `manifest.json`
and writes only manifested payloads to `ASSETS.DAT`; stale files are excluded.
Internal keys remain paths such as `tiles.chr` and `areas/castle_1.bin`, so engine asset
lookups are shared. DOS searches the current directory and the executable's
directory; SDL can still use the unpacked tree.

## Optimization history

The final design came from these measured iterations:

1. **Full software framebuffer and planar upload:** correct but composition and
   a full-frame transfer dominated every frame.
2. **Persistent double-buffered pages:** removed full uploads; independent
   per-page residency prevented alternating stale/blank frames.
3. **CRTC plus pel-pan scrolling:** replaced framebuffer shifts with entering
   columns.
4. **Direct scattered VGA rendering:** looked cheap in simplified emulator
   models but amplified VGA-aperture access cost on period-style hardware.
5. **RAM preparation and plane-major batches:** restored locality and reduced
   Sequencer programming to four plane selections per flush.
6. **Mirrored 264-pixel ring:** removed periodic page rebase hitches while
   fitting two pages in standard Mode X memory.
7. **Whole-cell sprite restoration:** fixed trails caused by incomplete
   per-byte invalidation.
8. **HUD phase/content caches and sparse spans:** removed full fixed-bar uploads
   while scrolling.
9. **Retrace-safe display commit:** removed purple/black/green transient frames.
10. **Four-block auto-init audio DMA:** decoupled playback from variable render
    time and greatly reduced underruns.
11. **Blank-screen page warming:** keeps background residency current while the
    DAC displays only the backdrop, avoiding cold full-page work when the game
    enables the screen.
12. **Epoch-based dirty lists:** walks cells that changed instead of clearing
    and scanning all 990 ring cells every frame.
13. **Atomic uniform-cell upload:** empty and other plane-identical 8x8 cells
    select all four VGA maps and write once. This both prevents a partially
    restored clear cell and reduces its VGA bus traffic by 75 percent.
14. **Enabled-scene residency barrier:** after blank-screen warming, each
    physical page force-refreshes its playfield on its first enabled visit.
    This prevents a title/intermediate column from remaining resident behind
    cache keys that already describe the newly enabled scene.
15. **Cached PPU snapshots:** unchanged nametable and palette RAM are no longer
    copied into the DOS frame on every presentation.
16. **Preclassified planar tiles and row-level sprite lookup:** one 64-byte copy
    stages a dirty background cell, uniform-plane eligibility is precomputed,
    and sprite rows resolve their one or two destination jobs before visiting
    opaque pixels.

Rejected paths include repainting borders every frame, per-scanline HUD register
changes, palette tricks that flashed wrong colors, normal-path VGA readback,
sprite shortcuts that broke overlap/OAM semantics, and batched PCM draining.
The latter slightly reduced average audio work per logic tick but concentrated
it into larger deadline-breaking spikes, so per-NMI draining was restored.

The v7 row-level sprite and preclassified-tile changes did not materially move
the end-to-end FPS of the 486DX/33 configuration used for that experiment. They
are nevertheless retained because an equal-work comparison of 227 trace frames
reduced compositor time from 2.567 to 2.376 ms and dirty/sprite staging from
0.815 to 0.707 ms, while producing the same VGA work and passing the pixel-exact
renderer suite. The apparent whole-run reversal was caused by unequal catch-up
work, not slower rendering of the same frame. This result must not be generalized
to a different VGA adapter: unchanged upload bytes can take materially different
wall-clock time on another card or bus implementation.

## Benchmark and validation

`BENCH.EXE` defaults to 1,200 frames and writes `BENCH.TXT`, `FRAMES.CSV`, and
`VGA.PPM`. Use a hard disk for long runs because the floppy may fill.

```text
BENCH --frames 3600
BENCH --diagnostic --no-audio
BENCH --diagnostic --no-wait --no-audio
BENCH --diagnostic --no-upload --no-audio
BENCH --diagnostic --no-compositor --no-audio
BENCH --diagnostic --readback --no-audio
BENCH --profile-renderer --frames 3600
```

`--diagnostic` replaces gameplay with a deterministic scrolling scene.
`--no-wait` removes retrace pacing, `--no-upload` keeps RAM preparation but
skips VGA writes, and `--no-compositor` skips both preparation and upload.
Compare these modes on the same configuration to separate synchronization,
CPU composition, and VGA-aperture cost. Diagnostic startup also records
`probe_1MiB_seconds`, a simple one-mebibyte plane-write probe useful for
comparing VGA configurations; it is not a renderer throughput score.

`--readback` validates VGA writes but is deliberately too expensive for
performance measurement. `--profile-renderer` adds HUD content/diff and sprite
subtimers and also changes timing slightly. Inspect per-frame work,
compositor/upload/audio time, bytes, tiles, p95/p99/max, frames over 14 and
16.67 ms, and audio underruns. Average FPS alone hides scrolling and transition
spikes.

The normal benchmark is presentation-count bounded, not logic-tick bounded.
Logic follows wall-clock NTSC time: whenever presentation slows, the next loop
runs extra catch-up ticks. A slower 1,200-presentation run can therefore advance
farther into the demo, process more sprites/tiles, and make itself still slower.
Compare builds using identical-work trace rows or a fixed input and logic-frame
interval; do not rank them solely by `game_fps`, `logic_ticks`, or aggregate
`tiles_plotted` from separate wall-clock runs. `--profile-renderer` also adds
timer calls and is intentionally more expensive than `SMB2.EXE`. Record
`retrace_hz` alongside `game_fps`: a retrace-limited run near 58 Hz cannot report
60 presentations per second even when it meets every display deadline.

`make test-dos-renderer` compares the production renderer with the shared
software reference across 2,200 synthetic frames, including random
nametable/palette/OAM changes, reverse
scroll, ring wrap, HUD split, sprite priority, and screen disable. This proves
snapshot rendering equivalence, not physical scanout timing. DOSBox is useful
for functional regressions. 86Box is the visual/performance proxy only when its
CPU, bus, and VGA configuration are recorded; changing the VGA model changes
the tested machine.
