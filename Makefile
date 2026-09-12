# Makefile for Super Mario Bros. Reimplementation
# Builds the supported host frontends: SDL2, native Win9x, DOS, and terminal.

# ========================================================================
# PATHS
# ========================================================================

SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS = $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")

# ========================================================================
# OUTPUT FILES
# ========================================================================

SDL_TARGET = smb2
SDL_RELEASE_TARGET = smb2-release

# ========================================================================
# SDL (PC) COMPILER SETTINGS
# ========================================================================

CC = gcc
WARNFLAGS = -Wall -Wextra

# Debug build (default): symbols, no optimization, debug features enabled
INCLUDES = -I. -Iengine
DEBUG_CFLAGS = -g -O0 $(WARNFLAGS) $(SDL_CFLAGS) $(INCLUDES)

# Release build: optimized, debug features disabled (headless mode, debug prints)
RELEASE_CFLAGS = -O2 -DNDEBUG $(WARNFLAGS) $(SDL_CFLAGS) $(INCLUDES)

# ========================================================================
# SOURCE FILES
# ========================================================================

# System (platform layer)
SYSTEM_ASM = 
SYSTEM_C = system/sdl/platform_sdl.c system/common/ppu_memory.c system/sdl/video_soft.c system/vram_flush.c

# Engine
ENGINE_C = engine/nmi.c engine/audio.c engine/assets.c engine/timers.c engine/title-menu.c engine/victory.c engine/sprite-offsets.c engine/spr-object.c engine/score.c engine/misc.c engine/enemy/enemy-data.c engine/enemy/enemy.c engine/enemy/enemy-slot.c engine/enemy/enemy-spawn.c engine/enemy/enemy-move.c engine/enemy/enemy-collide.c engine/enemy/enemy-draw.c engine/enemy/enemy-run.c engine/opermode.c engine/game-mode/core.c engine/scroll.c engine/player/player.c engine/player/player-physics.c engine/player/player-sprite.c engine/collision.c engine/level/level.c engine/level/level-load.c engine/level/level-vram.c engine/level/level-column.c engine/level/level-objects.c engine/screen/screen.c engine/screen/routine/colors.c engine/screen/routine/hud.c engine/screen/routine/init.c engine/screen/routine/intermediate.c engine/screen/routine/title.c engine/screen/routine/area_parser.c constants/globals.c

APU_CXX = system/sdl/apu_sdl.cpp third_party/nes_snd_emu/nes_apu/Blip_Buffer.cpp third_party/nes_snd_emu/nes_apu/Multi_Buffer.cpp third_party/nes_snd_emu/nes_apu/Nes_Apu.cpp third_party/nes_snd_emu/nes_apu/Nes_Oscs.cpp third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.cpp
APU_OBJECTS = $(patsubst %.cpp,build/apu/%.o,$(APU_CXX))

# Main
MAIN_C = main.c

# All C source files
C_SOURCES = $(MAIN_C) $(SYSTEM_C) $(ENGINE_C) system/fm2.c system/state_stream.c

# ========================================================================
# ASSET EXTRACTION
# The playable binary never opens the iNES file. Extract once into assets/.
# ========================================================================

DEFAULT_ROM = ../../Super Mario Bros. (W) [!].nes
ifeq ($(origin ROM), command line)
else ifneq ($(SMB_ROM),)
ROM := $(SMB_ROM)
else
ROM := $(DEFAULT_ROM)
endif
ASSET_DIR = assets
EXTRACT_TOOL = tools/extract_assets.py
ASSETS_STAMP = $(ASSET_DIR)/.extracted

# ========================================================================
# BUILD RULES
# ========================================================================

.PHONY: all sdl sdl-debug sdl-release win95 win95-debug win95-release win95-audit mingw mingw-debug mingw-release mingw64 mingw64-debug mingw64-release mingw32 mingw32-debug mingw32-release dos dos-bench dos-floppy terminal clean extract test help

# Default: build debug
all: sdl

# MinGW-w64 cross-compilation (Windows)
mingw:
	@$(MAKE) mingw64-debug

mingw-debug:
	@$(MAKE) mingw64-debug

mingw-release:
	@$(MAKE) mingw64-release

mingw64:
	@$(MAKE) mingw64-debug

mingw64-debug:
	@$(MAKE) -f Makefile.mingw CROSS=x86_64-w64-mingw32- debug

mingw64-release:
	@$(MAKE) -f Makefile.mingw CROSS=x86_64-w64-mingw32- release

mingw32:
	@$(MAKE) mingw32-debug

mingw32-debug:
	@$(MAKE) -f Makefile.mingw CROSS=i686-w64-mingw32- debug

mingw32-release:
	@$(MAKE) -f Makefile.mingw CROSS=i686-w64-mingw32- release

# Build SDL debug (default)
sdl: sdl-debug
	@true

sdl-debug: $(ASSETS_STAMP) $(SDL_TARGET)
	@echo "SDL debug build complete: $(SDL_TARGET)"

sdl-release: $(ASSETS_STAMP) $(SDL_RELEASE_TARGET)
	@echo "SDL release build complete: $(SDL_RELEASE_TARGET)"

# Debug build: no NDEBUG → headless mode + debug prints enabled
$(SDL_TARGET): $(ASSETS_STAMP) $(C_SOURCES) $(APU_OBJECTS)
	$(CC) $(C_SOURCES) $(APU_OBJECTS) $(DEBUG_CFLAGS) $(SDL_LIBS) -lstdc++ -lm -o $@

# Release build: NDEBUG defined → no debug features, optimized
$(SDL_RELEASE_TARGET): $(ASSETS_STAMP) $(C_SOURCES) $(APU_OBJECTS)
	$(CC) $(C_SOURCES) $(APU_OBJECTS) $(RELEASE_CFLAGS) $(SDL_LIBS) -lstdc++ -lm -o $@

build/apu/%.o: %.cpp
	@mkdir -p $(dir $@)
	g++ -std=c++11 -O2 -Wall -Wextra -include climits $(SDL_CFLAGS) -c $< -o $@

# ========================================================================
# Native Win32 / Win9x x86 frontend (no SDL dependency)
#
# It uses GDI, GetAsyncKeyState, timeGetTime, and a direct WinMM waveOut
# queue.  The frontend is the supported legacy Windows target.
# ========================================================================

WIN95_CC ?= i586-mingw32msvc-gcc
WIN95_CXX ?= i586-mingw32msvc-g++
WIN95_OBJDUMP ?= $(patsubst %gcc,%objdump,$(WIN95_CC))
WIN95_TARGET = smb2-win95-x86.exe
WIN95_DEBUG_TARGET = smb2-win95-debug-x86.exe
WIN95_CFLAGS ?= -std=gnu99 -O3 -DNDEBUG -DWIN95 -march=pentium -mtune=pentium -fomit-frame-pointer $(WARNFLAGS) $(INCLUDES)
WIN95_DEBUG_CFLAGS ?= -std=gnu99 -g -O0 -DWIN95 -march=pentium -mtune=pentium $(WARNFLAGS) $(INCLUDES)
WIN95_CXXFLAGS ?= -std=c++0x -O3 -DNDEBUG -DWIN95 -march=pentium -mtune=pentium -fomit-frame-pointer -fno-exceptions -fno-rtti $(WARNFLAGS) -include climits $(INCLUDES)
WIN95_DEBUG_CXXFLAGS ?= -std=c++0x -g -O0 -DWIN95 -march=pentium -mtune=pentium -fno-exceptions -fno-rtti $(WARNFLAGS) -include climits $(INCLUDES)
WIN95_LDFLAGS ?= -Wl,-Bstatic -static-libgcc -Wl,-Bdynamic -mwindows
WIN95_LIBS ?= -luser32 -lgdi32 -lwinmm -Wl,-Bstatic -lstdc++ -Wl,-Bdynamic -lm
WIN95_SYSTEM_C = system/win95/platform_win95.c system/common/ppu_memory.c system/sdl/video_soft.c system/vram_flush.c
WIN95_C_SOURCES = $(MAIN_C) $(WIN95_SYSTEM_C) $(ENGINE_C) system/fm2.c system/state_stream.c
WIN95_APU_CXX = system/win95/apu_win95.cpp third_party/nes_snd_emu/nes_apu/Blip_Buffer.cpp third_party/nes_snd_emu/nes_apu/Multi_Buffer.cpp third_party/nes_snd_emu/nes_apu/Nes_Apu.cpp third_party/nes_snd_emu/nes_apu/Nes_Oscs.cpp third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.cpp
WIN95_APU_DIR = build/win95-apu
WIN95_APU_OBJECTS = $(patsubst %.cpp,$(WIN95_APU_DIR)/%.o,$(WIN95_APU_CXX))
WIN95_DEBUG_APU_DIR = build/win95-debug-apu
WIN95_DEBUG_APU_OBJECTS = $(patsubst %.cpp,$(WIN95_DEBUG_APU_DIR)/%.o,$(WIN95_APU_CXX))

win95: win95-release

win95-debug: $(ASSETS_STAMP) $(WIN95_DEBUG_TARGET)
	@echo "Native Win32 debug build complete: $(WIN95_DEBUG_TARGET)"

win95-release: $(ASSETS_STAMP) $(WIN95_TARGET)
	@$(MAKE) --no-print-directory win95-audit
	@echo "Native Win32 Win9x build complete: $(WIN95_TARGET)"

.PHONY: win95-audit
win95-audit:
	@echo "Auditing native Win32 frontend for Pentium/i586 and Win9x compatibility"
	@test -f $(WIN95_TARGET) || { echo "error: missing $(WIN95_TARGET)" >&2; exit 1; }
	@command -v "$(WIN95_OBJDUMP)" >/dev/null 2>&1 || { echo "error: WIN95_OBJDUMP not found: $(WIN95_OBJDUMP)" >&2; exit 1; }
	@if $(WIN95_OBJDUMP) -d $(WIN95_TARGET) | grep -Eiq '\bcmov[a-z]*\b|%xmm[0-7]\b|%mm[0-7]\b|\b(emms|femms|movdqa|movdqu|movaps|movups|padd[bwdq]|psub[bwdq]|pmulhw|pmullw)\b'; then \
		echo "error: $(WIN95_TARGET) contains post-i586 instructions" >&2; exit 1; \
	fi
	@if $(WIN95_OBJDUMP) -p $(WIN95_TARGET) | grep -Eiq '[[:space:]](GetThreadId|TryEnterCriticalSection|InitializeCriticalSectionEx)[[:space:]]*$$'; then \
		echo "error: $(WIN95_TARGET) imports APIs unavailable on Windows 9x" >&2; exit 1; \
	fi
	@if $(WIN95_OBJDUMP) -p $(WIN95_TARGET) | grep -Eiq '[[:space:]]SDL(_main)?\.dll[[:space:]]*$$'; then \
		echo "error: native Win32 target unexpectedly imports SDL" >&2; exit 1; \
	fi

$(WIN95_TARGET): $(ASSETS_STAMP) $(WIN95_C_SOURCES) $(WIN95_APU_OBJECTS)
	$(WIN95_CC) $(WIN95_C_SOURCES) $(WIN95_APU_OBJECTS) $(WIN95_CFLAGS) $(WIN95_LDFLAGS) $(WIN95_LIBS) -o $@

$(WIN95_DEBUG_TARGET): $(ASSETS_STAMP) $(WIN95_C_SOURCES) $(WIN95_DEBUG_APU_OBJECTS)
	$(WIN95_CC) $(WIN95_C_SOURCES) $(WIN95_DEBUG_APU_OBJECTS) $(WIN95_DEBUG_CFLAGS) $(WIN95_LDFLAGS) $(WIN95_LIBS) -o $@

$(WIN95_APU_DIR)/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(WIN95_CXX) $(WIN95_CXXFLAGS) -c $< -o $@

$(WIN95_DEBUG_APU_DIR)/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(WIN95_CXX) $(WIN95_DEBUG_CXXFLAGS) -c $< -o $@

# Extract assets from a user-supplied canonical SMB1 iNES dump
$(ASSETS_STAMP): $(EXTRACT_TOOL)
	@if [ ! -f "$(ROM)" ]; then \
	  echo "Need a Super Mario Bros. (W) [!] dump: make extract ROM=path.nes"; \
	  echo "Accepted SHA-1: ea343f4e445a9050d4b4fbac2c77d0693b1d0922"; \
	  exit 1; \
	fi
	python3 $(EXTRACT_TOOL) "$(ROM)" $(ASSET_DIR)
	@touch $(ASSETS_STAMP)

extract: $(ASSETS_STAMP)

# ANSI/UTF-8 terminal frontend: 60 Hz logic, 30 Hz text presentation, no audio.
TERMINAL_TARGET = smb2-terminal
TERMINAL_SYSTEM_C = system/terminal/platform_terminal.c system/common/ppu_memory.c system/sdl/video_soft.c system/vram_flush.c
TERMINAL_C_SOURCES = $(MAIN_C) $(TERMINAL_SYSTEM_C) $(ENGINE_C) system/fm2.c system/state_stream.c

terminal: $(ASSETS_STAMP) $(TERMINAL_TARGET)
	@echo "Terminal build complete: $(TERMINAL_TARGET)"

$(TERMINAL_TARGET): $(ASSETS_STAMP) $(TERMINAL_C_SOURCES)
	$(CC) $(TERMINAL_C_SOURCES) -O2 -DNDEBUG $(WARNFLAGS) $(INCLUDES) -o $@

# ========================================================================
# DOS VGA (DJGPP protected-mode 386+)
# ========================================================================

DJGPP_PREFIX ?= $(HOME)/djgpp
DJGPP_CC = $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
DJGPP_CXX = $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-g++
DJGPP_ENV = PATH="$(DJGPP_PREFIX)/bin:$(DJGPP_PREFIX)/i586-pc-msdosdjgpp/bin:$$PATH" \
	GCC_EXEC_PREFIX="$(DJGPP_PREFIX)/lib/gcc/" \
	DJDIR="$(DJGPP_PREFIX)/i586-pc-msdosdjgpp"
DOS_DIR = dosdist
DOS_EXE = $(DOS_DIR)/SMB2.EXE
ASSETS_DAT = $(DOS_DIR)/ASSETS.DAT
PACK_TOOL = tools/pack_assets.py
CWSDPMI_DIR = third_party/cwsdpmi
CWSDPMI_ZIP = $(CWSDPMI_DIR)/csdpmi7b.zip
CWSDPMI_EXE = $(CWSDPMI_DIR)/bin/CWSDPMI.EXE
DOS_ARCH_FLAGS = -march=i386 -mtune=i486 -fomit-frame-pointer
DOS_CFLAGS = -DDOS -DNDEBUG -O2 $(DOS_ARCH_FLAGS) $(WARNFLAGS) $(INCLUDES)
DOS_CXXFLAGS = -std=gnu++11 -DDOS -DNDEBUG -O2 $(DOS_ARCH_FLAGS) -Wall -Wextra -fno-exceptions -fno-rtti -include climits $(INCLUDES)
DOS_SYSTEM_C = system/dos/platform_dos.c system/common/ppu_memory.c system/dos/dos_renderer.c system/vram_flush.c
DOS_C_SOURCES = $(MAIN_C) $(DOS_SYSTEM_C) $(ENGINE_C)
DOS_C_OBJECTS = $(patsubst %.c,build/dos/%.o,$(DOS_C_SOURCES)) build/dos/system/dos/dos_blit.o
DOS_APU_CXX = system/dos/apu_dos.cpp third_party/nes_snd_emu/nes_apu/Blip_Buffer.cpp third_party/nes_snd_emu/nes_apu/Multi_Buffer.cpp third_party/nes_snd_emu/nes_apu/Nes_Apu.cpp third_party/nes_snd_emu/nes_apu/Nes_Oscs.cpp third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.cpp
DOS_APU_OBJECTS = $(patsubst %.cpp,build/dos-apu/%.o,$(DOS_APU_CXX))
DOS_IMG = $(DOS_DIR)/SMB2.IMG
DOS_BENCH_EXE = $(DOS_DIR)/BENCH.EXE
DOS_BENCH_CFLAGS = $(DOS_CFLAGS) -DDOS_BENCH
DOS_BENCH_C_SOURCES = $(DOS_C_SOURCES) system/dos/dos_bench.c
DOS_BENCH_C_OBJECTS = $(patsubst %.c,build/dos-bench/%.o,$(DOS_BENCH_C_SOURCES)) build/dos-bench/system/dos/dos_blit.o
DOS_BENCH_APU_OBJECTS = $(patsubst %.cpp,build/dos-bench-apu/%.o,$(DOS_APU_CXX))

dos: $(DOS_EXE) $(ASSETS_DAT) $(DOS_DIR)/CWSDPMI.EXE $(DOS_DIR)/README.TXT
	@echo "DOS build complete: $(DOS_DIR)/"

dos-bench: $(DOS_BENCH_EXE) $(ASSETS_DAT) $(DOS_DIR)/CWSDPMI.EXE
	@echo "DOS benchmark build complete: $(DOS_BENCH_EXE)"

.PHONY: test-dos-renderer test-dos-game-renderer
test-dos-renderer: test-dos-game-renderer
	@mkdir -p build/tests
	cc -std=c99 -O2 -Wall -Wextra tools/test_dos_renderer.c system/dos/dos_renderer.c system/sdl/video_soft.c system/common/ppu_memory.c -o build/tests/test_dos_renderer
	./build/tests/test_dos_renderer

test-dos-game-renderer: $(ASSETS_STAMP)
	@mkdir -p build/tests
	$(CC) -std=c99 -O2 -DNDEBUG $(WARNFLAGS) $(INCLUDES) \
		tools/test_dos_game_renderer.c system/dos/dos_renderer.c \
		system/sdl/video_soft.c system/common/ppu_memory.c system/vram_flush.c \
		$(ENGINE_C) -o build/tests/test_dos_game_renderer
	./build/tests/test_dos_game_renderer
	./build/tests/test_dos_game_renderer --start

build/dos-bench/system/dos/dos_bench.o: $(DOS_C_SOURCES) system/dos/dos_blit.S system/dos/dos_bench.h system/dos/dos_renderer.h
build/dos/system/dos/platform_dos.o build/dos/system/dos/dos_renderer.o build/dos-bench/system/dos/platform_dos.o build/dos-bench/system/dos/dos_renderer.o: system/dos/dos_renderer.h system/dos/dos_bench.h
build/dos-bench/main.o: system/dos/dos_bench.h

# 1.44M FAT12 data floppy (not bootable). Boot DOS from elsewhere, then A:\SMB2.EXE.
dos-floppy: $(DOS_IMG)

$(DOS_IMG): $(DOS_EXE) $(DOS_BENCH_EXE) $(ASSETS_DAT) $(DOS_DIR)/CWSDPMI.EXE $(DOS_DIR)/README.TXT
	@mkdir -p $(DOS_DIR)
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	MTOOLS_SKIP_CHECK=1 mformat -f 1440 -v SMB2 -i $@ ::
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ $(DOS_EXE) $(DOS_BENCH_EXE) $(ASSETS_DAT) \
		$(DOS_DIR)/CWSDPMI.EXE $(DOS_DIR)/README.TXT ::
	@echo "DOS floppy image: $@"

$(ASSETS_DAT): $(ASSETS_STAMP) $(PACK_TOOL)
	@mkdir -p $(DOS_DIR)
	python3 $(PACK_TOOL) $(ASSET_DIR) $(ASSETS_DAT)

build/dos/%.o: %.c
	@mkdir -p $(dir $@)
	$(DJGPP_ENV) $(DJGPP_CC) $(DOS_CFLAGS) -c $< -o $@

build/dos/%.o: %.S
	@mkdir -p $(dir $@)
	$(DJGPP_ENV) $(DJGPP_CC) $(DOS_CFLAGS) -c $< -o $@

build/dos-bench/%.o: %.S
	@mkdir -p $(dir $@)
	$(DJGPP_ENV) $(DJGPP_CC) $(DOS_CFLAGS) -c $< -o $@

build/dos-apu/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(DJGPP_ENV) $(DJGPP_CXX) $(DOS_CXXFLAGS) -c $< -o $@

build/dos-bench/%.o: %.c
	@mkdir -p $(dir $@)
	$(DJGPP_ENV) $(DJGPP_CC) $(DOS_BENCH_CFLAGS) -c $< -o $@

build/dos-bench-apu/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(DJGPP_ENV) $(DJGPP_CXX) $(DOS_CXXFLAGS) -DDOS_BENCH -c $< -o $@

$(DOS_EXE): $(DOS_C_OBJECTS) $(DOS_APU_OBJECTS)
	@mkdir -p $(DOS_DIR)
	$(DJGPP_ENV) $(DJGPP_CXX) $(DOS_C_OBJECTS) $(DOS_APU_OBJECTS) -lm -o $@
	@# DJGPP stubify also emits SMB2.exe (lowercase extension) on Linux.
	@find $(DOS_DIR) -maxdepth 1 -name 'SMB2.exe' -delete

$(DOS_BENCH_EXE): $(DOS_BENCH_C_OBJECTS) $(DOS_BENCH_APU_OBJECTS)
	@mkdir -p $(DOS_DIR)
	$(DJGPP_ENV) $(DJGPP_CXX) $(DOS_BENCH_C_OBJECTS) $(DOS_BENCH_APU_OBJECTS) -lm -o $@
	@find $(DOS_DIR) -maxdepth 1 -name 'BENCH.exe' -delete

$(DOS_DIR)/README.TXT: Makefile
	@mkdir -p $(DOS_DIR)
	@printf '%s\n' \
	  'SMB2 DOS VGA (386+ protected mode)' \
	  'Run SMB2.EXE. Keep CWSDPMI.EXE and ASSETS.DAT in this folder.' \
	  'BENCH.EXE runs 1200 frames by default (use --frames N to override).' \
	  'Renderer: hybrid-ring-v7. BENCH.TXT includes build stamp and frame deadlines.' \
	  'BENCH --diagnostic --no-audio tests VGA scrolling without game logic.' \
	  'BENCH --frames 3600 runs the longer demo test with audio enabled.' \
	  'Needs a 386DX, 4MB RAM, VGA. Sound Blaster optional:' \
	  '  SET BLASTER=A220 I5 D1 T4' \
	  'Keys: arrows, X=A, Z=B, Enter=Start, Backspace=Select, Esc=Quit' \
	  'Esc returns to text mode and prints average FPS.' \
	  > $@

$(CWSDPMI_EXE):
	@mkdir -p $(CWSDPMI_DIR)
	@if [ ! -f "$@" ]; then \
	  wget -O $(CWSDPMI_ZIP).tmp https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip && \
	  mv $(CWSDPMI_ZIP).tmp $(CWSDPMI_ZIP) && \
	  unzip -o -q -d $(CWSDPMI_DIR) $(CWSDPMI_ZIP); \
	fi
	@test -f $(CWSDPMI_EXE) || test -f $(CWSDPMI_DIR)/bin/cwsdpmi.exe

$(DOS_DIR)/CWSDPMI.EXE: $(CWSDPMI_EXE)
	@mkdir -p $(DOS_DIR)
	@src="$(CWSDPMI_EXE)"; \
	if [ ! -f "$$src" ]; then src="$(CWSDPMI_DIR)/bin/cwsdpmi.exe"; fi; \
	cp "$$src" $@

# Clean build files (preserves assets)
clean:
	rm -f $(SDL_TARGET) $(SDL_RELEASE_TARGET) $(WIN95_TARGET) $(WIN95_DEBUG_TARGET) *.o engine/*.o engine/*/*.o engine/*/*/*.o system/*.o system/*/*.o constants/*.o smb2
	rm -rf build/apu build/win95-apu build/win95-debug-apu build/dos build/dos-apu $(DOS_DIR)

# Test SDL build (debug)
test: sdl-debug
	./$(SDL_TARGET)

# Help
help:
	@echo "Super Mario Bros. Reimplementation Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build SDL debug version (default)"
	@echo "  sdl          - Build SDL debug version"
	@echo "  sdl-debug    - Build SDL debug (-g -O0, headless mode enabled)"
	@echo "  sdl-release  - Build SDL release (-O2 -DNDEBUG)"
	@echo "  win95        - Native Win32/Win9x x86 build (no SDL dependency)"
	@echo "  win95-debug  - Native Win32 x86 debug build"
	@echo "  win95-release - Native Win32/Win9x x86 release build"
	@echo "  dos          - DJGPP DOS VGA build (dosdist/SMB2.EXE)"
	@echo "  dos-floppy   - 1.44M FAT12 image (dosdist/SMB2.IMG)"
	@echo "  terminal     - ANSI true-color terminal build (30 FPS display, no audio)"
	@echo "  mingw64      - Cross-compile x64 Windows exe (debug): smb2-x64.exe"
	@echo "  mingw64-release - Cross-compile x64 Windows exe (release): smb2-release-x64.exe"
	@echo "  mingw32      - Cross-compile x86 Windows exe (debug): smb2-x86.exe"
	@echo "  mingw32-release - Cross-compile x86 Windows exe (release): smb2-release-x86.exe"
	@echo "  extract      - Extract assets from ROM=path.nes (or SMB_ROM)"
	@echo "  clean        - Remove build files"
	@echo "  test         - Build and run SDL debug version"
	@echo "  help         - Show this message"
	@echo ""
	@echo "Requirements:"
	@echo "  SDL:    libsdl2-dev"
	@echo "  Win95:  i586 MinGW compiler (no SDL package required)"
	@echo "  DOS:    DJGPP at \$$HOME/djgpp (override DJGPP_PREFIX=)"
