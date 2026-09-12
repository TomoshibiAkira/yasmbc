# SDL 1.2 / Windows 98 frontend

The SDL 1.2 frontend is an x86-only compatibility build for Win9x-class systems. It
shares the engine, PPU memory model, VRAM flush, assets, and software
compositor with the SDL2 build. Only the host adapter is different:

- `system/sdl12/platform_sdl12.c` uses `SDL_SetVideoMode` and a plain
  `SDL_Surface`; it does not use SDL2 textures, renderers, or window objects.
- The canonical 256x240 palette-index framebuffer is expanded with integer
  nearest-neighbour scaling (2x by default) directly into the surface.
- An 8-bit surface is requested first, so the NES palette can be installed as
  an SDL logical/physical palette. A desktop format is accepted as a fallback
  and is filled through a small 64-entry RGB lookup table.
- Keyboard state is sampled through SDL 1.2's event and key-state APIs using
  the same controls as the other frontends.
- Audio uses the same Nes_Snd_Emu 2A03 core as SDL2, with an SDL 1.2 callback
  feeding a bounded mono PCM ring. The callback is prebuffered before playback
  so foreground scrolling work does not immediately produce an underrun.

## Building

The target is deliberately opt-in because SDL 1.2 development files and a
Win98-capable compiler are not normally installed on modern systems:

```sh
make sdl12-release \
  SDL12_CC=/opt/mingw32/bin/i386-pc-mingw32-gcc \
  SDL12_CXX=/opt/mingw32/bin/i386-pc-mingw32-g++ \
  SDL12_PREFIX=/opt/SDL-1.2.15/mingw32
```

`SDL12_PREFIX` must contain `include/SDL/SDL.h` and the matching x86 import
libraries (`libSDL.a`/`libSDL.dll.a` and `libSDLmain.a`). The output is
`smb2-sdl12-release-x86.exe`. Build with an older MinGW/MinGW32 or VC6-era
toolchain when the executable must run on Windows 98; a current MinGW-w64
runtime is not a Win98 compatibility guarantee.

Ship the executable with the matching 32-bit `SDL.dll` and the extracted
`assets/` directory. Normal interactive runs also require an SDL1.2-compatible
audio device; if opening it fails, the game remains playable but audio is
silenced.

The tree includes `system/sdl12/win98_gthr_compat.c`. It supplies the small
critical-section subset needed by the statically linked C++ audio runtime, so
GCC 13's `gthr-win32.o` (and its XP-only `GetThreadId` import) is not pulled
into the SDL1.2 executable. This is a link-time compatibility measure; the
SDL DLL and any replacement CRT must still come from a Win9x-capable package.

## Runtime controls

`SMB_SDL12_SCALE=1..4` selects integer scaling (default `2`). Set
`SMB_SDL12_FULLSCREEN=1` to request fullscreen mode. Headless runs use the
same command-line options as SDL2 and do not initialize SDL video, which keeps
state/frame-stream verification independent of the compatibility frontend.

## Video performance

The frontend presents a 256x240 palette-index framebuffer through an SDL 1.2
software surface. At the default 2x scale, each displayed frame expands this
to a 512x480 8-bit surface and then relies on the Win98 SDL video backend and
driver to display it. That makes effective surface/flip bandwidth at least as
important as host CPU frequency.

This distinction was visible on the tested Pentium II system: the game could
not sustain full speed with an S3 ViRGE PCI card, but the same build and CPU
ran at full speed after replacing it with an ATI Mach64. The earlier apparent
CPU limit was therefore a graphics-card/driver presentation limit. This is a
configuration result, not a claim that every ViRGE or Mach64 revision behaves
identically.

When diagnosing another period machine, first check the display adapter and
Win98 driver, and compare `SMB_SDL12_SCALE=1` with the default scale 2. Scale 1
substantially reduces the expanded surface traffic; a large improvement points
to presentation rather than game logic or APU synthesis. The startup message
also reports the actual surface depth: the requested 8-bit indexed path is the
cheapest, while 16/32-bit fallbacks require palette conversion and more bytes
per frame.

The release build uses `-O3`, `-march=pentium2`, `-mtune=pentium2`, LTO, and
omits frame pointers. Its background compositor keeps a persistent 256x240
index/mask image: a camera step shifts that image and only the exposed strip
and PPU dirty-tile list are redrawn. Sprite composition still runs every frame,
so priority and transparency do not depend on the cache.

The default release is tuned for a Pentium II-class target. Toolchains too old
to provide LTO can build with `SDL12_LTO=0`; the generated code remains ordinary
32-bit x86 and does not require SSE.

## Audio performance

The SDL1.2 adapter synthesizes at 22,050 Hz, signed 16-bit mono. A 32,768-sample
ring is drained by SDL's callback and filled once per game frame. By default it
uses the cheaper linear mono mixer; this preserves the 2A03 channel timing and
register behavior while leaving more CPU headroom than the nonlinear pulse/TND
pass. Set `SMB_SDL12_AUDIO_HIFI=1` to select nonlinear mixing for listening
comparisons. The SDL2 build retains its 48 kHz path. On the tested machine, the
decisive full-speed difference was the video adapter rather than this mixer;
the cheaper path remains useful as protection against audio underruns during
presentation spikes.

## Scope and limitations

SDL 1.2 is an old, deprecated branch; freeze the SDL DLL and compiler runtime
with the build. This backend is intended to make the current software
renderer/input path usable on Win98, not to emulate NES PPU timing or to
guarantee 60 FPS on every period CPU/display-card/driver combination. The
current binary is linked with a modern cross-toolchain plus the documented
Win98 gthread compatibility shim; re-audit its PE imports if the compiler,
runtime, or SDL DLL is replaced.
