/* Shared NMI UpdateScreen / VRAM buffer flush for SDL and DOS. */

#include "platform.h"
#include "common/ppu_memory.h"
#include "../constants/defs.h"
#include "../engine/assets.h"

#include <stddef.h>
#include <stdint.h>

static const uint8_t *rom_entry_water;
static size_t rom_entry_water_len;
static const uint8_t *rom_entry_ground;
static size_t rom_entry_ground_len;
static const uint8_t *rom_entry_underground;
static size_t rom_entry_underground_len;
static const uint8_t *rom_entry_castle;
static size_t rom_entry_castle_len;
static const uint8_t *rom_entry_snow_day;
static size_t rom_entry_snow_day_len;
static const uint8_t *rom_entry_snow_night;
static size_t rom_entry_snow_night_len;
static const uint8_t *rom_entry_mushroom;
static size_t rom_entry_mushroom_len;
static const uint8_t *rom_entry_bowser;
static size_t rom_entry_bowser_len;
static const uint8_t *rom_entry_mario_thanks;
static size_t rom_entry_mario_thanks_len;
static const uint8_t *rom_entry_luigi_thanks;
static size_t rom_entry_luigi_thanks_len;
static const uint8_t *rom_entry_retainer;
static size_t rom_entry_retainer_len;
static const uint8_t *rom_entry_princess_1;
static size_t rom_entry_princess_1_len;
static const uint8_t *rom_entry_princess_2;
static size_t rom_entry_princess_2_len;
static const uint8_t *rom_entry_world_select_1;
static size_t rom_entry_world_select_1_len;
static const uint8_t *rom_entry_world_select_2;
static size_t rom_entry_world_select_2_len;

static void flush_entry_stream(const uint8_t *data, int max_len)
{
    int pos = 0;
    while (pos + 2 < max_len) {
        uint8_t addr_hi = data[pos];
        uint8_t addr_lo;
        uint8_t control;
        uint16_t vram_addr;
        int length;
        int vertical;
        int repeat;
        int entry_size;
        uint8_t saved_ctrl;
        int i;

        if (addr_hi == 0x00)
            break;

        addr_lo = data[pos + 1];
        control = data[pos + 2];
        vram_addr = ((uint16_t)addr_hi << 8) | addr_lo;
        length = control & 0x3F;
        vertical = (control & 0x80) != 0;
        repeat = (control & 0x40) != 0;
        if (length == 0)
            length = 256;

        PPU_SetFullAddr(vram_addr);
        saved_ctrl = PPU_GetCtrl();
        PPU_SetIncrement32(vertical ? 1 : 0);
        if (repeat) {
            uint8_t byte = data[pos + 3];
            for (i = 0; i < length; i++)
                PPU_WriteData(byte);
            entry_size = 4;
        } else {
            for (i = 0; i < length; i++)
                PPU_WriteData(data[pos + 3 + i]);
            entry_size = 3 + length;
        }
        PPU_SetCtrl(saved_ctrl);
        pos += entry_size;
    }
}

typedef struct {
    const uint8_t *data;
    int length;
} VramAddrSource;

static VramAddrSource vram_addr_sources[VRAM_ADDR_TABLE_COUNT];
static uint8_t vram_addr_sources_ready;

static void init_vram_addr_sources(void)
{
    extern uint8_t g_VRAM_Buffer1[];
    extern uint8_t g_VRAM_Buffer2[];

    if (vram_addr_sources_ready)
        return;

#define LOAD_VRAM(slot, rel, ptr, len) \
    do { \
        (ptr) = Assets_Load((rel), &(len)); \
        if (!(ptr)) \
            return; \
        vram_addr_sources[(slot)].data = (ptr); \
        vram_addr_sources[(slot)].length = (int)(len); \
    } while (0)

    vram_addr_sources[VRAM_ADDR_BUFFER1].data = g_VRAM_Buffer1;
    vram_addr_sources[VRAM_ADDR_BUFFER1].length = 510;
    LOAD_VRAM(VRAM_ADDR_WATER_PALETTE, "tables/vram_water_palette.bin",
              rom_entry_water, rom_entry_water_len);
    LOAD_VRAM(VRAM_ADDR_GROUND_PALETTE, "tables/vram_ground_palette.bin",
              rom_entry_ground, rom_entry_ground_len);
    LOAD_VRAM(VRAM_ADDR_UNDERGROUND_PALETTE, "tables/vram_underground_palette.bin",
              rom_entry_underground, rom_entry_underground_len);
    LOAD_VRAM(VRAM_ADDR_CASTLE_PALETTE, "tables/vram_castle_palette.bin",
              rom_entry_castle, rom_entry_castle_len);
    vram_addr_sources[VRAM_ADDR_BUFFER1_OFFSET].data = g_VRAM_Buffer1;
    vram_addr_sources[VRAM_ADDR_BUFFER1_OFFSET].length = 510;
    vram_addr_sources[VRAM_ADDR_BUFFER2].data = g_VRAM_Buffer2;
    vram_addr_sources[VRAM_ADDR_BUFFER2].length = 510;
    vram_addr_sources[VRAM_ADDR_BUFFER2_ALT].data = g_VRAM_Buffer2;
    vram_addr_sources[VRAM_ADDR_BUFFER2_ALT].length = 510;
    LOAD_VRAM(VRAM_ADDR_BOWSER_PALETTE, "tables/vram_bowser.bin",
              rom_entry_bowser, rom_entry_bowser_len);
    LOAD_VRAM(VRAM_ADDR_SNOW_DAY, "tables/vram_snow_day.bin",
              rom_entry_snow_day, rom_entry_snow_day_len);
    LOAD_VRAM(VRAM_ADDR_SNOW_NIGHT, "tables/vram_snow_night.bin",
              rom_entry_snow_night, rom_entry_snow_night_len);
    LOAD_VRAM(VRAM_ADDR_MUSHROOM, "tables/vram_mushroom.bin",
              rom_entry_mushroom, rom_entry_mushroom_len);
    LOAD_VRAM(VRAM_ADDR_MARIO_THANKS, "tables/vram_mario_thanks.bin",
              rom_entry_mario_thanks, rom_entry_mario_thanks_len);
    LOAD_VRAM(VRAM_ADDR_LUIGI_THANKS, "tables/vram_luigi_thanks.bin",
              rom_entry_luigi_thanks, rom_entry_luigi_thanks_len);
    LOAD_VRAM(VRAM_ADDR_RETAINER, "tables/vram_retainer.bin",
              rom_entry_retainer, rom_entry_retainer_len);
    LOAD_VRAM(VRAM_ADDR_PRINCESS_1, "tables/vram_princess_1.bin",
              rom_entry_princess_1, rom_entry_princess_1_len);
    LOAD_VRAM(VRAM_ADDR_PRINCESS_2, "tables/vram_princess_2.bin",
              rom_entry_princess_2, rom_entry_princess_2_len);
    LOAD_VRAM(VRAM_ADDR_WORLD_SELECT_1, "tables/vram_world_select_1.bin",
              rom_entry_world_select_1, rom_entry_world_select_1_len);
    LOAD_VRAM(VRAM_ADDR_WORLD_SELECT_2, "tables/vram_world_select_2.bin",
              rom_entry_world_select_2, rom_entry_world_select_2_len);
#undef LOAD_VRAM
    vram_addr_sources_ready = 1;
}

void platform_vram_flush(void)
{
    extern uint8_t g_VRAM_Buffer1[];
    extern uint8_t g_VRAM_Buffer1_Offset;
    extern uint8_t g_VRAM_Buffer2[];
    extern uint8_t g_VRAM_Buffer2_Offset;
    extern uint8_t g_VRAM_Buffer_AddrCtrl;

    init_vram_addr_sources();
    if (g_VRAM_Buffer_AddrCtrl < VRAM_ADDR_TABLE_COUNT) {
        flush_entry_stream(vram_addr_sources[g_VRAM_Buffer_AddrCtrl].data,
                           vram_addr_sources[g_VRAM_Buffer_AddrCtrl].length);
    } else {
        flush_entry_stream(g_VRAM_Buffer1, 510);
    }

    if (g_VRAM_Buffer_AddrCtrl == 6) {
        g_VRAM_Buffer2_Offset = 0;
        g_VRAM_Buffer2[0] = 0;
    } else {
        g_VRAM_Buffer1_Offset = 0;
        g_VRAM_Buffer1[0] = 0;
    }
    g_VRAM_Buffer_AddrCtrl = 0;
}
