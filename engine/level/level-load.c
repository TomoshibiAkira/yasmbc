/* engine/level/level-load.c - GetAreaDataAddrs / area pointer selection */

#include <stdio.h>
#include <string.h>
#include "level/level.h"
#include "level/level-internal.h"
#include "assets.h"
#include "constants/defs.h"
#include "constants/globals.h"

uint8_t g_AreaDataOffset = 0;
const uint8_t* g_AreaDataPtr = NULL;
uint16_t g_AreaDataLen = 0;

/* Nametable base address offset for metatile writes.
 * 0 = NT0 ($2000), 0x0400 = NT1 ($2400).
 * Externally visible so scroll.c can set it for scroll-triggered rendering.
 */
uint16_t s_NTBaseOffset = 0;

/* Parsed header values are the original RAM owners.  AlterAreaAttributes
 * writes these same bytes later while the terrain and palette consumers read
 * them; private shadows would break that shared-RAM contract. */

/* Loaded area data buffer */
static uint8_t s_AreaDataBuffer[1024];

/* LoadAreaPointer/FindAreaPointer (src/level-load.asm:1-21;
 * src/levels.asm:4-26).  AreaNumber indexes an area sequence, not the HUD's
 * four-level number.  Worlds with a pipe transition therefore have a fifth
 * entry: e.g. World1Areas is 1-1, 1-2 pipe intro, 1-2 underground, 1-3, 1-4.
 * Keep these tables verbatim so the continuation pointer is selected by the
 * same RAM state as the 6502 implementation. */
static uint8_t WorldAddrOffsets[8];
static uint8_t WorldAreaCounts[8];
static uint8_t AreaAddrOffsets[36];
static uint8_t AreaTypeBaseIndex[4];
static uint8_t EnemyTypeBaseIndex[4];

int Level_LoadPointerTables(void) {
    static const uint8_t world_counts[8] = {5, 5, 4, 5, 4, 4, 5, 4};
    static const char *const world_assets[8] = {
        "tables/world_1_areas.bin", "tables/world_2_areas.bin",
        "tables/world_3_areas.bin", "tables/world_4_areas.bin",
        "tables/world_5_areas.bin", "tables/world_6_areas.bin",
        "tables/world_7_areas.bin", "tables/world_8_areas.bin"
    };
    uint8_t offset = 0;
    unsigned i;

    for (i = 0; i < 8; i++) {
        WorldAddrOffsets[i] = offset;
        WorldAreaCounts[i] = world_counts[i];
        if (Assets_Copy(world_assets[i], AreaAddrOffsets + offset,
                        world_counts[i]))
            return -1;
        offset = (uint8_t)(offset + world_counts[i]);
    }
    if (offset != sizeof(AreaAddrOffsets) ||
        Assets_Copy("tables/area_type_base_index.bin", AreaTypeBaseIndex,
                    sizeof(AreaTypeBaseIndex)) ||
        Assets_Copy("tables/enemy_type_base_index.bin", EnemyTypeBaseIndex,
                    sizeof(EnemyTypeBaseIndex)))
        return -1;
    return 0;
}

/* GetAreaDataAddrs (src/level-load.asm:23-46) uses the pointer's two type
 * bits and five low bits to index these ROM tables.  The underground bonus
 * entry is extracted from the immutable ROM at PRG offset $2d79; this keeps
 * the asset name aligned with the L_UndergroundArea3 table entry. */
static const char *const AreaDataAssetNames[34] = {
    "water_1", "water_2", "water_3",
    "ground_1", "ground_2", "ground_3", "ground_4", "ground_5",
    "ground_6", "ground_7", "ground_8", "ground_9", "ground_10",
    "ground_11", "ground_12", "ground_13", "ground_14", "ground_15",
    "ground_16", "ground_17", "ground_18", "ground_19", "ground_20",
    "ground_21", "ground_22",
    "underground_1", "underground_2", "underground_3",
    "castle_1", "castle_2", "castle_3", "castle_4", "castle_5",
    "castle_6"
};

/* These are the corresponding EnemyDataAddrLow/High table indices.  The
 * selected index is the C owner of the zero-page EnemyData pointer loaded by
 * GetAreaDataAddrs; enemy.c consumes it at each ProcessEnemyData pass. */
uint8_t s_AreaDataIndex;
uint8_t s_EnemyDataIndex;

static int FindAreaPointer(uint8_t world, uint8_t area, uint8_t *pointer) {
    uint16_t index;

    if (world >= 8 || area >= WorldAreaCounts[world])
        return 0;
    index = (uint16_t)WorldAddrOffsets[world] + area;
    if (index >= sizeof(AreaAddrOffsets))
        return 0;
    *pointer = AreaAddrOffsets[index];
    return 1;
}

static int ResolveAreaPointer(uint8_t pointer, uint8_t world, uint8_t area) {
    uint8_t area_type;
    uint8_t low_offset;
    uint16_t area_index;
    uint16_t enemy_index;

    area_type = (uint8_t)((pointer & 0x60) >> 5);
    low_offset = pointer & 0x1f;
    area_index = (uint16_t)AreaTypeBaseIndex[area_type] + low_offset;
    enemy_index = (uint16_t)EnemyTypeBaseIndex[area_type] + low_offset;
    if (area_index >= sizeof(AreaDataAssetNames) /
                         sizeof(AreaDataAssetNames[0]) || enemy_index >= 34)
        return 0;

    g_AreaPointer = pointer;
    g_AreaType = area_type;
    s_AreaDataIndex = (uint8_t)area_index;
    s_EnemyDataIndex = (uint8_t)enemy_index;

    /* InitializeArea/GetAreaDataAddrs: SecondaryHardMode is enabled for
     * primary hard mode, worlds after World5, and World5 areas 3/4. */
    g_SecondaryHardMode = 0;
    if (g_PrimaryHardMode || world > WORLD_5 ||
        (world == WORLD_5 && area >= 2))
        g_SecondaryHardMode = 1;
    return 1;
}

static int ResolveAreaAddress(uint8_t world, uint8_t area) {
    uint8_t pointer;

    if (!FindAreaPointer(world, area, &pointer))
        return 0;
    g_AreaNumber = area;
    return ResolveAreaPointer(pointer, world, area);
}

/* ========================================================================
 * LEVEL LOADING
 * ======================================================================== */

static int LoadResolvedArea(void) {
    char rel[64];
    const uint8_t *raw;
    size_t raw_len = 0;

    snprintf(rel, sizeof(rel), "areas/%s.bin", AreaDataAssetNames[s_AreaDataIndex]);
    raw = Assets_Load(rel, &raw_len);
    if (!raw || raw_len < 2 || raw_len > sizeof(s_AreaDataBuffer)) {
        fprintf(stderr, "Level_Load: failed %s\n", rel);
        g_AreaDataPtr = NULL;
        g_AreaDataLen = 0;
        return 0;
    }
    memcpy(s_AreaDataBuffer, raw, raw_len);

    /* GetAreaDataAddrs (src/level-load.asm:48-102) reads the header through
     * the original pointer, then advances AreaDataLow/High by two bytes.
     * AreaDataOffset remains zero: its first byte is the first object record,
     * not a file-header index. */
    Level_ParseHeader(s_AreaDataBuffer);
    g_AreaDataPtr = s_AreaDataBuffer + 2;
    g_AreaDataLen = (uint16_t)(raw_len - 2);
    g_AreaDataOffset = 0;
    return 1;
}

void Level_Load(uint8_t world, uint8_t area) {
    if (!ResolveAreaAddress(world, area)) {
        g_AreaDataPtr = NULL;
        g_AreaDataLen = 0;
        fprintf(stderr, "Level_Load: invalid AreaNumber %u for WorldNumber %u\n",
                area, world);
        return;
    }
    (void)LoadResolvedArea();
}

/* LoadAreaPointer (src/level-load.asm:1-11) selects the packed area pointer
 * and derives AreaType, but does not call GetAreaDataAddrs or parse the new
 * header.  Transition callers need this narrower boundary so the previous
 * header remains live until the next InitializeArea pass. */
int Level_SelectAreaPointer(uint8_t world, uint8_t area) {
    uint8_t pointer;

    if (!FindAreaPointer(world, area, &pointer))
        return 0;
    g_AreaPointer = pointer;
    g_AreaType = (uint8_t)((pointer & 0x60) >> 5);
    return 1;
}

/* GetAreaDataAddrs consumes the already selected packed AreaPointer for an
 * entrance transition; it does not rederive it from the HUD level number.
 * This is the C boundary used after HandlePipeEntry and for row-$0e enemy
 * stream area changes. */
int Level_LoadAreaPointer(uint8_t pointer) {
    if (!ResolveAreaPointer(pointer, g_WorldNumber, g_AreaNumber)) {
        g_AreaDataPtr = NULL;
        g_AreaDataLen = 0;
        fprintf(stderr, "Level_Load: invalid packed AreaPointer $%02x\n",
                pointer);
        return 0;
    }
    return LoadResolvedArea();
}

/* NextArea increments the variable-length WorldNAreas sequence before the
 * game-mode task boundary.  The ROM's LoadAreaPointer only selects and
 * decodes the packed pointer; GetAreaDataAddrs parses the new header later,
 * from InitializeArea on the next game-mode task. */
int Level_AdvanceArea(void) {
    uint8_t next = (uint8_t)(g_AreaNumber + 1);
    uint8_t pointer;

    /* NextArea increments AreaNumber before LoadAreaPointer. */
    g_AreaNumber = next;
    if (!FindAreaPointer(g_WorldNumber, next, &pointer))
        return 0;

    /* LoadAreaPointer (src/level-load.asm:23-37) stops after FindAreaPointer
     * and GetAreaType.  ResolveAreaPointer also performs the later
     * GetAreaDataAddrs hard-mode gate, so selecting it here would write
     * SecondaryHardMode one NMI too early. */
    g_AreaPointer = pointer;
    g_AreaType = (uint8_t)((pointer & 0x60) >> 5);

    /* The ROM increments FetchNewGameTimerFlag only after the packed pointer
     * has been selected.  ChgAreaMode, HalfwayPage, and Silence belong to the
     * caller's shared NextArea boundary rather than this pointer selector. */
    g_FetchNewGameTimerFlag++;
    return 1;
}

/* HandlePipeEntry's warp branch (main.asm:9555-9581).  The table entry is
 * converted from the displayed one-based world number to the zero-based
 * WorldNumber RAM value before selecting that world's area-zero pointer. */
int Level_SelectWarpZone(uint8_t warp_control, uint8_t player_x) {
    uint8_t pipe = 0;
    uint8_t raw_world;
    uint8_t world;

    if (player_x >= 0x60) pipe++;
    if (player_x >= 0xa0) pipe++;
    raw_world = WarpZoneNumbers[(uint8_t)((warp_control & 0x03) * 4 + pipe)];
    if (raw_world == 0 || raw_world == 0x24)
        return 0; /* unused/blank displayed pipe has no valid area target */

    world = (uint8_t)(raw_world - 1);
    if (world >= 8 || !FindAreaPointer(world, 0, &g_AreaPointer))
        return 0;

    g_WorldNumber = world;
    g_AreaNumber = 0;
    g_LevelNumber = 0;
    g_EntrancePage = 0;
    g_AltEntranceControl = 0;
    g_Hidden1UpFlag++;
    /* HandlePipeEntry's warp branch writes these symbols in the active
     * player record ($075a-$0760), not in independent storage.  Keep the
     * scalar views above for translated consumers, then publish the exact
     * physical owners used by the canonical state projection. */
    g_OnscreenPlayerInfo[5] = g_WorldNumber;   /* WorldNumber=$075f */
    g_OnscreenPlayerInfo[6] = g_AreaNumber;    /* AreaNumber=$0760 */
    g_OnscreenPlayerInfo[2] = g_LevelNumber;   /* LevelNumber=$075c */
    g_OnscreenPlayerInfo[3] = g_Hidden1UpFlag; /* Hidden1UpFlag=$075d */
    g_FetchNewGameTimerFlag++;
    g_EventMusicQueue = Silence;
    /* HandlePipeEntry leaves the old AreaData pointer live.  GetAreaDataAddrs
     * consumes the newly selected AreaPointer only from InitializeArea after
     * the ChangeAreaTimer/VerticalPipeEntry boundary (main.asm:9537-9585). */
    return 1;
}

void Level_ParseHeader(const uint8_t* data) {
    uint8_t b0 = data[0];
    uint8_t b1 = data[1];

    /* Byte 0: %TTPEEFFF
     * TT (7-6): Game timer setting
     * PE (5-3): Player entrance control
     * FF (2-0): Foreground scenery (0-3) or bg color control (4-7)
     */
    g_GameTimerSetting = (b0 >> 6) & 0x03;
    g_PlayerEntranceCtrl = (b0 >> 3) & 0x07;
    uint8_t ff = b0 & 0x07;
    if (ff >= 4) {
        g_BackgroundColorCtrl = ff;
        g_ForegroundScenery = 0;
    } else {
        g_BackgroundColorCtrl = 0;
        g_ForegroundScenery = ff;
    }

    /* Byte 1: %SSBTTTT
     * SS (7-6): Area style / cloud override
     * BB (5-4): Background scenery
     * TTTT (3-0): Terrain control
     */
    uint8_t ss = (b1 >> 6) & 0x03;
    g_BackgroundScenery = (b1 >> 4) & 0x03;
    g_TerrainControl = b1 & 0x0F;

    if (ss == 3) {
        g_CloudTypeOverride = ss;
        g_AreaStyle = 0;
    } else {
        g_CloudTypeOverride = 0;
        g_AreaStyle = ss;
    }

    /* The caller publishes AreaData as a pointer past this header, matching
     * GetAreaDataAddrs; the parser's zero-page cursor therefore starts at
     * offset zero. */
    g_AreaDataOffset = 0;

    printf("Header: timer=%d entrance=%d foregnd=%d bgctrl=%d terrain=%d bgscn=%d style=%d\n",
           g_GameTimerSetting, g_PlayerEntranceCtrl, g_ForegroundScenery,
           g_BackgroundColorCtrl, g_TerrainControl, g_BackgroundScenery, g_AreaStyle);
}

/* ========================================================================
 * METATILE HELPERS
 * ======================================================================== */

uint8_t Level_GetTerrainMetatile(void) {
    if (g_AreaType == AREA_TYPE_WATER && g_WorldNumber == WORLD_8)
        return 0x62; /* World8 water branch, main.asm:1932-1940 */
    if (g_CloudTypeOverride != 0)
        return 0x88; /* CloudTypeOverride branch, main.asm:1941-1944 */
    return TerrainMetatiles[g_AreaType & 0x03];
}
