# Audio implementation

## Design boundary

Audio follows the same hardware-abstraction goal as video. SMB's game-specific
sound program is translated from the 6502 reference, while synthesis is a
platform service:

```text
gameplay queues
    -> Audio_SoundEngine (ASM translation)
    -> platform_apu_write(address, value)
       -> SDL: Nes_Snd_Emu 2A03 renderer -> SDL queued mono PCM
       -> Win95: Nes_Snd_Emu 2A03 renderer -> WinMM waveOut ring PCM
       -> DOS: Nes_Snd_Emu 2A03 renderer -> Sound Blaster DMA ring
```

The gameplay code does not generate PCM, emulate CPU instructions, or depend on
a host. Host backends do not understand SMB songs or effects. This keeps the
portable game logic at the APU-register boundary while allowing each host
backend to choose its own synthesis, output format, and buffering policy.

## Source of truth

`engine/audio.c` translates `SoundEngine` in `../../smb1-disasm/main.asm` and
`MusicHandler` in `../../smb1-disasm/audio/engine.asm`. It preserves channel
priority, queue consumption, note/envelope counters, music loopback, pause
mode, and register-write order. Music bytes are loaded from
`assets/audio/music_data.bin` (CPU `$f90d–$fff9`). This is the contiguous
`audio/music.asm` data region: music headers/streams and lookup/envelope
tables. The interrupt vectors at `$fffa–$ffff` are intentionally excluded.

The host renderers use Shay Green's Nes_Snd_Emu 0.1.7 at upstream commit
`3badd244a0dd62a9f1b7fc2a0a6cac35c4491f83`, vendored under
`third_party/nes_snd_emu`. Its LGPL-2.1 license and provenance are retained.
The old core assumes a 32-bit `long` when its default buffer length is used;
`system/sdl/apu_sdl.cpp` therefore requests an explicit 100 ms buffer on 64-bit
hosts.

SDL2 output is NTSC (`1,789,773` CPU clocks), 48 kHz, signed 16-bit mono, using
the core's nonlinear pulse/TND mixer. Each video frame advances 29,780 or
29,781 clocks alternately. Since the C translation has no CPU instruction
clock, writes within an NMI use monotonically increasing four-cycle timestamps;
this preserves ordering and timer-reset edges without introducing a CPU
emulator into game logic.

Interactive pacing uses the same 29,780.5-clock NTSC field duration through
SDL's high-resolution performance counter. Audio playback starts after a
50 ms queue prebuffer. The queue is never wholesale-cleared during playback:
an earlier 16 ms integer frame timer ran at 62.5 Hz, made queued audio grow,
and periodically cleared it at 100 ms, producing audible note corruption and
dropouts.

## Verification

Set `SMB_AUDIO_TRACE` to record candidate writes as TSV:

```bash
SMB_AUDIO_TRACE=/tmp/candidate.tsv ./smb2 --headless \
  --tas path/to/movie.fm2 --frames 1000
```

An external NES reference runner can emit the same ordered APU trace for a
movie, and a separate comparison tool can compare the two traces. This is an
opt-in regression check; ordinary framebuffer, RAM, and input playback remain
unchanged.

The comparison is per SoundEngine NMI ordinal. The candidate consumes the
reference's NMI-sampled controller bytes (`--nmi-inputs`), not FM2 video
records, so lag frames cannot shift later jump/stomp/fireball queue edges.
Audio state remains excluded from the canonical gameplay RAM schema so sound
cannot make a TAS gameplay pass or fail.

The first 2,200 frames of `smb-0.fm2` are register-exact (13,362 APU writes).
An earlier frame-2166 divergence was traced to `Collision_CheckHead`: the C
port queued `Sfx_Bump` before distinguishing `BumpBlock` from the big-Mario
`BrickShatter` path. The queue write now occurs only in `BumpBlock`, matching
the ASM behavior; `BrickShatter` queues only its noise-channel effect.

## DOS backend

DOS shares the register-level APU adapter but synthesizes at 22,420 Hz and
feeds a four-block, 8-bit Sound Blaster auto-init DMA ring through an 8 KiB
FIFO. The IRQ marks completed 512-byte blocks and foreground service refills
them. This isolates playback from scrolling-time render jitter. Hardware setup,
underrun accounting, and the reasons for the chosen rate are documented in
[the consolidated DOS platform document](dos.md).

The ANSI terminal backend is intentionally silent.

## Build and runtime

`make sdl` builds the C game, the C++ APU adapter, and the vendored core. Normal
interactive SDL runs open an audio device; headless runs advance the APU and
can emit deterministic traces without opening one. Use the common command-line
options `--audio-rate 22050|44100|48000` and `--audio-hifi` on SDL and Win95;
SDL defaults to 48,000 Hz and always uses its nonlinear mixer, while Win95
defaults to 22,050 Hz with its lower-cost linear mixer. The native Win95
frontend uses WinMM `waveOut` with a four-buffer queue; `--audio-rate 44100`
selects 44,100 Hz and `--audio-hifi` restores nonlinear mixing for listening
comparisons. Period-hardware testing found the PCI video
presentation path, not APU synthesis, to be the decisive full-speed bottleneck
in the tested configuration. Headless runs still avoid opening an audio
device. The NES/FCEUX side remains the external reference used to specify and
verify the translated game; this tree does not produce a NES ROM.

Current limitations:

- DMC samples are not used by SMB; the renderer supplies a flat DMC reader.
- The SDL backend models frame-level, not instruction-cycle-level, write time.
- Coverage is only as complete as gameplay sound-queue producers. The audio
  oracle deliberately exposes missing or extra producers instead of hiding
  them in the renderer.
