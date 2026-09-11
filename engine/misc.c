/* engine/misc.c - supported misc-object subset
 *
 * This file translates the air-bubble and whirlpool portions of main.asm.
 * The cannon/whirlpool object bank is shared exactly as in the NES RAM
 * overlay.  Platform/rope and vine constructors remain explicit deferred
 * branches at their parser/slot boundaries.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "misc.h"
#include "enemy/enemy.h"
#include "player/player.h"
#include "sprite-offsets.h"
#include "spr-object.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "assets.h"

#define BUBBLE_COUNT 3
#define CANNON_COUNT 6
#define WHIRLPOOL_COUNT 5

/* Bubble_MForceData ($b74b) and BubbleTimerData ($b74d) are two-byte
 * tables in the ROM.  SetupBubble is also reached from
 * Entrance_GameTimerSetup, immediately after GameRoutines' JumpEngine has
 * left the selected routine's high pointer byte in $0007.  The original
 * code indexes these tables with that unmasked byte; the ROM therefore
 * supplies the bytes between the tables and the following routines for the
 * entrance path.  Keep the complete $b74b..$b84c ROM window so that this
 * indexed read remains defined for every $0007 value, while ordinary
 * BubbleCheck still uses indexes 0 and 1. */
static uint8_t BubbleDataROM[258];

int Misc_LoadRom(void) {
    return Assets_Copy("compat-rom-windows/bubble.bin", BubbleDataROM,
                       sizeof(BubbleDataROM));
}


typedef struct {
    /* Bubble_PageLoc=$83, Bubble_X_Position=$9c,
     * Bubble_Y_HighPos=$cb, Bubble_Y_Position=$e4. */
    uint8_t page;
    uint8_t x;
    uint8_t y_high;
    uint8_t y;
    uint8_t y_mf_dummy;       /* Bubble_YMF_Dummy=$042c */
    uint8_t rel_x;            /* Bubble_Rel_XPos=$03b0 */
    uint8_t rel_y;            /* Bubble_Rel_YPos=$03bb */
    uint8_t offscreen_bits;   /* Bubble_OffscreenBits=$03d3 */
} BubbleSlot;

static BubbleSlot s_Bubbles[BUBBLE_COUNT];
/* Cannon_Offset=$046A, Cannon_PageLoc=$046B-$0470,
 * Cannon_X_Position=$0471-$0476, Cannon_Y_Position=$0477-$047C,
 * Cannon_Timer=$047D-$0482.  Hole_Empty aliases the same bytes as
 * Whirlpool_Offset/PageLoc/LeftExtent/Length/Flag. */
static MiscCannonRecord s_CannonWhirlpools[CANNON_COUNT];

static void bubble_object(BubbleSlot *bubble, SprObjectView *object) {
    *object = (SprObjectView){
        &bubble->page, &bubble->x, &bubble->y_high, &bubble->y,
        0, 0, 0, 0, &bubble->y_mf_dummy,
        &bubble->rel_x, &bubble->rel_y, &bubble->offscreen_bits
    };
}

void Misc_Reset(void) {
    /* InitializeMemory clears these object bytes.  Do not replace the
     * zero-initialized Bubble_Y_Position state with an invented sentinel. */
    memset(s_Bubbles, 0, sizeof(s_Bubbles));
    memset(s_CannonWhirlpools, 0, sizeof(s_CannonWhirlpools));
    g_WhirlpoolOffset = 0;
    g_WhirlpoolFlag = 0;
}

void Misc_GetBubbleState(MiscBubbleState states[BUBBLE_COUNT]) {
    unsigned i;

    for (i = 0; i < BUBBLE_COUNT; ++i) {
        states[i].page = s_Bubbles[i].page;
        states[i].x = s_Bubbles[i].x;
        states[i].y_high = s_Bubbles[i].y_high;
        states[i].y = s_Bubbles[i].y;
        states[i].y_mf_dummy = s_Bubbles[i].y_mf_dummy;
    }
}

/* The title-screen VRAM copy and clear loops reach the same physical bytes
 * as Bubble_YMF_Dummy.  Keep this write on the bubble owner so the detached
 * SDL VRAM buffer cannot hide a real NES RAM alias. */
void Misc_SetBubbleYmf(uint8_t slot, uint8_t value) {
    if (slot < BUBBLE_COUNT)
        s_Bubbles[slot].y_mf_dummy = value;
}

/* SetupBubble (main.asm:3308-3329).  The table index is the live $0007
 * scratch value, not necessarily the masked bit used by BubbleCheck. */
static void setup_bubble(BubbleSlot *bubble, uint8_t table_index) {
    uint16_t x;
    uint8_t carry = (uint8_t)(g_PlayerFacingDir & 0x01);
    uint8_t adder = carry ? 0x08 : 0x00;

    /* SetupBubble leaves the carry from `lsr PlayerFacingDir` live when
     * the right-facing $08 path reaches `adc Player_X_Position`.  The
     * resulting nine-pixel offset, and only that ADC carry, feed the page
     * byte's `adc #$00` (main.asm:3308-3321). */
    x = (uint16_t)g_Player_X_Position + adder + carry;
    bubble->x = (uint8_t)x;
    bubble->page = (uint8_t)(g_Player_PageLoc + (x >> 8));
    bubble->y = (uint8_t)(g_Player_Y_Position + 0x08);
    bubble->y_high = 0x01;
    g_AirBubbleTimer = BubbleDataROM[(unsigned)table_index + 2];
}

/* SetupBubble falls through into MoveBubl in the original routine.  MoveBubl
 * also reuses the same unmasked $07 table index. */
static void move_bubble(BubbleSlot *bubble, uint8_t table_index) {
    uint8_t force = BubbleDataROM[table_index];
    uint8_t old_force = bubble->y_mf_dummy;
    uint8_t borrow;

    bubble->y_mf_dummy = (uint8_t)(old_force - force);
    borrow = old_force < force;
    bubble->y = (uint8_t)(bubble->y - borrow);
    if (bubble->y < 0x20)
        bubble->y = 0xf8;
}

/* BubbleCheck (main.asm:3298-3344). */
static void bubble_check(BubbleSlot *bubble, uint8_t slot) {
    uint8_t random_bit = g_PseudoRandomBitReg[1 + slot] & 0x01;

    if (bubble->y == 0xf8) {
        if (g_AirBubbleTimer != 0)
            return;
        setup_bubble(bubble, random_bit);
    }
    move_bubble(bubble, random_bit);
}

/* DrawBubble (main.asm:11733-11749). */
static void draw_bubble(const BubbleSlot *bubble, uint8_t slot) {
    uint8_t base;

    if (g_Player_Y_HighPos != 0x01 ||
        (bubble->offscreen_bits & 0x08) != 0)
        return;
    base = SpriteOffset_Bubble(slot);
    g_SpriteData[base + 0] = bubble->rel_y;
    g_SpriteData[base + 1] = 0x74;
    g_SpriteData[base + 2] = 0x02;
    g_SpriteData[base + 3] = bubble->rel_x;
}

/* ProcAirBubbles (main.asm:3220-3231). */
void Misc_ProcAirBubbles(void) {
    int slot;

    if (g_AreaType != AREA_TYPE_WATER)
        return;
    for (slot = BUBBLE_COUNT - 1; slot >= 0; slot--) {
        BubbleSlot *bubble = &s_Bubbles[slot];
        SprObjectView object;
        SprScreenEdges edges;

        bubble_check(bubble, (uint8_t)slot);
        bubble_object(bubble, &object);
        SprObject_GetRelativePosition(&object, g_ScreenLeft_X_Pos);
        SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                                 g_ScreenLeft_X_Pos);
        bubble->offscreen_bits = SprObject_GetOffscreenBits(&object, &edges);
        draw_bubble(bubble, (uint8_t)slot);
    }
}

/* SetupBubble is called by Entrance_GameTimerSetup before the next engine
 * tick.  For the ordinary water-entry path, slot is the raw X restored by
 * GetPlayerColors from VRAM_Buffer1_Offset (colors.asm:56). */
void Misc_SetupEntranceBubble(uint8_t slot) {
    uint8_t table_index;

    /* SetupBubble's X register is a raw object-array offset.  Only the three
     * ordinary bubble slots are represented here; other offsets are physical
     * aliases owned by their corresponding ROM object path and remain an
     * explicit deferred branch rather than being silently remapped. */
    if (slot >= BUBBLE_COUNT || g_AreaType != AREA_TYPE_WATER)
        return;
    /* Entrance_GameTimerSetup calls SetupBubble without refreshing $07.
     * Preserve the raw JumpEngine pointer-high byte for both table reads. */
    table_index = g_ZeroPageScratch07;
    setup_bubble(&s_Bubbles[slot], table_index);
    move_bubble(&s_Bubbles[slot], table_index);
}

/* SetupBubble after Setup_Vine (game-timer-setup.asm:64-82) runs with X=$05.
 * It intentionally addresses the byte overlay described in
 * Enemy_SetEntranceBubbleAlias rather than allocating a fourth BubbleSlot.
 * `$07` is a shared zero-page scratch byte in the ROM.  DecodeAreaData
 * supplies its JumpEngine-family value and each handler's GetLrgObjAttrib
 * reloads the row before the handler returns; this entry reads the current
 * raw owner instead of deriving a TAS value. */
void Misc_SetupEntranceBubbleAlias(void) {
    uint8_t table_index = g_ZeroPageScratch07;
    uint8_t facing_carry = g_PlayerFacingDir & 0x01;
    uint8_t adder = facing_carry ? 0x08 : 0x00;
    uint16_t x_sum = (uint16_t)g_Player_X_Position + adder + facing_carry;
    uint8_t x_position = (uint8_t)x_sum;
    uint8_t page_loc = (uint8_t)(g_Player_PageLoc + (x_sum >> 8));
    uint8_t y_position = (uint8_t)(g_Player_Y_Position + 0x08);
    uint8_t old_force = g_EntranceBubbleYMF;
    uint8_t force = BubbleDataROM[table_index];
    uint8_t borrow = old_force < force;

    /* SetupBubble's timer write precedes its fall-through to MoveBubl. */
    g_AirBubbleTimer = BubbleDataROM[(unsigned)table_index + 2];
    g_EntranceBubbleYMF = (uint8_t)(old_force - force);
    y_position = (uint8_t)(y_position - borrow);
    if (y_position < 0x20)
        y_position = 0xf8;
    Enemy_SetEntranceBubbleAlias(x_position, page_loc, 0x01, y_position);
}

/* Hole_Empty (main.asm:3005-3031) registers the five-entry whirlpool ring
 * in the same bank later consumed by ProcessCannons. */
void Misc_RegisterWhirlpool(uint8_t page, uint8_t left, uint8_t length) {
    MiscCannonRecord *slot = &s_CannonWhirlpools[g_WhirlpoolOffset];

    slot->page = page;
    slot->x_or_left = left;
    slot->y_or_length = length;
    g_WhirlpoolOffset++;
    if (g_WhirlpoolOffset == WHIRLPOOL_COUNT)
        g_WhirlpoolOffset = 0;
}

/* BulletBillCannon (main.asm:2879-2907) advances the same offset through
 * six cannon records.  The parser supplies the current page/column/row, so
 * these are the GetAreaObjXPosition/GetAreaObjYPosition results rather than
 * renderer or screen coordinates. */
void Misc_RegisterCannon(uint8_t page, uint8_t x, uint8_t y) {
    MiscCannonRecord *slot = &s_CannonWhirlpools[g_WhirlpoolOffset];

    slot->page = page;
    slot->x_or_left = x;
    slot->y_or_length = y;
    g_WhirlpoolOffset++;
    if (g_WhirlpoolOffset == CANNON_COUNT)
        g_WhirlpoolOffset = 0;
}

const MiscCannonRecord *Misc_GetCannon(uint8_t slot) {
    if (slot >= CANNON_COUNT)
        return NULL;
    return &s_CannonWhirlpools[slot];
}

void Misc_SetCannonTimer(uint8_t slot, uint8_t value) {
    if (slot >= CANNON_COUNT)
        return;
    s_CannonWhirlpools[slot].timer = value;
    /* $047D is also Whirlpool_Flag.  Keep its public global mirror in sync
     * with the aliased first byte of Cannon_Timer. */
    if (slot == 0)
        g_WhirlpoolFlag = value;
}

/* ProcessWhirlpools/WhirlpoolActivate (main.asm:3415-3496). */
void Misc_ProcessWhirlpools(void) {
    uint16_t player;
    int slot;

    /* ProcessWhirlpools branches first on AreaType (main.asm:3415-3420).
     * Whirlpool_Flag aliases Cannon_Timer[0] at $047D, so the clear must not
     * run in non-water areas: doing so erases a pending Cannon_Timer. */
    if (g_AreaType != AREA_TYPE_WATER)
        return;
    g_WhirlpoolFlag = 0;
    s_CannonWhirlpools[0].timer = 0;
    if (g_TimerControl != 0)
        return;

    player = (uint16_t)(((uint16_t)g_Player_PageLoc << 8) |
                        g_Player_X_Position);
    for (slot = WHIRLPOOL_COUNT - 1; slot >= 0; slot--) {
        const MiscCannonRecord *whirlpool = &s_CannonWhirlpools[slot];
        uint16_t left;
        uint16_t right;
        uint16_t center;

        /* ProcessWhirlpools skips a record whose page byte is zero. */
        if (whirlpool->page == 0)
            continue;
        left = (uint16_t)(((uint16_t)whirlpool->page << 8) |
                          whirlpool->x_or_left);
        right = (uint16_t)(left + whirlpool->y_or_length);
        if (player < left || player > right)
            continue;

        center = (uint16_t)(left + (whirlpool->y_or_length >> 1));
        if ((g_FrameCounter & 0x01) != 0) {
            if (player > center) {
                player--;
            } else if ((g_Player_CollisionBits & 0x01) != 0) {
                player++;
            }
            g_Player_X_Position = (uint8_t)player;
            g_Player_PageLoc = (uint8_t)(player >> 8);
        }

        g_WhirlpoolFlag = 1;
        s_CannonWhirlpools[0].timer = 1;
        Player_ApplyWhirlpoolGravity();
        break;
    }
}
