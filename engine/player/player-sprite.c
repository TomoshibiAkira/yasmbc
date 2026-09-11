/* engine/player/player-sprite.c - PlayerGfxHandler / DrawSpriteObject */
#include "player/player.h"
#include "sprite-offsets.h"
#include "spr-object.h"
#include "assets.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include <string.h>

/* ========================================================================
 * SPRITE UPDATE
 * PlayerGfxHandler writes player OAM via Player_UpdateSpriteASM.
 * ======================================================================== */

static void Player_UpdateSpriteASM(void);

void Player_UpdateSprite(void) {
    Player_UpdateSpriteASM();
}

/* ========================================================================
 * ASM PLAYER GRAPHICS TABLE/DISPATCH
 *
 * PlayerGfxHandler, ProcessPlayerAction, HandleChangeSize, DrawPlayerLoop,
 * ChkForPlayerAttrib, and PlayerOffscreenChk are translated as one state
 * boundary.
 * ======================================================================== */

static uint8_t AsmPlayerGfxTblOffsets[16];
static uint8_t AsmPlayerGraphicsTable[0xd0];
static uint8_t AsmChangeSizeOffsetAdder[20];

void Player_UpdateOffscreenBits(void) {
    SprObjectView object = {
        &g_Player_PageLoc, &g_Player_X_Position, &g_Player_Y_HighPos,
        &g_Player_Y_Position, 0, 0, 0, 0, 0, 0, 0,
        &g_Player_OffscreenBits
    };
    SprScreenEdges edges;

    SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                             g_ScreenLeft_X_Pos);
    SprObject_GetOffscreenBits(&object, &edges);
}

static uint8_t Player_AsmGfxActionOffset(uint8_t action) {
    if (g_PlayerSize != PLAYER_SIZE_BIG) action = (uint8_t)(action + 8);
    return action;
}

static uint8_t Player_AsmCurrentAnimOffset(uint8_t action) {
    uint8_t index = Player_AsmGfxActionOffset(action);
    return (uint8_t)(AsmPlayerGfxTblOffsets[index] +
                     (uint8_t)(g_PlayerAnimCtrl << 3));
}

static uint8_t Player_AsmNonAnimated(uint8_t action) {
    g_PlayerAnimCtrl = 0;
    return AsmPlayerGfxTblOffsets[Player_AsmGfxActionOffset(action)];
}

static uint8_t Player_AsmAnimate(uint8_t action, uint8_t extent) {
    uint8_t offset = Player_AsmCurrentAnimOffset(action);

    if (g_PlayerAnimTimer == 0) {
        uint8_t next;
        g_PlayerAnimTimer = g_PlayerAnimTimerSet;
        next = (uint8_t)(g_PlayerAnimCtrl + 1);
        if (next >= extent) next = 0;
        g_PlayerAnimCtrl = next;
    }
    return offset;
}

static uint8_t Player_AsmSwimmingAction(void) {
    if (g_JumpSwimTimer != 0 || g_PlayerAnimCtrl != 0 ||
        (g_A_B_Buttons & BTN_A) != 0)
        return Player_AsmAnimate(1, 3);
    return Player_AsmCurrentAnimOffset(1);
}

static uint8_t Player_AsmProcessAction(void) {
    uint8_t action;

    if (g_Player_State == PLAYER_STATE_CLIMB) {
        action = 5;
        if (g_Player_Y_Speed == 0) return Player_AsmNonAnimated(action);
        return Player_AsmAnimate(action, 2);
    }
    if (g_Player_State == PLAYER_STATE_FALL)
        return Player_AsmCurrentAnimOffset(4);
    if (g_Player_State == PLAYER_STATE_JUMP) {
        if (g_SwimmingFlag) return Player_AsmSwimmingAction();
        return Player_AsmNonAnimated(g_CrouchingFlag ? 6 : 0);
    }

    if (g_CrouchingFlag) return Player_AsmNonAnimated(6);
    if (g_Player_X_Speed == 0 && g_Left_Right_Buttons == 0)
        return Player_AsmNonAnimated(2);

    if (g_Player_XSpeedAbsolute >= 0x09 &&
        (g_Player_MovingDir & g_PlayerFacingDir) == 0)
        return Player_AsmNonAnimated(3);
    return Player_AsmAnimate(4, 3);
}

static uint8_t Player_AsmChangeSize(void) {
    uint8_t frame = g_PlayerAnimCtrl;

    if ((g_FrameCounter & 0x03) == 0) {
        frame++;
        if (frame >= 0x0a) {
            frame = 0;
            g_PlayerChangeSizeFlag = 0;
        }
        g_PlayerAnimCtrl = frame;
    }

    if (g_PlayerSize == PLAYER_SIZE_BIG) {
        uint8_t adder = AsmChangeSizeOffsetAdder[frame];
        return (uint8_t)(AsmPlayerGfxTblOffsets[0x0f] + (adder << 3));
    }
    if (AsmChangeSizeOffsetAdder[(uint8_t)(frame + 0x0a)] != 0)
        return AsmPlayerGfxTblOffsets[0x09];
    return AsmPlayerGfxTblOffsets[0x01];
}

static void Player_AsmDrawRows(uint8_t rows) {
    uint8_t row;
    uint8_t oam = SpriteOffset_Player();
    uint8_t flip = (g_PlayerFacingDir & BTN_LEFT) ? 0x40 : 0x00;
    uint8_t attrs = (uint8_t)(g_Player_SprAttrib | flip);

    g_Player_Pos_ForScroll = g_Player_Rel_XPos;
    for (row = 0; row < rows; row++) {
        const uint8_t *tiles = &AsmPlayerGraphicsTable[
            (uint8_t)(g_PlayerGfxOffset + row * 2)];
        uint8_t *out = &g_SpriteData[oam + row * 8];

        out[0] = (uint8_t)(g_Player_Rel_YPos + row * 8);
        out[1] = flip ? tiles[1] : tiles[0];
        out[2] = attrs;
        out[3] = g_Player_Rel_XPos;
        out[4] = out[0];
        out[5] = flip ? tiles[0] : tiles[1];
        out[6] = attrs;
        out[7] = (uint8_t)(g_Player_Rel_XPos + 8);
    }
}

static void Player_AsmCheckAttrib(void) {
    uint8_t first;
    uint8_t last;
    uint8_t row;
    uint8_t oam = SpriteOffset_Player();

    if (g_GameEngineSubroutine == 0x0b || g_PlayerGfxOffset == 0xc8) {
        first = 2;
        last = 3;
    } else if (g_PlayerGfxOffset == 0x50 ||
               g_PlayerGfxOffset == 0xb8 ||
               g_PlayerGfxOffset == 0xc0) {
        first = 3;
        last = 3;
    } else {
        return;
    }

    for (row = first; row <= last; row++) {
        uint8_t *left = &g_SpriteData[oam + row * 8];
        uint8_t *right = left + 4;
        left[2] = (uint8_t)(left[2] & 0x3f);
        right[2] = (uint8_t)((right[2] & 0x3f) | 0x40);
    }
}

static void Player_AsmFireballThrow(void) {
    uint8_t old_timer;
    uint8_t rows;

    if (g_FireballThrowingTimer == 0) return;
    old_timer = g_FireballThrowingTimer;
    g_FireballThrowingTimer = 0;
    if (g_PlayerAnimTimer >= old_timer) return;
    g_FireballThrowingTimer = g_PlayerAnimTimer;
    g_PlayerGfxOffset = AsmPlayerGfxTblOffsets[7];
    rows = (g_Player_X_Speed != 0 || g_Left_Right_Buttons != 0) ? 3 : 4;
    Player_AsmDrawRows(rows);
}

static void Player_AsmSwimKick(void) {
    uint8_t oam = SpriteOffset_Player();
    uint8_t column = (g_PlayerFacingDir & BTN_RIGHT) ? 0 : 4;
    uint8_t tile = (uint8_t)(oam + 24 + column);

    if (g_SpriteData[tile + 1] == 0x48) return;
    g_SpriteData[tile + 1] =
        (g_PlayerSize == PLAYER_SIZE_BIG) ? 0x31 : 0x46;
}

static void Player_UpdateSpriteASM(void) {
    uint8_t offset;
    uint8_t allow_swim_kick = 0;

    /* NMI's MoveSpritesOffscreen owns the clear on the blink frame. */
    if (g_InjuryTimer != 0 && (g_FrameCounter & 1)) return;

    if (g_GameEngineSubroutine == 0x0b)
        offset = AsmPlayerGfxTblOffsets[0x0e];
    else if (g_PlayerChangeSizeFlag != 0)
        offset = Player_AsmChangeSize();
    else {
        offset = Player_AsmProcessAction();
        /* PlayerGfxHandler reaches SwimKT only from the normal
         * FindPlayerAction path.  PlayerKilled and DoChangeSize jump to
         * PlayerGfxProcessing and return after PlayerOffscreenChk. */
        allow_swim_kick = 1;
    }

    g_PlayerGfxOffset = offset;
    Player_AsmDrawRows(4);
    Player_AsmCheckAttrib();
    Player_AsmFireballThrow();
    SprObject_MaskPlayerOAM(g_SpriteData, SpriteOffset_Player() / 4,
                            g_Player_OffscreenBits);

    /* PlayerGfxHandler performs this replacement after the normal draw and
     * offscreen check; it is not a second physics or trajectory update. */
    if (allow_swim_kick && g_SwimmingFlag &&
        g_Player_State != PLAYER_STATE_GROUND &&
        (g_FrameCounter & 0x04) == 0)
        Player_AsmSwimKick();

}

/* ========================================================================
 * COLOR ROTATION
 * From main.asm: ColorRotation (line 699-743)
 * Cycles palette 3 color 1 every 8 frames for coin icon shining effect.
 * Writes the FULL palette 3 (4 bytes at $3F0C), replacing color 1
 * with a cycling value from ColorRotatePalette.
 * ======================================================================== */

extern uint8_t g_ColorRotateOffset;

static uint8_t ColorRotatePalette[6];
static uint8_t Palette3Data[4][4];

void Player_ColorRotation(void) {
    uint8_t x = g_VRAM_Buffer1_Offset;

    /* Skip if buffer is too full (> 0x31 bytes used) */
    if (x >= 0x31) return;

    /* Only run every 8th frame */
    if ((g_FrameCounter & 0x07) != 0) return;

    /* Write full palette 3 to VRAM buffer at current offset:
     * [$3F, $0C, $04, color0, color1, color2, color3, $00]
     */
    const uint8_t *base = Palette3Data[g_AreaType & 0x03];

    g_VRAM_Buffer1[x + 0] = 0x3F;          /* PPU address high */
    g_VRAM_Buffer1[x + 1] = 0x0C;          /* PPU address low ($3F0C) */
    g_VRAM_Buffer1[x + 2] = 0x04;          /* Control: literal, length 4 */
    g_VRAM_Buffer1[x + 3] = base[0];       /* Color 0 */
    g_VRAM_Buffer1[x + 4] = ColorRotatePalette[g_ColorRotateOffset]; /* Color 1 (cycling) */
    g_VRAM_Buffer1[x + 5] = base[2];       /* Color 2 */
    g_VRAM_Buffer1[x + 6] = base[3];       /* Color 3 */
    g_VRAM_Buffer1[x + 7] = 0x00;          /* Null terminator */

    /* Advance buffer offset for subsequent writes.
     * Do NOT set AddrCtrl here — the NES original doesn't.
     * vram_flush resets AddrCtrl to 0 each frame, so it processes from position 0.
     */
    g_VRAM_Buffer1_Offset = x + 7;

    /* Advance cycling offset */
    g_ColorRotateOffset++;
    if (g_ColorRotateOffset >= 6) {
        g_ColorRotateOffset = 0;
    }
}

int Player_LoadSpriteTables(void) {
    if (Assets_Copy("tables/player_gfx_tbl_offsets.bin", AsmPlayerGfxTblOffsets,
                    sizeof(AsmPlayerGfxTblOffsets)) ||
        Assets_Copy("tables/player_graphics.bin", AsmPlayerGraphicsTable,
                    sizeof(AsmPlayerGraphicsTable)) ||
        Assets_Copy("tables/change_size_offset_adder.bin", AsmChangeSizeOffsetAdder,
                    sizeof(AsmChangeSizeOffsetAdder)) ||
        Assets_Copy("tables/color_rotate_palette.bin", ColorRotatePalette,
                    sizeof(ColorRotatePalette)) ||
        Assets_Copy("tables/palette3.bin", Palette3Data, sizeof(Palette3Data)))
        return -1;
    return 0;
}

