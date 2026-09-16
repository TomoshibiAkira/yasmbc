# Shared source manifests for hosted SDL-family frontends.

MAIN_C = main.c
COMMON_C = system/fm2.c system/state_stream.c
SDL_SYSTEM_C = system/sdl/platform_sdl.c system/common/ppu_memory.c system/sdl/video_soft.c system/vram_flush.c

ENGINE_C = engine/nmi.c engine/audio.c engine/assets.c engine/timers.c engine/title-menu.c engine/victory.c engine/sprite-offsets.c engine/spr-object.c engine/score.c engine/misc.c engine/enemy/enemy-data.c engine/enemy/enemy.c engine/enemy/enemy-slot.c engine/enemy/enemy-spawn.c engine/enemy/enemy-move.c engine/enemy/enemy-collide.c engine/enemy/enemy-draw.c engine/enemy/enemy-run.c engine/opermode.c engine/game-mode/core.c engine/scroll.c engine/player/player.c engine/player/player-physics.c engine/player/player-sprite.c engine/collision.c engine/level/level.c engine/level/level-load.c engine/level/level-vram.c engine/level/level-column.c engine/level/level-objects.c engine/screen/screen.c engine/screen/routine/colors.c engine/screen/routine/hud.c engine/screen/routine/init.c engine/screen/routine/intermediate.c engine/screen/routine/title.c engine/screen/routine/area_parser.c constants/globals.c

APU_CXX = system/sdl/apu_sdl.cpp third_party/nes_snd_emu/nes_apu/Blip_Buffer.cpp third_party/nes_snd_emu/nes_apu/Multi_Buffer.cpp third_party/nes_snd_emu/nes_apu/Nes_Apu.cpp third_party/nes_snd_emu/nes_apu/Nes_Oscs.cpp third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.cpp
