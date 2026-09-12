# Yet Another Super Mario Bros. C port

A C reimplementation of *Super Mario Bros.* (1985) for PC. Game
logic is translated from the original 6502 program. Video and audio go
through a host layer: software PPU-style rendering, and APU register writes
fed to [Blargg's Nes_Snd_Emu](https://github.com/blarggs-audio-libraries/Nes_Snd_Emu).

This tree is the playable program. It does not contain Nintendo ROM or CHR
data. You can extract those from a dump you already own with provided
tool script. The running binaries never open the original ROM; the dump is
only an asset-extraction source, and this tree no longer builds a NES ROM.

If you wish to dive into the code, [walkthrough.md](docs/walkthrough.md) is 
a good starting point.

## Requirements

- GCC and G++
- GNU Make
- Python 3 (asset extraction only)
- SDL2 development files (for Linux/SDL build)
- An i586 MinGW-compatible x86 compiler (for the native Win95 build)
- DJGPP (for DOS VGA build)
- MinGW (for Windows build)

On Debian/Ubuntu:

```bash
sudo apt install build-essential python3 libsdl2-dev
```

For a Windows `.exe` from Linux, also:

```bash
sudo apt install mingw-w64 libsdl2-mingw-w64-dev
```

## Extract assets

Use a Super Mario Bros. (W) [!] iNES dump whose SHA-1 is

`ea343f4e445a9050d4b4fbac2c77d0693b1d0922`

```bash
make extract ROM=/path/to/Super\ Mario\ Bros.\ \(W\)\ \[\!\].nes
```

`make sdl`, `make sdl-release`, and `make win95-release` run extraction
automatically when `assets/` is missing, using `ROM=` or the `SMB_ROM`
environment variable.

Generated files (`assets/areas/`, `enemies/`, `audio/`, `tables/`,
`tiles.chr`) are not part of git. See [assets/README.md](assets/README.md).

## Build and play (Linux)

From this directory:

```bash
make sdl-release
./smb2-release
```

| Target | Output | Notes |
|--------|--------|--------|
| `make` / `make sdl` | `smb2` | Debug (`-g -O0`). Headless extras enabled. |
| `make sdl-release` | `smb2-release` | Optimized. This is the build meant for playing. |
| `make win95-release` | `smb2-win95-x86.exe` | Native Win32/GDI + WinMM Win9x frontend; no SDL dependency. |
| `make extract` | `assets/` | ROM required; see above. |
| `make clean` | | Removes objects and binaries, keeps `assets/`. |

A debug `smb2` with **no arguments** does not open a window. It runs 1800
headless frames and writes PPMs under `test_output/` (a development default).
Pass flags for a bounded headless run, or use the release binary to play.

```bash
make sdl
./smb2 --headless --frames 60
```

### Controls

| NES | Keyboard |
|-----|----------|
| D-pad | Arrow keys |
| A | X or keypad 0 |
| B | Z or keypad `.` |
| Start | Enter |
| Select | Backspace |
| Quit | Esc |

## Headless and movies

```bash
./smb2-release --help
./smb2 --headless --frames 300
./smb2 --headless --tas path/to/movie.fm2 --frames 100
```

Useful flags (debug builds unless noted):

- `--headless` — no window, no frame pacing
- `--tas PATH` — replay a text FM2 v3 power-on movie (implies headless)
- `--frames N` — stop after N host frames
- `--save-frames` — write `test_output/frame_XXXX.ppm` (debug only)

`--dump-frame-stream`, `--dump-state-stream`, and `--nmi-inputs` are for
external comparison tools, not required to play.

## TAS verification boundary

The release acceptance criterion is the canonical gameplay state sampled at
each SMB NMI entry, plus the ordered APU register/state stream. The current
checkpoint reaches the end of all 13 corpus movies with zero gameplay-RAM and
APU differences. The strict palette-index framebuffer comparison remains a
secondary presentation diagnostic.

The port deliberately does not emulate cycle- or scanline-accurate PPU
latches. Consequently, a small set of reviewed framebuffer differences is
expected at the NES sprite-0 HUD split and at mid-frame scroll-latch updates;
these can appear as a shifted playfield or a different dynamic-sprite cadence
while gameplay RAM and APU state remain exact. 

## Windows `.exe` (MinGW, from Linux)

Extract assets with the Unix Makefile first, then cross-compile:

```bash
make extract ROM=/path/to/Super\ Mario\ Bros.\ \(W\)\ \[\!\].nes
make mingw64-release        # smb2-release-x64.exe
# or:
make mingw64                # smb2-x64.exe (debug)
make mingw32                # smb2-x86.exe (debug)
make mingw32-release        # smb2-release-x86.exe
```

Equivalent:

```bash
make -f Makefile.mingw release
CROSS=i686-w64-mingw32- make -f Makefile.mingw          # x86 debug
# 32-bit release:
CROSS=i686-w64-mingw32- make -f Makefile.mingw release
```

`make mingw32` needs the complete **32-bit MinGW SDL2 development bundle**,
not just the i686 compiler: `SDL2/SDL.h`, `libSDL2.dll.a` (or `libSDL2.a`),
and `libSDL2main.a` for the i686 target. Install the i686 MinGW toolchain
(`gcc-mingw-w64-i686`, `g++-mingw-w64-i686`, and binutils) and unpack the
matching SDL2 MinGW developer archive into a prefix such as
`/usr/i686-w64-mingw32` (or pass that prefix as `SDL2_PREFIX=`). The SDL2 DLL
copied beside the resulting executable must be the same 32-bit architecture.

Copy onto a Windows machine:

- `smb2-release-x64.exe` (or `smb2-x64.exe`) for 64-bit Windows
- `smb2-release-x86.exe` (or `smb2-x86.exe`) for 32-bit Windows
- the `assets/` directory next to the exe
- `SDL2.dll` from the MinGW SDL2 package (same architecture as the exe)

`Makefile.mingw` selects `/usr/x86_64-w64-mingw32` for x64 and
`/usr/i686-w64-mingw32` for x86 by default. Override with `SDL2_PREFIX=` if
yours is elsewhere.

## DOS VGA (protected mode)

This is a playable extra, not the TAS verification host. It needs DJGPP
(`i586-pc-msdosdjgpp-gcc`) and a VGA 386-class machine or emulator.

`make dos` downloads CWSDPMI 7 from delorie.com into `third_party/cwsdpmi/`
on first run (needs network). Override the toolchain with `DJGPP_PREFIX=`.

```bash
make extract ROM=/path/to/Super\ Mario\ Bros.\ \(W\)\ \[\!\].nes
make dos
```

Ship the `dosdist/` folder:

| File | Role |
|------|------|
| `SMB2.EXE` | Game (DPMI) |
| `ASSETS.DAT` | Packed `assets/` (8.3 name; internal paths unchanged) |
| `CWSDPMI.EXE` | DPMI host, if your stub does not embed one |

Compatibility floor: **386 instruction set**, 4 MB RAM, VGA. This is not a
playability or full-speed guarantee. DOS performance is a property of the
whole CPU/chipset/bus/VGA configuration; there is no reliable CPU-only minimum,
and scrolling is especially sensitive to VGA aperture write throughput.
Sound uses a Sound Blaster at ~22 kHz if `BLASTER=` is set (for example
`BLASTER=A220 I5 D1 T4`). No card means silence.

Video is unchained Mode X with a **256×240 active display**, black hardware
overscan, and a mirrored ring for scrolling. Keyboard map matches the SDL port
(arrows, X/Z, Enter, Backspace, Esc).

Test proxy: 86Box, VGA, and Sound Blaster / Dosbox.

All implementation details, optimization history, performance limits, and
validation commands are consolidated in [docs/dos.md](docs/dos.md). Build the
data floppy with `make dos-floppy`. `BENCH --diagnostic --no-audio` isolates
video from game logic; add `--readback` to check VGA writes (not to measure
speed).
For long runs such as `BENCH --frames 3600`, copy the files to a DOS hard
disk so `FRAMES.CSV`, `BENCH.TXT` and `VGA.PPM` have sufficient free space.
Historical 86Box configurations measured roughly 58 presented FPS on a
486DX/66 setup (at a measured 58 Hz retrace) and around 40--42 FPS on a
486DX/33 setup, while game logic remained near NTSC speed through catch-up.
Those are complete-configuration measurements, not a processor scaling curve.
See the DOS platform document for hardware-reporting requirements and the
benchmark comparison caveat.

## Native Win32 / Windows 95 frontend

The `win95` target is the preferred dependency-free legacy Windows build. It
uses GDI for the indexed framebuffer, WinMM `waveOut` for audio, and
`GetAsyncKeyState` for independent multi-key input. Build it with an i586
MinGW-compatible compiler:

```bash
make win95-release \
  WIN95_CC=/opt/mingw32/bin/i586-mingw32msvc-gcc \
  WIN95_CXX=/opt/mingw32/bin/i586-mingw32msvc-g++ \
  WIN95_OBJDUMP=/opt/mingw32/bin/i586-mingw32msvc-objdump
```

This produces `smb2-win95-x86.exe`; copy it with `assets/` and no SDL runtime.
`SMB_WIN95_SCALE=1..4` controls integer scaling. Set
`SMB_WIN95_AUDIO_RATE=44100` for 44.1 kHz output; the default is 22.05 kHz.
The four-buffer native WaveOut queue is tuned for Win95/K6-class systems. See
[docs/win95.md](docs/win95.md) for the API, queue, and compatibility details.

## ANSI terminal frontend

Build and run the silent terminal version on a UTF-8 terminal with ANSI
true-color support:

```bash
make terminal
./smb2-terminal
```

Game logic runs at the NTSC rate while text presentation is capped at 30 FPS.
The 128×60 upper-half-block display represents an effective 128×120 color
resolution. A terminal at least 256 columns by 113 rows automatically uses
256×112 cells, preserving the complete 256×224 NES pixel resolution; the final
row is reserved for the input display.
The renderer uses colored Unicode upper-half blocks and selects 128×60 when the
terminal is at least 128 columns by 61 rows; smaller windows use 64×30. Controls
are arrows, X/Z, Enter, Backspace, and Q to quit. Since terminals have no key-up
notification, arrows toggle held directions and Z toggles B (run). X performs a
full-height jump and C performs a short jump; neither needs a release press.
Space clears held directions.

## Layout

```
engine/           translated game (NMI, player, level, enemies, audio, …)
system/common/    host-side PPU memory shared by SDL, Win95, DOS, and terminal
system/sdl/       SDL video and audio host
system/win95/     Native Win9x GDI/WinMM video/input/audio host (no SDL)
system/dos/       Mode X, Sound Blaster, and DOS benchmark host
system/           platform-neutral interfaces, FM2, state streams
constants/        shared types and RAM-named globals
assets/           generated payloads (not in git) plus README
tools/extract_assets.py
third_party/nes_snd_emu/   Nes_Snd_Emu 0.1.7 (LGPL-2.1)
docs/             walkthrough, audio, DOS host
```

## Special Thanks

* smb1-disasm: https://github.com/pgattic/smb1-disasm
* SMBDIS.ASM: https://gist.github.com/1wErt3r/4048722

## License notes & Legal disclaimer

Nes_Snd_Emu is © Shay Green and is licensed under the GNU LGPL 2.1
(`third_party/nes_snd_emu/LICENSE`).

This repository does not include Nintendo's ROM, graphics, or music,
and has no affiliation with Nintendo.
