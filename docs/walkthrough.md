# Code walkthrough

This is a reading-order guide to the game source tree. It explains how a frame
runs, where each system lives, and which file to open next. It is not a
line-by-line translation of the 6502 program.

Build and play notes are in [README.md](../README.md). Audio detail is in
[audio.md](audio.md). The DOS VGA host is in [dos.md](dos.md).

## The idea in one paragraph

The original Super Mario Bros. is a 6502 program that talks to NES hardware.
This tree splits that in two:

- **`engine/`** is the game: modes, player, levels, enemies, collision, HUD,
  and the sound *driver*. It is translated from the disassembly under
  `smb1-disasm/`.
- **`system/`** is the host: window, keyboard, software PPU, APU synthesis.
  SDL2, SDL 1.2, DOS, and terminal frontends all implement the same
  [`system/platform.h`](../system/platform.h) API.

Nintendo graphics, music, and level bytes are not in git. You extract them
once into `assets/`. The running binary never opens a `.nes` file.

```text
assets/  (CHR, music, area streams, ROM tables)
    |
engine/  (translated SMB logic)
    |
platform.h
    +-- system/sdl/        PC window
    +-- system/sdl12/      Win98-compatible SDL 1.2 surface
    +-- system/dos/        VGA + Sound Blaster
    +-- system/terminal/   ANSI true-color
```

## Where to start

Open [`main.c`](../main.c). After argument parsing it calls `Assets_Init()`
through the platform, then enters a loop that, each host frame, does two
things:

1. **`game_tick`** — run one NMI-equivalent (`NMI_Tick` in
   [`engine/nmi.c`](../engine/nmi.c)), then advance host audio.
2. **`game_render`** — `platform_render_begin()` then `platform_render_end()`.

NMIs do not start on video frame 0. `NMI_FIRST_VIDEO_FRAME` is 5, matching
the NES/FCEUX startup warmup. Video frames 0–4 are blank-ish backdrop only.

A debug `./smb2` with no arguments is headless: 1800 frames and PPM dumps.
Play with `make sdl-release` and `./smb2-release`.

## One host frame

On real hardware the PPU is already drawing while the CPU runs the NMI.
This port models that with **latches**. At the *start* of `NMI_Tick` the
engine copies the current OAM, scroll, nametable bit, and screen-enable flag
into `g_RenderOAM`, `g_RenderScrollX`, `g_RenderNT`, and
`g_RenderEnabledLatch`. Game logic then runs and writes *next* frame’s
sprites and VRAM. `game_render` composites from those latches, so the picture
you see is the state sampled at NMI start — the previous NMI’s finished
work — not a mid-logic snapshot.

Do not reorder this into “flush VRAM, draw, then run OperMode.” The flush
belongs inside the NMI, before the sound engine and before `OperMode_Tasks`.

```text
platform_wait_frame
        |
        v
   NMI_Tick          joypad, latch picture, flush VRAM, sound,
                     timers, OperMode
        |
        v
platform_audio_frame
        |
        v
platform_render_*    nametable + sprites from the latches
```

`g_DisableScreenFlag` is the NES “show backdrop only” gate. While it is set,
the host clears to palette `$3F00` and skips nametable and sprites. That is
why the title sequence is gray, then black, then blue, then the logo — not a
special-case title renderer.

Expected title colors (debug PPMs, matching NES):

| Frames | Backdrop | Why |
|--------|----------|-----|
| 0–2 | gray `#626262` | PPU power-on palette `$00` |
| 3–8 | black `#000000` | `SetupIntermediate` sets `BGColorCtrl = 2` (`$0F`) |
| 9–14 | light blue `#8287FF` | area + player palettes (`$22`) |
| 15+ | title visible | `PrimaryGameSetup` clears `g_DisableScreenFlag` |

## `NMI_Tick`: the real game clock

Almost everything interesting happens in [`engine/nmi.c`](../engine/nmi.c),
in this order (from `nmi.asm` `NonMaskableInterrupt`):

1. **Joypads.** `NMI_ReadJoypads` writes `g_SavedJoypadBits`. Select and
   Start are edge-triggered through `g_JoypadBitMask`. Skipped NMIs must not
   consume a movie record.
2. **Picture latches.** Copy sprite RAM (`g_SpriteData`, NES `$0200`) and
   scroll/screen flags for the compositor.
3. **`platform_vram_flush`.** Drain the NES-format VRAM buffer into PPU
   memory ([`system/vram_flush.c`](../system/vram_flush.c)).
4. **`Level_FlushDeferred`.** Extra nametable writes queued by the area
   parser.
5. **`Audio_SoundEngine`.** Game-side APU register writes. Synthesis is a
   host problem; see [audio.md](audio.md).
6. **Pause routine**, then top-score update.
7. If unpaused: **decrement timers**, increment `g_FrameCounter`.
8. **PRNG** (seven-byte rotate chain in `g_PseudoRandomBitReg`).
9. If sprite-0 hit detection is on and unpaused: hide OAM slots 1+, shuffle
   sprite offsets.
10. If unpaused: **`OperMode_Tasks()`**.

Long routines (`InitializeGame`, `InitializeArea`) set `g_NMIBusy` so the
next one or two NMIs only keep the picture alive. That matches the original
CPU running past the NMI disable.

Globals that name this machine live in
[`constants/globals.h`](../constants/globals.h) / `globals.c`. Names follow
NES RAM (`g_OperMode`, `g_Player_X_Position`, …). Treat those names as the
contract with the disassembly, not as a second memory map you invent.

## Operating modes

[`engine/opermode.c`](../engine/opermode.c) is the top-level state machine.
`g_OperMode` chooses the mode; `g_OperMode_Task` is the step inside it.

| `g_OperMode` | Name | What it does |
|--------------|------|----------------|
| 0 | `TITLE_SCREEN_MODE` | Init, screen routines, primary setup, menu/demo |
| 1 | `GAME_MODE` | Init area, screen routines, secondary setup, `GameCoreRoutine` |
| 2 | `VICTORY_MODE` | Castle / world-end sequence ([`engine/victory.c`](../engine/victory.c)) |
| 3 | `GAME_OVER_MODE` | Continue / game-over |

Title and game mode share the same four-task shape:

```text
task 0  InitializeGame / InitializeArea   (sets g_NMIBusy)
task 1  ScreenRoutines until task 15
task 2  Primary/SecondaryGameSetup        (enables the screen)
task 3  menu demo  or  GameMode_MainLoop
```

The title menu and attract demo live in
[`engine/title-menu.c`](../engine/title-menu.c). Starting a game switches
`g_OperMode` to `GAME_MODE` and resets the task index.

## Screen routines (tasks 0–14)

While `g_OperMode_Task == 1`, [`engine/screen/screen.c`](../engine/screen/screen.c)
indexes `g_ScreenRoutineTask` into a jump table. Each routine does a little
work, then usually calls `Screen_IncTask()`.

| Task | Function | Role |
|------|----------|------|
| 0 | `InitScreen` | Clear sprites, fill nametable with tile `$24` |
| 1 | `SetupIntermediate` | World/level intro setup, backdrop |
| 2–3 | HUD status lines | “MARIO / WORLD / TIME” and the digits |
| 4 | `DisplayTimeUp` | TIME UP |
| 5, 7 | `ResetSpritesAndScreenTimer` | Clear sprites, wait |
| 6 | `DisplayIntermediate` | World-level card |
| 8 | `AreaParserTaskControl` | Parse enough of the area to fill the screen |
| 9–11 | Palettes | Area, player/BG, alternate |
| 12 | `DrawTitleScreen` | Copy title RLE into the VRAM buffer |
| 13 | `ClearBuffersDrawIcon` | Mushroom cursor |
| 14 | `WriteTopScore` | High score |

Files: `engine/screen/routine/{init,intermediate,hud,colors,title,area_parser}.c`.

### How tiles actually change

Game code almost never pokes the software PPU directly. It appends NES VRAM
buffer records to `g_VRAM_Buffer1` / `g_VRAM_Buffer2`:

```text
[addr_hi, addr_lo, control, data…]
control: D7 = vertical (+32), D6 = repeat, D5–D0 = length
terminated by $00
```

`g_VRAM_Buffer_AddrCtrl` picks the source (0 and 5 are buffer 1; 1–4 are
palette tables from assets; 6–7 are buffer 2). The NMI flush walks entries
until the terminator. On NES, the title RLE overflows the tiny `$0300`
buffer into neighboring RAM; SDL sizes `g_VRAM_Buffer1` at 512 bytes so the
same linear walk is safe.

Palette RAM is the source of truth. `$3F00` is the universal backdrop. Writes
to `$3F10` / `$3F14` / `$3F18` / `$3F1C` mirror `$3F00` / `$3F04` / `$3F08` /
`$3F0C`, as on the NES. `g_AreaType` indexes palettes in ROM order: water 0,
ground 1, underground 2, castle 3.

## A gameplay frame

When game mode reaches task 3,
[`GameMode_MainLoop`](../engine/game-mode/core.c) runs
`GameCoreRoutine`:

1. If Luigi is active, copy port 2 bits into `g_SavedJoypadBits`.
2. **`GameRoutines_Dispatch`** — one routine from `g_GameEngineSubroutine`
   (`$0E`):
   entrance, pipes, vine, flagpole, death, size-change, injury blink,
   fire-flower, or **player control** (subroutine 8).
3. **`GameEngine`** — the rest of the frame: fireballs, enemies, player
   sprites, brick bounces, hammers, cannons, whirlpools, flagpole, timer,
   coin sparkle, then the area parser for newly scrolled columns.

Player control (subroutine 8) is the everyday path:

```text
Player_UpdateControl          buttons → direction / crouch
Player_MovementSubs           ground / jump / fall / swim / climb
Player_UpdateMovingDirection
Scroll_Update                 camera follows Mario
offscreen bits + relative X/Y
Collision_UpdatePlayerBoundingBox
Collision_PlayerBG            feet, head, walls
Player_Hole                   fell off the screen?
```

Then `GameEngine` (still the same NMI):

```text
Enemy_ProcFireballBubble
Enemies_Core                  six object slots
PlayerGfxHandler              write Mario into g_SpriteData
Collision_DrawBlockObjects    bouncing bricks / coins
Enemy_ProcessHammers
Enemy_ProcessCannons
Misc_ProcessWhirlpools
Enemy_RunFlagpoleRoutine
RunGameTimer
Player_ColorRotation
Scroll_RunParserTask          draw the next column if needed
```

Order matters. Fireballs see last frame’s enemy boxes; newly spawned enemies
run later in the same NMI. A/B edge tests use `g_PreviousA_B_Buttons`,
saved at the *end* of `GameEngine`.

## Player

| File | Owns |
|------|------|
| [`player-physics.c`](../engine/player/player-physics.c) | Friction, jump tables, `Player_MovementSubs`, horizontal/vertical move |
| [`player-sprite.c`](../engine/player/player-sprite.c) | `PlayerGfxHandler`, graphics table, H-flip of the splayed foot tile |
| [`player.c`](../engine/player/player.c) | Loads those ROM tables from `assets/` |

Shared motion (page:x, gravity, relative position) is
[`engine/spr-object.c`](../engine/spr-object.c). Player, enemies, fireballs,
and misc objects all go through that helper instead of each inventing ADC.

Mario is 8×8 sprites, four rows of two tiles. The graphics table is eight
bytes per pose: left/right tile for each row. Facing left swaps the pair and
sets OAM attribute `$40`. After drawing, `ChkForPlayerAttrib` toggles
horizontal flip on the bottom-right sprite for standing / crouch / death so
both feet splay outward.

CHR layout (PPUCTRL `%00010000`): sprites at `$0000`, background at `$1000`.
OAM tile bytes are a direct 0–255 index into the sprite pattern table.

## Level

[`engine/level/`](../engine/level/) is the area parser and the 32×13 block
buffer (NES `$0500` / `$05d0`).

| File | Owns |
|------|------|
| `level-load.c` | World → area pointer, header, enemy data index |
| `level-objects.c` | Object types in the area stream (pipes, stairs, …) |
| `level-column.c` | Terrain bits → 13 metatile rows |
| `level-vram.c` | Metatiles → nametable writes |
| `level.c` | Shared tables and block-buffer probes |

`Level_Load(world, area)` picks a pointer. Screen routine 8 and
`Scroll_RunParserTask` then ask the parser for columns as the camera moves.
Collision does not scan the framebuffer; it reads the block buffer through
`Level_BlockBufferCollision`.

## Enemies

Six slots (five ordinary + power-up / flag in slot 5), plus two fireballs.
The public API is [`engine/enemy/enemy.h`](../engine/enemy/enemy.h). Internals
are split so each translation unit matches a cluster of ASM:

| File | Owns |
|------|------|
| `enemy-slot.c` | Slot allocation, parallel RAM fields |
| `enemy-spawn.c` | Stream decode, constructors |
| `enemy-run.c` | Per-ID run/dispatch |
| `enemy-move.c` | Walking, flying, Bowser, platforms |
| `enemy-collide.c` | Player/enemy and enemy/enemy |
| `enemy-draw.c` | OAM for each type |
| `enemy.c` | Fireballs, flagpole, hammers, `Enemies_Core` |
| `enemy-data.c` | Loaded ROM tables |

`Enemies_Core` is a loop: for each slot, process the object, then floatey
score numbers. Jump tables stay in C (they are code, not `.db` data). Speeds,
timers, and tile lists come from `assets/tables/`.

## Collision, scroll, HUD, misc

- [`engine/collision.c`](../engine/collision.c) — player vs background, brick
  objects, jumping coins / hammers in `Misc_State`.
- [`engine/scroll.c`](../engine/scroll.c) — `ScrollHandler`, scroll lock,
  when to request another parser column.
- [`engine/screen/routine/hud.c`](../engine/screen/routine/hud.c) and
  [`engine/score.c`](../engine/score.c) — status bar and digit math.
- [`engine/misc.c`](../engine/misc.c) — whirlpools and other leftovers that
  are not player, enemy, or level.
- [`engine/sprite-offsets.c`](../engine/sprite-offsets.c) — OAM slot shuffle
  (the NES trick that reduces sprite-0 and flicker artifacts).
- [`engine/timers.c`](../engine/timers.c) — `g_Timers[35]`, interval control.

## Audio

Gameplay only **queues** songs and SFX (`g_EventMusicQueue`,
`g_Square1SoundQueue`, …). `Audio_SoundEngine` in
[`engine/audio.c`](../engine/audio.c) is the original driver: priorities,
note counters, pause. It writes APU registers through
`platform_apu_write`. SDL and DOS turn those writes into PCM with Nes_Snd_Emu;
the SDL 1.2 compatibility frontend sends them through its SDL 1.2 APU callback.
Music bytes are `assets/audio/music_data.bin`. Full notes: [audio.md](audio.md).

## How a picture is built (host)

Shared PPU memory is [`system/common/ppu_memory.c`](../system/common/ppu_memory.c):
nametables, palette RAM, generation counters. The software compositor is
[`system/sdl/video_soft.c`](../system/sdl/video_soft.c) (DOS reuses the same
idea, then blits to VGA).

Each present:

1. Clear to backdrop (`PPU_ReadPalette(0)`).
2. If the screen is enabled, draw the scrolled nametable from CHR.
3. Draw sprites from `g_RenderOAM` (8×8, sprite-0 HUD split when flagged).
4. Upload a 256×240 buffer (SDL texture, SDL 1.2 surface, VGA page, or ANSI
   cells).

The NES nametable persists in PPU RAM. SDL2 rebuilds its pixel image every
frame from that RAM, while the SDL 1.2 adapter keeps a presentation-only
background cache and applies camera shifts plus dirty-tile updates. Neither
cache is game state; PPU RAM remains the source of truth.

## Assets

[`engine/assets.c`](../engine/assets.c) loads from a directory named `assets/`
next to the binary, or from packed `ASSETS.DAT` on DOS.

```bash
make extract ROM=/path/to/Super\ Mario\ Bros.\ \(W\)\ \[\!\].nes
```

Accepted dump SHA-1: `ea343f4e445a9050d4b4fbac2c77d0693b1d0922`.
`make sdl` will extract automatically if `assets/` is missing and `ROM=` or
`SMB_ROM` is set.

What lands in `assets/`:

- `tiles.chr` — both pattern tables
- `areas/`, `enemies/` — compressed level and enemy streams
- `audio/music_data.bin` — contiguous `audio/music.asm` data, excluding vectors
- `tables/*.bin` — every ROM `.db` the engine copies at startup
- `compat-rom-windows/*.bin` — isolated, documented table-overread behavior
- `manifest.json` — provenance, extraction method, size, and hash

`Assets_Init` then calls each subsystem’s `*_LoadRomTables()`. Game logic
must not grow new graphics, music, or table blobs in C. Add ordinary authored
data to `tools/extract_assets.py`; only observable table-overread behavior may
use `compat-rom-windows/`. Jump/dispatch tables that are code stay in C, while
offsets derivable from record layout are computed by the loader.

## If you want to change gameplay

The 6502 disassembly is the spec. An external NES/TAS reference check is how
we *find* mistakes, not a license to special-case a movie. The isolated TAS
optimizer must not treat this implementation as its oracle.

Cite the ASM routine, the RAM names, and any branch you are still deferring.
A longer matching frame prefix by itself is not a fix.

## “Where is …?”

| I want… | Open |
|---------|------|
| The frame loop | `main.c`, `engine/nmi.c` |
| Title vs play vs victory | `engine/opermode.c` |
| World card / TIME UP / title RLE | `engine/screen/` |
| Mario physics | `engine/player/player-physics.c` |
| Mario drawing | `engine/player/player-sprite.c` |
| Goomba / Bowser / Piranha | `engine/enemy/` |
| 1-1 column data | `engine/level/` + `assets/areas/` |
| Hitting a brick | `engine/collision.c` |
| Camera | `engine/scroll.c` |
| Coin sparkle palette | `Player_ColorRotation` in `player-sprite.c` |
| Jump SFX / overworld theme | `engine/audio.c` |
| SDL window | `system/sdl/platform_sdl.c` |
| SDL 1.2 / Win98 surface | `system/sdl12/platform_sdl12.c` |
| Nametable pixels | `system/sdl/video_soft.c` |
| RAM names | `constants/globals.h` |
| Entity IDs / music IDs | `constants/entity_constants.h`, `music_constants.h` |

## Related documents

- [README.md](../README.md) — build, controls, headless flags
- [assets/README.md](../assets/README.md) — extraction
- [audio.md](audio.md) — SoundEngine and APU host
- [dos.md](dos.md) — Mode X, Sound Blaster, benchmarks
- [smb1-disasm](https://github.com/pgattic/smb1-disasm) — 6502 specification
