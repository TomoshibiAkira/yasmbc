/* sprite-offsets.c - shared SprDataOffset/SpriteShuffler state
 *
 * The NES stores these offsets as one shared RAM pool at $06E4-$06FC.  The
 * first 15 bytes are initialized from DefaultSprOffsets; SpriteShuffler
 * rotates them in reverse order and derives Misc_SprDataOffset from entries
 * 5..7.  Consumers must obtain offsets here rather than owning independent
 * shuffle counters.
 */

#include "sprite-offsets.h"
#include "assets.h"
#include <string.h>

#define SPR_DATA_POOL_COUNT 25
#define DEFAULT_OFFSET_COUNT 15

static uint8_t s_SprDataOffset[SPR_DATA_POOL_COUNT];
static uint8_t s_SprShuffleAmtOffset;
/* SprDataOffset_Ctrl ($03ee) is shared by PlayerHeadCollision,
 * BlockObjectsCore, and FloateyNumbersRoutine. */
static uint8_t s_SprDataOffsetCtrl;

/* DefaultSprOffsets (main.asm:1396-1400), $06E4-$06F2. */
static uint8_t s_DefaultSprOffsets[DEFAULT_OFFSET_COUNT];
static uint8_t s_Sprite0Data[4];

int SpriteOffsets_LoadRom(void) {
    if (Assets_Copy("tables/default_spr_offsets.bin", s_DefaultSprOffsets,
                    sizeof(s_DefaultSprOffsets)) ||
        Assets_Copy("tables/sprite0.bin", s_Sprite0Data, sizeof(s_Sprite0Data)))
        return -1;
    return 0;
}

void SpriteOffsets_InstallSprite0(uint8_t *sprite_data) {
    sprite_data[0] = s_Sprite0Data[0];
    sprite_data[1] = s_Sprite0Data[1];
    sprite_data[2] = s_Sprite0Data[2];
    sprite_data[3] = s_Sprite0Data[3];
}

/* SprShuffleAmt (main.asm:1507-1512), selected by $06E0. */
static const uint8_t s_SprShuffleAmt[3] = {0x58, 0x48, 0x38};

static void set_misc_offsets(void) {
    int x = 8;
    int y;

    /* SetMiscOffset (main.asm:96-111).  Relative to pool base $06E4,
     * Misc_SprDataOffset is index 15; x values 8,5,2 write indices 21..23,
     * 18..20, and 15..17.  Runtime misc object offsets 5..8 therefore map
     * to pool indices 20..23, yielding the initial B8,C0,C8,D0 sequence. */
    for (y = 2; y >= 0; y--, x -= 3) {
        uint8_t value = s_SprDataOffset[5 + y];
        s_SprDataOffset[13 + x] = value;
        s_SprDataOffset[14 + x] = (uint8_t)(value + 0x08);
        s_SprDataOffset[15 + x] = (uint8_t)(value + 0x10);
    }
}

/* InitializeMemory clears the physical $06e4-$06fc pool and $03ee control
 * byte after the first page (main.asm:1536-1555).  This is separate from
 * SecondaryGameSetup, which installs DefaultSprOffsets later. */
void SpriteOffsets_ClearMemory(void) {
    memset(s_SprDataOffset, 0, sizeof(s_SprDataOffset));
    s_SprShuffleAmtOffset = 0;
    s_SprDataOffsetCtrl = 0;
}

void SpriteOffsets_Reset(void) {
    unsigned i;
    for (i = 0; i < SPR_DATA_POOL_COUNT; i++) s_SprDataOffset[i] = 0;
    for (i = 0; i < DEFAULT_OFFSET_COUNT; i++)
        s_SprDataOffset[i] = s_DefaultSprOffsets[i];
    s_SprShuffleAmtOffset = 0;
    s_SprDataOffsetCtrl = 0;
    /* SecondaryGameSetup writes only DefaultSprOffsets.  SetMiscOffset is
     * reached by the later SpriteShuffler call (main.asm:69-111), not by
     * the setup routine; the derived $06f3-$06fb bytes remain zero until
     * that call. */
}

void SpriteOffsets_Shuffle(void) {
    int i;
    uint8_t amount = s_SprShuffleAmt[s_SprShuffleAmtOffset];

    /* SpriteShuffler/ShuffleLoop (main.asm:73-95). */
    for (i = DEFAULT_OFFSET_COUNT - 1; i >= 0; i--) {
        uint16_t sum;
        if (s_SprDataOffset[i] < 0x28) continue;
        sum = (uint16_t)s_SprDataOffset[i] + amount;
        if (sum > 0xFF) sum += 0x28;
        s_SprDataOffset[i] = (uint8_t)sum;
    }
    s_SprShuffleAmtOffset++;
    if (s_SprShuffleAmtOffset == 3) s_SprShuffleAmtOffset = 0;
    set_misc_offsets();
}

uint8_t SpriteOffsets_GetControl(void) {
    return (uint8_t)(s_SprDataOffsetCtrl & 0x01);
}

void SpriteOffsets_SetControl(uint8_t control) {
    s_SprDataOffsetCtrl = (uint8_t)(control & 0x01);
}

void SpriteOffsets_GetVerifierState(uint8_t *shuffle_offset,
                                    uint8_t *control,
                                    uint8_t offsets[SPR_DATA_POOL_COUNT]) {
    if (shuffle_offset) *shuffle_offset = s_SprShuffleAmtOffset;
    if (control) *control = s_SprDataOffsetCtrl;
    if (offsets) memcpy(offsets, s_SprDataOffset, SPR_DATA_POOL_COUNT);
}

uint8_t SpriteOffset_Player(void) { return s_SprDataOffset[0]; }

uint8_t SpriteOffset_Enemy(uint8_t slot) {
    return (slot < 6) ? s_SprDataOffset[1 + slot] : s_SprDataOffset[1];
}

uint8_t SpriteOffset_Fireball(uint8_t slot) {
    /* FBall_SprDataOffset=$06F1/$06F2, pool entries 13/14. */
    return (slot < 2) ? s_SprDataOffset[13 + slot] : s_SprDataOffset[13];
}

uint8_t SpriteOffset_FireballExplosion(uint8_t slot) {
    /* Alt_SprDataOffset=$06EC/$06ED, pool entries 8/9.  The apparent
     * overlap with block offsets is intentional in the original shared pool. */
    return (slot < 2) ? s_SprDataOffset[8 + slot] : s_SprDataOffset[8];
}

uint8_t SpriteOffset_Alt(uint8_t slot) {
    /* DrawExplosion_Fireworks indexes Alt_SprDataOffset ($06EC) by the
     * current ObjectOffset, so its six entries alias the shared pool rather
     * than the two fireball-only entries above. */
    return (slot < 6) ? s_SprDataOffset[8 + slot] : s_SprDataOffset[8];
}

uint8_t SpriteOffset_Bubble(uint8_t slot) {
    /* Bubble_SprDataOffset=$06EE (main.asm:1396-1400). */
    return (slot < 3) ? s_SprDataOffset[10 + slot] : s_SprDataOffset[10];
}

uint8_t SpriteOffset_Block(uint8_t slot) {
    return (slot < 2) ? s_SprDataOffset[8 + slot] : s_SprDataOffset[8];
}

uint8_t SpriteOffset_MiscRaw(uint8_t slot) {
    /* Misc_SprDataOffset=$06f3 is indexed by the raw Misc_State offset
     * $00..$08.  The four ordinary coin callers below use raw slots $05..$08
     * but hammers can use the full bank. */
    return (slot < 9) ? s_SprDataOffset[15 + slot] : s_SprDataOffset[15];
}

uint8_t SpriteOffset_Misc(uint8_t slot) {
    return (slot < 4) ? SpriteOffset_MiscRaw((uint8_t)(slot + 5))
                      : SpriteOffset_MiscRaw(5);
}

uint8_t SpriteOffset_Floaty(void) {
    /* FloateyNumbersRoutine indexes Alt_SprDataOffset ($06ec) with the
     * shared SprDataOffset_Ctrl byte ($03ee). */
    return s_SprDataOffset[8 + (s_SprDataOffsetCtrl & 0x01)];
}
