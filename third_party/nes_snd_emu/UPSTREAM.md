# Nes_Snd_Emu provenance

This directory contains the core files required from
`blarggs-audio-libraries/Nes_Snd_Emu` commit
`3badd244a0dd62a9f1b7fc2a0a6cac35c4491f83`.

Copyright (C) 2003-2005 Shay Green. Licensed under LGPL-2.1; see `LICENSE`
and `LGPL.txt`. The project uses the library in the SDL2, native Win95, and
DOS audio backends. Each backend owns its PCM buffering and device interface;
the shared game engine supplies the same APU register stream.

Local portability change: `nes_apu/blargg_common.h` replaces Boost's
fixed-width integer aliases and static-assert macro with their C++11 standard
library equivalents. No synthesis or timing logic is changed.
