# Nes_Snd_Emu provenance

This directory contains the core files required from
`blarggs-audio-libraries/Nes_Snd_Emu` commit
`3badd244a0dd62a9f1b7fc2a0a6cac35c4491f83`.

Copyright (C) 2003-2005 Shay Green. Licensed under LGPL-2.1; see `LICENSE`
and `LGPL.txt`. The SMB2 project uses the library only in the SDL host build.
The NES backend writes the hardware APU registers directly.

Local portability change: `nes_apu/blargg_common.h` replaces Boost's
fixed-width integer aliases and static-assert macro with their C++11 standard
library equivalents. No synthesis or timing logic is changed.
