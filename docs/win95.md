# Native Win32 / Windows 95 frontend

The native frontend is an x86-only build for Windows 95/98-class systems. It
has no SDL runtime dependency:

- GDI `CreateDIBSection` and `StretchDIBits` present the 256x240 indexed image.
- `GetAsyncKeyState` samples all controller buttons independently, so holding
  X and a direction works without SDL keyboard-state/event limitations.
- `timeGetTime`, `timeBeginPeriod`, and `Sleep` provide the 60 Hz host pacing.
- WinMM `waveOut` owns four PCM buffers. The audio callback only marks a
  completed `WAVEHDR`; the game thread performs all APU synthesis and buffer
  refills.

The game and software compositor are shared with the other PC frontends. The
native adapter is in `system/win95/`, while the APU core remains the same
`Nes_Snd_Emu` implementation used by the SDL2 and DOS hosts.

## Build

Use an i586 MinGW.org-compatible compiler/runtime. The build deliberately uses
`-march=pentium -mtune=pentium` and links only Win95-era system libraries:

```sh
make win95-release \
  WIN95_CC=/opt/mingw32/bin/i586-mingw32msvc-gcc \
  WIN95_CXX=/opt/mingw32/bin/i586-mingw32msvc-g++ \
  WIN95_OBJDUMP=/opt/mingw32/bin/i586-mingw32msvc-objdump
```

The result is `smb2-win95-x86.exe`. Copy it beside the extracted `assets/`
directory. No SDL runtime is required. `SMB_WIN95_SCALE=1..4` selects integer
window scaling (default 2); headless runs retain the normal `--headless` and
`--frames` options.

`make win95-release` runs `win95-audit`, which rejects CMOV/MMX/SSE-family
instructions, Windows XP synchronization imports, and accidental SDL imports.
The default native build uses the legacy compiler's own Win9x-compatible
gthread implementation. Modern mingw-w64 runtimes are not supported here,
because their thread support may import APIs that do not exist on Win9x.

## Audio design

The default output is 22,050 Hz, signed 16-bit mono. Select the optional
44,100 Hz mode with `SMB_WIN95_AUDIO_RATE=44100`; any other nonempty value is
rejected and falls back to 22,050 Hz. Four 1024-sample `WAVEHDR`s are kept in
flight, giving about 186 ms of device-side scheduling margin at 22,050 Hz (or
93 ms at 44,100 Hz).
The APU producer keeps a 32,768-sample ring and starts playback after one full
four-buffer queue has been prepared. Set `SMB_WIN95_AUDIO_HIFI=1` only for
listening comparisons; the default linear mixer leaves more CPU headroom on
K6/Pentium systems.

This is not cycle-accurate NES audio hardware emulation; it preserves the same
APU register/state contract as the other PC builds. A hardware driver may still
underrun if the host cannot sustain the game loop for longer than the queued
margin, but the queue depth and refill policy are now explicit and measurable
in the native source rather than hidden in SDL.

## Video trade-off

The native version intentionally uses GDI rather than DirectDraw. This keeps
the dependency-free baseline deterministic without claiming an automatic
frame-rate improvement. If a particular Mach64 or ViRGE driver benefits from a
hardware surface, DirectDraw can be added as a separate backend after the GDI
baseline is measured.
