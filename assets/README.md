# Extracted assets

The playable program never opens an iNES ROM. A legitimate Super Mario Bros.
(W) [!] dump is used once:

```
make extract ROM=/path/to/Super\ Mario\ Bros.\ \(W\)\ \[\!\].nes
```

or `make sdl ROM=...` if `assets/` is missing.

Accepted SHA-1: `ea343f4e445a9050d4b4fbac2c77d0693b1d0922`

Generated files under `areas/`, `enemies/`, `audio/`, `tables/`,
`compat-rom-windows/`, plus `tiles.chr` and `manifest.json`, are not shipped in
git. The playable binary will use the extracted assets instead of the original ROM.

`tables/` contains authored lookup data, including lookup tables whose names
contain `Offset`. Structural offsets that can be derived from record layout
are not emitted. `compat-rom-windows/` is deliberately separate: those bytes
preserve observable 6502 reads beyond a logical table boundary and may include
adjacent instruction bytes. `manifest.json` records category, source label,
ROM/CHR offset, length, extraction method, and SHA-256 for every generated
payload.
