#include <stdint.h>
#include "constants/defs.h"
#include "constants/globals.h"
#include "enemy/enemy-internal.h"
#include "sprite-offsets.h"
#include "score.h"
#include "level/level.h"
#include "assets.h"

/* Enemy drawing. BoundBoxCtrlData (main.asm:10066-10078), indexed by
 * Enemy_BoundBoxCtrl. */
uint8_t s_BoundBoxCtrlData[12][4];
static uint8_t s_EnemyAnimTimingBMask[2];
static uint8_t s_EnemyGraphicsTable[0x102];
static uint8_t s_EnemyGfxTableOffsets[0x1b];
static uint8_t s_EnemyAttributeData[0x1b];
static uint8_t s_JumpspringFrameOffsets[5];
static uint8_t powerup_gfx[16];
static uint8_t powerup_attrs[4];
static uint8_t s_FlagpoleScoreNumTiles[10];
uint8_t s_FlagpoleScoreMods[5];
uint8_t s_FlagpoleScoreDigits[5];
uint8_t s_BridgeCollapseData[15];
static uint8_t StarFlagYPosAdder[4];
static uint8_t StarFlagXPosAdder[4];
static uint8_t StarFlagTileData[4];

int Enemy_LoadDrawTables(void) {
    if (Assets_Copy("tables/bound_box_ctrl.bin", s_BoundBoxCtrlData,
                    sizeof(s_BoundBoxCtrlData)) ||
        Assets_Copy("tables/enemy_graphics.bin", s_EnemyGraphicsTable,
                    sizeof(s_EnemyGraphicsTable)) ||
        Assets_Copy("tables/enemy_gfx_tbl_offsets.bin", s_EnemyGfxTableOffsets,
                    sizeof(s_EnemyGfxTableOffsets)) ||
        Assets_Copy("tables/enemy_attribute.bin", s_EnemyAttributeData,
                    sizeof(s_EnemyAttributeData)) ||
        Assets_Copy("tables/jumpspring_frame_offsets.bin",
                    s_JumpspringFrameOffsets,
                    sizeof(s_JumpspringFrameOffsets)) ||
        Assets_Copy("tables/powerup_gfx.bin", powerup_gfx, sizeof(powerup_gfx)) ||
        Assets_Copy("tables/powerup_attributes.bin", powerup_attrs,
                    sizeof(powerup_attrs)) ||
        Assets_Copy("tables/flagpole_score_tiles.bin", s_FlagpoleScoreNumTiles,
                    sizeof(s_FlagpoleScoreNumTiles)) ||
        Assets_Copy("tables/flagpole_score_mods.bin", s_FlagpoleScoreMods,
                    sizeof(s_FlagpoleScoreMods)) ||
        Assets_Copy("tables/flagpole_score_digits.bin", s_FlagpoleScoreDigits,
                    sizeof(s_FlagpoleScoreDigits)) ||
        Assets_Copy("tables/bridge_collapse.bin", s_BridgeCollapseData,
                    sizeof(s_BridgeCollapseData)) ||
        Assets_Copy("tables/star_flag_y_adder.bin", StarFlagYPosAdder,
                    sizeof(StarFlagYPosAdder)) ||
        Assets_Copy("tables/star_flag_x_adder.bin", StarFlagXPosAdder,
                    sizeof(StarFlagXPosAdder)) ||
        Assets_Copy("tables/star_flag_tiles.bin", StarFlagTileData,
                    sizeof(StarFlagTileData)) ||
        Assets_Copy("tables/enemy_anim_timing.bin", s_EnemyAnimTimingBMask,
                    sizeof(s_EnemyAnimTimingBMask)))
        return -1;
    return 0;
}


/* DrawSpriteObject (main.asm:12388-12426) writes one shared row contract:
 * tile order is swapped when MovingDir d1 is set, the H-flip bit is attached
 * to both row attributes, X wraps as an 8-bit add, and each subsequent row is
 * eight bytes later.  EnemyGfxHandler's FlipEnemyVertically then exchanges
 * either rows 1/2 or rows 0/2 depending on the object class. */
void draw_enemy_rows(const EnemySlot* e, uint8_t sprite_base,
                            const uint8_t* gfx, uint8_t base_y,
                            uint8_t moving_dir, const uint8_t attrs[6],
                            uint8_t row_flip_mode, uint8_t offscreen_bits) {
    uint8_t row;
    uint8_t col;

    for (row = 0; row < 3; row++) {
        uint8_t source_row = row;
        if (row_flip_mode == ENEMY_ROWS_SWAP_MIDDLE) {
            if (row == 1) source_row = 2;
            else if (row == 2) source_row = 1;
        } else if (row_flip_mode == ENEMY_ROWS_SWAP_OUTER) {
            source_row = (uint8_t)(2 - row);
        }
        for (col = 0; col < 2; col++) {
            uint8_t source_col = (moving_dir & 0x02) ?
                (uint8_t)(1 - col) : col;
            uint8_t* oam = &g_SpriteData[sprite_base +
                                         (row * 2 + col) * 4];
            oam[0] = (uint8_t)(base_y + row * 8);
            oam[1] = gfx[source_row * 2 + source_col];
            oam[2] = attrs[row * 2 + col];
            oam[3] = (uint8_t)(e->rel_x + col * 8);
        }
    }
    SprObject_MaskEnemyOAM(g_SpriteData, sprite_base / 4, offscreen_bits);
}

/* DrawVine/SixSpriteStacker (main.asm:10414-10474).  Each vine segment
 * owns six vertically stacked sprites at the shared enemy offset. */
void draw_vine_segment(const EnemySlot *e, uint8_t slot,
                              uint8_t segment) {
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t y = (uint8_t)(e->rel_y + (segment ? 0x30 : 0x00));
    uint8_t i;

    for (i = 0; i < 6; i++) {
        uint8_t *oam = &g_SpriteData[base + i * 4];
        uint8_t delta = (uint8_t)(g_VineStartY - y);

        oam[0] = y;
        oam[1] = (segment == 0 && i == 0) ? 0xe0 : 0xe1;
        oam[2] = (i & 1) ? 0x61 : 0x21;
        oam[3] = (uint8_t)(e->rel_x + ((i & 1) ? 0x06 : 0x00));
        if (delta >= 0x64) oam[0] = 0xf8;
        y = (uint8_t)(y + 0x08);
    }
}

/* FloateyNumbersRoutine and DrawFloateyNumber (main.asm:176-255,
 * 10740-10787).  The table is FloateyNumTileData at main.asm:155. */

void draw_floaty(EnemySlot* e, uint8_t oam_byte_offset) {
    extern uint8_t g_SpriteData[256];
    uint8_t timer;
    uint8_t y;
    uint8_t* oam;

    /* FloateyNumbersRoutine clamps all controls >= $0b before indexing the
     * ROM tables (main.asm:176-184). */
    if (e->floaty_control >= 0x0b) e->floaty_control = 0x0b;
    if (e->floaty_control == 0 || e->floaty_timer == 0) {
        e->floaty_control = 0;
        return;
    }
    timer = e->floaty_timer;
    e->floaty_timer--;
    /* FloateyNumbersRoutine loads A from FloateyNum_Timer before DEC.  Its
     * CMP #$2b therefore sees the old timer value, not the decremented byte
     * (main.asm:185-208). */
    if (timer == 0x2b && e->floaty_control < 0x0c) {
        Score_ProcessFloateyReward(e->floaty_control);
    }
    /* FloateyNumbersRoutine's CMP #$18 carry reaches the later SBC #$08:
     * after the >=$18 path's SBC #$01, the visible coordinate is
     * (Y-1)-8; on the below-$18 path the clear carry makes it Y-9. */
    if (e->floaty_y >= 0x18) {
        e->floaty_y--;
        y = (uint8_t)(e->floaty_y - 8);
    } else {
        y = (uint8_t)(e->floaty_y - 9);
    }
    oam = &g_SpriteData[oam_byte_offset];
    oam[0] = y;
    oam[1] = Score_FloateyNumTileData[e->floaty_control * 2];
    oam[2] = 0x02;
    oam[3] = e->floaty_x;
    oam[4] = y;
    oam[5] = Score_FloateyNumTileData[e->floaty_control * 2 + 1];
    oam[6] = 0x02;
    oam[7] = (uint8_t)(e->floaty_x + 8);
}

/* FloateyNumbersRoutine's ChkTallEnemy selector (main.asm:209-230) first
 * obtains Enemy_SprDataOffset, then keeps it for Spiny, Piranha Plant, both
 * Cheep-Cheeps, and low-ID objects whose state is at least $02.  Hammer Bro,
 * tall/special IDs, and active low-ID objects use
 * Alt_SprDataOffset[SprDataOffset_Ctrl]. */
uint8_t floaty_oam_offset(const EnemySlot* e, int slot) {
    /* ChkTallEnemy sends Hammer Bro to GetAltOffset before the explicit
     * special-ID cases.  Those special IDs then retain Enemy_SprDataOffset;
     * only the later TallEnemy comparison selects the alternate bank. */
    if (e->id == HammerBro)
        return SpriteOffset_Floaty();
    if (e->id == Spiny || e->id == PiranhaPlant ||
        e->id == GreyCheepCheep || e->id == RedCheepCheep ||
        (e->id < TallEnemy && e->state >= 0x02))
        return SpriteOffset_Enemy((uint8_t)slot);
    if (e->id >= TallEnemy)
        return SpriteOffset_Floaty();
    return SpriteOffset_Floaty();
}

/* DrawExplosion_Fireworks (main.asm:11623-11662). */
void draw_fireworks_explosion(const EnemySlot* e, uint8_t slot,
                                     uint8_t animation) {
    /* RunFireworks (main.asm:7602) loads Enemy_SprDataOffset,x before
     * DrawExplosion_Fireworks; the callee consumes that Y unchanged.  The
     * alternate bank belongs to DrawExplosion_Fireball (main.asm:11615),
     * not to the fireworks object. */
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t tile = s_ExplosionTiles[animation];
    uint8_t *oam = &g_SpriteData[base];

    oam[0] = (uint8_t)(e->rel_y - 0x04);
    oam[1] = tile;
    oam[2] = 0x02;
    oam[3] = (uint8_t)(e->rel_x - 0x04);
    oam[4] = (uint8_t)(e->rel_y + 0x04);
    oam[5] = tile;
    oam[6] = 0x82;
    oam[7] = (uint8_t)(e->rel_x - 0x04);
    oam[8] = (uint8_t)(e->rel_y - 0x04);
    oam[9] = tile;
    oam[10] = 0x42;
    oam[11] = (uint8_t)(e->rel_x + 0x04);
    oam[12] = (uint8_t)(e->rel_y + 0x04);
    oam[13] = tile;
    oam[14] = 0xc2;
    oam[15] = (uint8_t)(e->rel_x + 0x04);
}

void draw_star_flag(const EnemySlot* e, uint8_t slot) {
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t i;

    for (i = 0; i < 4; i++) {
        uint8_t *oam = &g_SpriteData[base + i * 4];
        oam[0] = (uint8_t)(e->rel_y + StarFlagYPosAdder[i]);
        oam[1] = StarFlagTileData[i];
        oam[2] = 0x22;
        oam[3] = (uint8_t)(e->rel_x + StarFlagXPosAdder[i]);
    }
}

/* Draw one Goomba: the complete three-row EnemyGraphicsTable subset and its
 * state-dependent EnemyGfxHandler path (main.asm:10943-11270).  In
 * particular, stomp/revive state $04 uses the defeated $8a entry, while the
 * star-killed d5 state $22 keeps the normal entry and applies vertical flip. */
void draw_goomba(const EnemySlot* e, int slot_base) {
    const uint8_t *gfx = &s_EnemyGraphicsTable[s_EnemyGfxTableOffsets[GOOMBA_ID]];
    const uint8_t *defeated_gfx = &s_EnemyGraphicsTable[0x8a];
    uint8_t draw_dir = e->moving_dir;
    uint8_t base_y = e->rel_y;
    uint8_t base_attr = (uint8_t)(0x03 | e->spr_attrib);
    uint8_t attrs[6];
    uint8_t vertical_flip = 0;
    uint8_t alternate_state = 0;
    uint8_t i;

    /* EnemyGfxHandler's CheckForPodoboo stores the selected object code in
     * $ef before DrawEnemyObject (main.asm:11000). */
    g_ZeroPageScratchEF = GOOMBA_ID;

    /* GmbaAnim is gated by d5, TimerControl, and FrameCounter bit 3. */
    if (e->state < 0x02 && (e->state & 0x20) == 0 &&
        g_TimerControl == 0 && (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0) {
        draw_dir ^= 0x03;
    }

    if (e->state >= 0x02) {
        /* CheckForGoomba writes the graphics-only $EC alternate state for
         * every defeated/stunned state.  It is distinct from the persistent
         * Enemy_State byte: CheckForDefeatedState later clears $EC only for
         * d5 vertical-flip states. */
        alternate_state = 0x04;
        if (e->state & 0x20) {
            /* CheckForDefdGoomba leaves the normal table for d5 states;
             * CheckForDefdGoomba has already added two pixels to $02, then
             * DrawEnemyObject exchanges rows 1 and 2 and sets v-flip. */
            base_y = (uint8_t)(base_y + 2);
            vertical_flip = 1;
            alternate_state = 0;
        } else {
            /* The defeated entry is selected after the shell-state path has
             * added two pixels and then decremented $02 once. */
            base_y = (uint8_t)(base_y + 1);
            gfx = defeated_gfx;
        }
    }

    for (i = 0; i < 6; i++) {
        attrs[i] = (uint8_t)(base_attr |
                             ((draw_dir & 0x02) ? 0x40 : 0x00) |
                             (vertical_flip ? 0x80 : 0x00));
    }
    /* MirrorEnemyGfx (main.asm:11297-11320) handles the alternate enemy
     * state selected for a stomped Goomba after DrawEnemyObject.  Its $04
     * branch adds vertical flip to the middle and bottom rows while leaving
     * the defeated tile order intact. */
    if (alternate_state == 0x04) {
        attrs[2] = (uint8_t)((attrs[2] & 0xbf) | 0x80);
        attrs[3] = (uint8_t)((attrs[3] & 0x3f) | 0xc0);
        attrs[4] = (uint8_t)((attrs[4] & 0xbf) | 0x80);
        attrs[5] = (uint8_t)((attrs[5] & 0x3f) | 0xc0);
        /* MirrorEnemyGfx applies the same right-column H-flip to the
         * preceding row before its state-$04 vertical-flip additions. */
        attrs[1] = (uint8_t)((attrs[1] & 0x3f) | 0xc0);
    }
    /* slot_base is the six-row OAM index used by the old call boundary;
     * convert it to the shared SpriteData byte offset only here. */
    draw_enemy_rows(e, (uint8_t)(slot_base * 4), gfx, base_y, draw_dir,
                    attrs, vertical_flip ? ENEMY_ROWS_SWAP_MIDDLE :
                    ENEMY_ROWS_UNFLIPPED, s_Enemy_OffscreenBits);
}

/* SprObjectOffscrChk (main.asm:11384-11403) owns the final graphics-side
 * lifecycle check after EnemyGfxHandler.  The generic OAM mask above already
 * hides all rows for offscreen bit 7; the same branch retires an object that
 * has crossed into Y high page $02, except for Podoboo, which is deliberately
 * retained for its next launch. */
void enemy_graphics_offscreen_cleanup(EnemySlot *e) {
    /* SprObjectOffscrChk's lda Enemy_OffscreenBits is absolute; only the
     * subsequent Enemy_ID/Enemy_Y_HighPos checks use ,X. */
    if ((s_Enemy_OffscreenBits & 0x80) == 0 || e->id == Podoboo)
        return;
    if (e->y_high == 0x02)
        erase_enemy(e);
}

/* EnemyGraphicsTable (main.asm:10880-10904), kept contiguous at the ROM
 * offsets consumed by EnemyGfxTableOffsets.  The specialized enemy entries
 * through the jumpspring frames are retained here so EnemyGfxHandler can
 * select them by the original offset table rather than by an OAM substitute. */



/* JumpspringFrameOffsets (main.asm:10940-10941) selects the extended enemy
 * table entries $18,$19,$1a,$19,$18 for animation controls 0..4. */

/* EnemyGfxHandler's specialized entries (main.asm:10943-11310).  This is
 * still a three-row DrawEnemyObject, followed by the shared OAM mask.  The
 * mirror and vertical-row exchanges below are the ROM's handler branches for
 * Bloober, Piranha Plant, and Podoboo; Cheep-Cheep and Bullet Bill retain the
 * ordinary DrawSpriteObject direction handling. */
void draw_special_enemy(const EnemySlot* e, uint8_t slot) {
    uint8_t id = e->id;
    uint8_t gfx_offset = 0;
    /* EnemyGfxHandler reloads the object Y low byte here, after movement and
     * collision have run; it does not consume RelativeEnemyPosition's shared
     * scratch Y.  Enemy_Rel_XPos is the coordinate carried into $05. */
    uint8_t base_y = e->y;
    uint8_t draw_dir = e->moving_dir;
    uint8_t frame_timer = g_Timers[TIMER_ENEMY_FRAME_BASE + slot];
    uint8_t frame_offset = 0;
    uint8_t vertical_flip = 0;
    uint8_t mirror = 0;
    uint8_t attr = e->spr_attrib;
    uint8_t attrs[6];
    uint8_t row_flip_mode = ENEMY_ROWS_UNFLIPPED;
    uint8_t retainer_object = 0;
    uint8_t jumpspring_object = 0;
    uint8_t scratch_ef = id;
    uint8_t i;
    uint8_t sprite_base = SpriteOffset_Enemy(slot);

    if (id == HammerBro) {
        /* CheckForHammerBro/CheckToAnimateEnemy (main.asm:11140-11168):
         * state d3 selects the $b4 throw row, then the ordinary animation
         * gate advances that row by six bytes when FrameCounter d3 is clear.
         * A normal state uses the same gate from the $a8 base row; other
         * nonzero states jump directly to CheckDefeatedState. */
        gfx_offset = 0xa8;
        if (e->state == 0) {
            if (g_TimerControl == 0 && (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0)
                frame_offset = 0x06;
        } else if (e->state & 0x08) {
            gfx_offset = 0xb4;
            if ((e->state & 0xa0) == 0 && g_TimerControl == 0 &&
                (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0)
                frame_offset = 0x06;
        }
        attr = 0x01;
    } else if (id == Lakitu) {
        gfx_offset = 0x90;
        /* CheckForLakitu (main.asm:11119-11130) does not use the frame
         * counter.  The alternate $96 row is selected only while the
         * frenzy timer is below $10 and the object is not in the d5
         * defeated state; otherwise the base $90 row remains selected. */
        if ((e->state & 0x20) == 0 && g_FrenzyEnemyTimer < 0x10)
            frame_offset = 0x06;
        /* EnemyAttributeData[$11] is $01 for Lakitu (main.asm:10988),
         * before DrawEnemyObject adds the direction flip bit. */
        attr = 0x01;
        mirror = 1;
    } else if (id == Spiny) {
        gfx_offset = (e->state & 0x07) == 0x05 ? 0x30 : 0x24;
        attr = 0x02;
        mirror = (e->state & 0x07) == 0x05;
        if ((e->state & 0x07) == 0x05) {
            /* CheckForSpiny writes the graphics-local $03=$02 for the egg;
             * the final Enemy_MovingDir byte remains object-owned and is
             * updated later by EnemyToBGCollisionDet. */
            draw_dir = BTN_LEFT;
        }
        /* Both normal Spiny ($24) and the egg ($30) reach CheckToAnimateEnemy.
         * CheckForSecondFrame uses EnemyAnimTimingBMask[0]=$08; the second
         * six-byte table row is gated by TimerControl and enemy state d7/d5. */
        if ((e->state & 0xa0) == 0 && g_TimerControl == 0 &&
            (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0)
            frame_offset = 0x06;
    } else if (id == Bowser) {
        /* BowserGfxHandler -> ProcessBowserHalf increments BowserGfxFlag
         * before each EnemyGfxHandler call (main.asm:7459-7467).  The front
         * and rear halves have separate table entries: CheckBowserFront
         * selects $d2/$de from the mouth bit, while CheckBowserRear selects
         * $d8/$e4 from the feet bit (main.asm:11039-11078). */
        uint8_t bowser_gfx = s_Bowser.gfx_flag;
        if (bowser_gfx == 2) {
            gfx_offset = (s_Bowser.body_controls & 0x01) ? 0xe4 : 0xd8;
        } else {
            gfx_offset = (s_Bowser.body_controls & 0x80) ? 0xde : 0xd2;
        }
        /* CheckBowserFront/Rear indexes the shared EnemyAttributeData with
         * $16/$17; both entries are $01.  Bowser is not the ordinary ID-$1f
         * lookup, so the explicit $02 palette used by Spiny is not reused. */
        attr = (uint8_t)(s_EnemyAttributeData[
            (bowser_gfx == 2) ? 0x17 : 0x16] | e->spr_attrib);
    } else if (id == BulletBill_CannonVar) {
        /* CheckForBulletBillCV selects EnemyGraphicsTable+$ea directly;
         * this ID is outside the ordinary EnemyGfxTableOffsets table. */
        gfx_offset = 0xea;
        attr = 0x03;
    } else if (id == JumpspringObject) {
        uint8_t frame_id = s_JumpspringFrameOffsets[g_JumpspringAnimCtrl];

        /* CheckForJumpspring leaves $ef as the table ID, then the shared
         * EnemyAttributeData/EnemyGfxTableOffsets lookup selects $f0/$f6/$fc. */
        gfx_offset = s_EnemyGfxTableOffsets[frame_id];
        attr = (uint8_t)(s_EnemyAttributeData[frame_id] | e->spr_attrib);
        jumpspring_object = 1;
        scratch_ef = frame_id;
    } else if (id == RetainerObject) {
        /* CheckToAnimateEnemy keeps the $9c princess entry in world 8 and
         * selects the $a2 mushroom-retainer entry otherwise.  The handler
         * also forces MovingDir=$01 for this object, so the ordinary table
         * reader does not exchange the two source columns. */
        gfx_offset = (g_WorldNumber >= WORLD_8) ? 0x9c : 0xa2;
        attr = (uint8_t)(0x02 | e->spr_attrib);
        draw_dir = BTN_RIGHT;
        retainer_object = 1;
        /* CheckToAnimateEnemy selects the mushroom entry and sets $EC=$03
         * before CheckForESymmetry on Worlds 1-7; World 8 keeps the
         * princess entry with $EC=$00.  Thus MirrorEnemyGfx mirrors every
         * right column for the mushroom retainer, while the princess takes
         * only the dedicated bottom-right $42 write (main.asm:11219-11227,
         * 11287-11313). */
        mirror = (g_WorldNumber < WORLD_8);
        scratch_ef = 0x15;
    } else if (id >= sizeof(s_EnemyGfxTableOffsets)) {
        /* RunBowserFlame and firebar have dedicated graphics writers. */
        return;
    } else {
        gfx_offset = s_EnemyGfxTableOffsets[id];
        attr = (uint8_t)(s_EnemyAttributeData[id] | e->spr_attrib);
    }

    if (id == PiranhaPlant) {
        /* The plant remains hidden in its pipe while descending and its
         * movement timer is nonzero; rising plants are drawn. */
        if ((e->x_speed & 0x80) == 0 && frame_timer != 0) return;
        /* EnemyGfxHandler's CheckToAnimateEnemy reaches the Piranha entry
         * with state index zero.  EnemyAnimTimingBMask[0] is $08, and the
         * normal animation-stop gate selects the second six-byte entry. */
        if ((e->state & 0xa0) == 0 && g_TimerControl == 0 &&
            (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0)
            frame_offset = 0x06;
        mirror = 1;
    } else if (id == Bloober) {
        /* CheckForBloober uses EnemyIntervalTimer, not the frame timer. */
        if (e->interval_timer < 0x05 && e->interval_timer != 0x01) {
            base_y = (uint8_t)(base_y + 0x03);
            /* CheckAnimationStop receives the incremented draw Y, then
             * applies the saved-state/TimerControl gate only to the
             * animation-table advance (main.asm:11143-11168). */
            if ((e->state & 0xa0) == 0 && g_TimerControl == 0)
                frame_offset = 0x06;
        }
        mirror = 1;
    } else if (id == GreyCheepCheep || id == RedCheepCheep ||
               id == FlyCheepCheepFrenzy) {
        /* CheckForBloober compares the resolved graphics offset ($48), so
         * FlyingCheepCheepFrenzy ($14) takes the same CheckToAnimateEnemy /
         * CheckForSecondFrame path as the swimming Cheep-Cheeps. */
        if ((e->state & 0xa0) == 0 && g_TimerControl == 0 &&
            (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0)
            frame_offset = 0x06;
    } else if (id == Podoboo) {
        mirror = 1;
        if ((e->y_speed & 0x80) == 0) vertical_flip = 1;
    } else if (id == BulletBill_FrenzyVar) {
        /* CheckForBulletBillCV's DEC $02 is reached only for the cannon
         * variant ($33); the frenzy variant ($08) skips that decrement but
         * still gets the $03 attribute and EnemyFrameTimer priority bit. */
        attr = 0x03;
        if (frame_timer != 0) attr |= 0x20;
    } else if (id == BulletBill_CannonVar) {
        /* CheckForBulletBillCV decrements Y and supplies the priority bit
         * from EnemyFrameTimer after the direct graphics selection above. */
        base_y = (uint8_t)(base_y - 1);
        if (frame_timer != 0) attr |= 0x20;
    }

    /* CheckForPodoboo's STA $ef is the persistent scratch read by a later
     * zero-page alias, not an object coordinate.  Bowser's graphics branch
     * replaces it with the front/rear table code only when BowserGfxFlag is
     * set (main.asm:11006-11018). */
    if (id == Bowser && s_Bowser.gfx_flag != 0)
        scratch_ef = (s_Bowser.gfx_flag == 1) ? 0x16 : 0x17;
    g_ZeroPageScratchEF = scratch_ef;

    /* CheckForDefeatedState sets VerticalFlipFlag for d5 objects except the
     * Bullet Bill path, which jumps directly to SprObjectOffscrChk. */
    if ((e->state & 0x20) != 0 && id != BulletBill_FrenzyVar &&
        id != BulletBill_CannonVar)
        vertical_flip = 1;

    /* CheckBowserRear (main.asm:11064-11078) subtracts $10 from the saved
     * relative Y before DrawSpriteObject when the rear half is in its
     * defeated/vertical-flip state.  The generic flip flag below only sets
     * the PPU attribute; omitting this coordinate adjustment leaves all
     * three rear rows one tile too low (aknell-small-fire frame 25614). */
    if (id == Bowser && s_Bowser.gfx_flag == 2 && vertical_flip)
        base_y = (uint8_t)(base_y - 0x10);

    attr = (uint8_t)(attr | (draw_dir & 0x02 ? 0x40 : 0));
    if (vertical_flip) {
        /* FlipEnemyVertically exchanges rows 1/2 for ordinary enemy IDs,
         * but rows 0/2 for HammerBro, Lakitu, and IDs >= $15. */
        row_flip_mode = (id == HammerBro || id == Lakitu || id >= 0x15) ?
            ENEMY_ROWS_SWAP_OUTER : ENEMY_ROWS_SWAP_MIDDLE;
        attr |= 0x80;
    }
    for (i = 0; i < 6; i++) attrs[i] = attr;

    /* CheckForESymmetry's retainer branch runs after DrawEnemyObject and
     * overwrites only the bottom-right attribute with $42.  This is an OAM
     * result of the same EnemyGfxHandler path, not a fixed sprite slot or
     * coordinate substitute for the retainer object. */
    if (retainer_object)
        attrs[5] = 0x42;

    if (jumpspring_object) {
        /* CheckForESymmetry reaches MirrorEnemyGfx first for a Jumpspring
         * ($ef=$18, $ec=$03).  That pass strips any horizontal-flip bit from
         * the base attribute and applies it only to the top row's right
         * sprite.  CheckToMirrorJSpring then overwrites the two lower rows
         * with the ROM's fixed vertical-flip/palette values. */
        attrs[0] = (uint8_t)(attr & 0xa3);
        attrs[1] = (uint8_t)(attrs[0] | 0x40);
        attrs[2] = 0x82;
        attrs[3] = 0xc2;
        attrs[4] = 0x82;
        attrs[5] = 0xc2;
    }

    if (id == Lakitu) {
        /* CheckToMirrorLakitu (main.asm:11320-11352) is not the generic
         * MirrorEnemyGfx path.  With no vertical flip it edits the third row
         * first, then copies that pair to the second row only while the
         * frenzy timer is below $10.  After FlipEnemyVertically it edits the
         * first row instead.  The untouched rows retain DrawSpriteObject's
         * object-owned direction/palette bits. */
        if (!vertical_flip) {
            attrs[4] &= 0x81;
            attrs[5] |= 0x41;
            if (g_FrenzyEnemyTimer < 0x10) {
                attrs[3] = attrs[5];
                attrs[2] = attrs[4];
            }
        } else {
            attrs[0] &= 0x81;
            attrs[1] |= 0x41;
        }
    } else if (mirror) {
        /* MirrorEnemyGfx preserves vertical flip, priority and palette bits
         * in the left column and applies H-flip only to the right column. */
        uint8_t left_attr = (uint8_t)(attr & 0xa3);
        uint8_t right_attr = (uint8_t)(left_attr | 0x40);
        for (i = 0; i < 3; i++) {
            attrs[i * 2] = left_attr;
            attrs[i * 2 + 1] = right_attr;
        }
        if ((e->state & 0x1f) == 0x05) {
            /* EggExc adds vertical flip to the right column only. */
            attrs[1] |= 0x80;
            attrs[3] |= 0x80;
            attrs[5] |= 0x80;
        } else if ((e->state & 0x1f) == 0x04) {
            /* The right-side-up shell branch flips rows 1 and 2. */
            attrs[2] |= 0x80;
            attrs[3] |= 0x80;
            attrs[4] |= 0x80;
            attrs[5] |= 0x80;
        }
    }

    draw_enemy_rows(e, sprite_base,
                    &s_EnemyGraphicsTable[gfx_offset + frame_offset],
                    base_y, draw_dir, attrs, row_flip_mode,
                    s_Enemy_OffscreenBits);
}

/* EnemyGfxHandler's supported non-Goomba path (main.asm:10943-11220).
 * Shell presentation is selected from the low state byte: states $02/$03
 * use the upside-down entries, while state $04 uses the right-side-up
 * entries.  The state d5 vertical exchange is retained for paratroopa
 * defeat; d7 stops animation but does not replace the shell table. */
void draw_koopa_family(const EnemySlot* e, uint8_t slot) {
    uint8_t id = e->id;
    uint8_t low_state = e->state & 0x1f;
    uint8_t gfx_offset;
    uint8_t frame_offset = 0;
    /* EnemyGfxHandler reloads the object Y low byte here, after movement and
     * collision have run; it does not consume RelativeEnemyPosition's shared
     * scratch Y.  Enemy_Rel_XPos is the coordinate carried into $05. */
    uint8_t base_y = e->y;
    uint8_t draw_dir = e->moving_dir;
    uint8_t vertical_flip = 0;
    uint8_t attr;
    uint8_t attrs[6];
    uint8_t row_flip_mode = ENEMY_ROWS_UNFLIPPED;
    const uint8_t* gfx;
    uint8_t i;
    uint8_t sprite_base = SpriteOffset_Enemy(slot);

    if (id >= 0x15 || id == GOOMBA_ID) return;
    gfx_offset = s_EnemyGfxTableOffsets[id];
    /* EnemyGfxTableOffsets[$09] and [$10] both select the ROM $18
     * paratroopa graphics entry.  TallEnemy has no movement/BG consumer,
     * while GreenParatroopaFly is moved by MoveFlyGreenPTroopa; neither is
     * an unsupported graphics ID at this RunNormalEnemies boundary. */
    if (id != GreenKoopa && id != BuzzyBeetle && id != RedKoopa &&
        id != 0x01 && id != 0x04 && id != TallEnemy &&
        id != GreenParatroopaJump && id != RedParatroopa &&
        id != GreenParatroopaFly)
        return;

    /* The normal EnemyGfxHandler path leaves Enemy_ID in $ef through
     * CheckForPodoboo before consulting the graphics tables. */
    g_ZeroPageScratchEF = id;

    if (id < 0x04 && low_state >= 0x02) {
        if (low_state == 0x04) {
            gfx_offset = (id == BuzzyBeetle) ? 0x72 : 0x66;
            /* EnemyGfxHandler reaches both shell branches for state $04:
             * CheckUpsideDownShell increments $02 for Buzzy, then
             * CheckRightSideUpShell increments it again before selecting
             * the right-side-up table.  Koopa takes the same two-pixel
             * total through the non-Buzzy increment. */
            base_y = (uint8_t)(base_y + 2);
        } else {
            gfx_offset = (id == BuzzyBeetle) ? 0x7e : 0x5a;
            if (id == BuzzyBeetle) base_y++;
        }
    }

    /* CheckForBloober (main.asm:11143-11168) sends an ordinary enemy
     * through CheckDefeatedState while EnemyIntervalTimer is at least $05.
     * This gate precedes CheckForSecondFrame, so a shell's frame selection
     * must use the object-owned interval timer rather than the frame timer. */
    if (e->interval_timer < 0x05 && (e->state & 0xa0) == 0 &&
        g_TimerControl == 0 &&
        (g_FrameCounter & s_EnemyAnimTimingBMask[0]) == 0)
        frame_offset = 6;
    if ((e->state & 0x20) != 0 && id >= 0x04)
        vertical_flip = 1;

    gfx = &s_EnemyGraphicsTable[gfx_offset + frame_offset];
    attr = (uint8_t)(s_EnemyAttributeData[id] | e->spr_attrib |
                     (draw_dir & 0x02 ? 0x40 : 0x00));
    if (vertical_flip) {
        attr |= 0x80;
        row_flip_mode = ENEMY_ROWS_SWAP_MIDDLE;
    }
    for (i = 0; i < 6; i++) attrs[i] = attr;

    /* MirrorEnemyGfx (main.asm:11296-11313) owns shell symmetry after
     * DrawEnemyObject.  Alternate states $02/$03/$04 are not rendered with
     * the ordinary MovingDir attribute: the left column keeps only the
     * vertical-flip/priority/palette bits and the right column receives the
     * corresponding horizontal flip.  State $04 additionally flips both
     * lower rows vertically. */
    if (low_state >= 0x02) {
        uint8_t left_attr = (uint8_t)(attr & 0xa3);
        uint8_t right_attr = (uint8_t)(left_attr | 0x40);
        for (i = 0; i < 3; i++) {
            attrs[i * 2] = left_attr;
            attrs[i * 2 + 1] = right_attr;
        }
        if (low_state == 0x05) {
            attrs[1] |= 0x80;
            attrs[3] |= 0x80;
            attrs[5] |= 0x80;
        } else if (low_state == 0x04) {
            attrs[2] |= 0x80;
            attrs[3] |= 0x80;
            attrs[4] |= 0x80;
            attrs[5] |= 0x80;
        }
    }
    draw_enemy_rows(e, sprite_base, gfx, base_y, draw_dir, attrs,
                    row_flip_mode, s_Enemy_OffscreenBits);
}

void draw_large_platform(const EnemySlot *e, uint8_t slot) {
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t tile = Level_CloudTypeOverride() ? 0x75 : 0x5b;
    uint8_t platform_x_bits;
    uint8_t i;
    SprObjectView platform_object_view = platform_object((EnemySlot *)e);
    SprScreenEdges edges;

    /* DrawLargePlatform (main.asm:10681-10732) writes the six rows, then
     * increments X and calls GetXOffscreenBits before rotating that raw
     * table byte into the six per-sprite Y masks.  Enemy_X_Position begins
     * one byte after the generic SprObject_X_Position base, so INX selects
     * the current enemy's generic object fields.  Preserve that original
     * object-owned alias; the packed Enemy_OffscreenBits byte below remains
     * the separate whole-object visibility gate. */
    SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                             g_ScreenLeft_X_Pos);
    platform_x_bits = SprObject_GetRawXOffscreenBits(&platform_object_view,
                                                     &edges);
    for (i = 0; i < 6; i++) {
        uint8_t *oam = &g_SpriteData[base + i * 4];
        oam[0] = (i >= 4 &&
                  (g_AreaType == AREA_TYPE_CASTLE || g_SecondaryHardMode))
                     ? 0xf8 : e->y;
        oam[1] = tile;
        oam[2] = 0x02;
        oam[3] = (uint8_t)(e->rel_x + i * 8);
        if ((platform_x_bits & (uint8_t)(0x80 >> i)) != 0 ||
            (e->offscreen_bits & 0x80) != 0)
            oam[0] = 0xf8;
    }
}

void draw_small_platform(const EnemySlot *e, uint8_t slot) {
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t i;

    for (i = 0; i < 6; i++) {
        uint8_t *oam = &g_SpriteData[base + i * 4];
        uint8_t column = (uint8_t)(i % 3);
        uint8_t y = (i < 3) ? e->y : (uint8_t)(e->y + 0x80);
        uint8_t mask = (uint8_t)(0x08 >> column);

        if (y < 0x20 || (e->offscreen_bits & mask)) y = 0xf8;
        oam[0] = y;
        oam[1] = 0x5b;
        oam[2] = 0x02;
        oam[3] = (uint8_t)(e->rel_x + column * 8);
    }
}

/* DrawPowerUp (main.asm:10803-10862).  Power-ups use the reserved enemy
 * slot's shuffled sprite offset and the ROM-derived four-entry graphics and
 * attribute tables.  The generic SprObjectOffscrChk masking is retained,
 * including its reserved third-row writes, because the 6502 routine shares
 * the enemy mask helper rather than inventing a two-row visibility rule. */
void draw_powerup(const EnemySlot* e) {
    extern uint8_t g_SpriteData[256];
    uint8_t type = s_PowerUpType & 0x03;
    uint8_t attrs = (uint8_t)(powerup_attrs[type] | e->spr_attrib);
    uint8_t base_y = (uint8_t)(e->rel_y + 0x08);
    uint8_t base_x = e->rel_x;
    uint8_t sprite_base = SpriteOffset_Enemy(POWERUP_SLOT);
    uint8_t gfx_base = (uint8_t)(type << 2);
    uint8_t row;

    for (row = 0; row < 2; row++) {
        uint8_t* left = &g_SpriteData[sprite_base + row * 8];
        uint8_t* right = left + 4;
        left[0] = (uint8_t)(base_y + row * 8);
        left[1] = powerup_gfx[gfx_base + row * 2];
        left[2] = attrs;
        left[3] = base_x;
        right[0] = left[0];
        right[1] = powerup_gfx[gfx_base + row * 2 + 1];
        right[2] = attrs;
        right[3] = (uint8_t)(base_x + 8);
    }

    /* Fire flower and star palettes animate every two frames.  The flower
     * keeps the ROM's base palette on its lower row; star updates both rows.
     * Both types mirror the right-hand sprites through H-flip, exactly as
     * FlipPUpRightSide does. */
    if (type == 1 || type == 2) {
        uint8_t animated = (uint8_t)(((g_FrameCounter >> 1) & 0x03) |
                                     e->spr_attrib);
        g_SpriteData[sprite_base + 0 * 4 + 2] = animated;
        g_SpriteData[sprite_base + 4 + 2] = (uint8_t)(animated | 0x40);
        if (type == 2) {
            g_SpriteData[sprite_base + 8 + 2] = animated;
            g_SpriteData[sprite_base + 12 + 2] =
                (uint8_t)(animated | 0x40);
        } else {
            g_SpriteData[sprite_base + 12 + 2] |= 0x40;
        }
    }

    SprObject_MaskEnemyOAM(g_SpriteData, sprite_base / 4,
                           s_Enemy_OffscreenBits);
}

void draw_firebar_segment(const EnemySlot* e, uint8_t base,
                                 uint8_t segment, uint8_t center_x,
                                 uint8_t center_y) {
    uint8_t horizontal;
    uint8_t vertical;
    uint8_t mirror;
    uint8_t x;
    uint8_t y;
    uint8_t *oam = &g_SpriteData[base];

    if (segment == 0xff) {
        /* ProcFirebar writes the center sprite before GetFirebarPosition;
         * the first DrawFbar iteration then looks up table row zero.  Use
         * $ff only as the C-side center sentinel so row zero remains a valid
         * lookup index (main.asm:6834-6845, 6853-6858). */
        x = center_x;
        y = center_y;
    } else {
        firebar_position(e, segment, &horizontal, &vertical, &mirror);
        x = (mirror & 1) ? (uint8_t)(center_x + horizontal) :
                           (uint8_t)(center_x - horizontal);
        mirror >>= 1;
        y = (mirror & 1) ? (uint8_t)(center_y + vertical) :
                           (uint8_t)(center_y - vertical);
    }
    if (firebar_coordinate_delta(x, center_x) >= 0x59 || center_y == 0xf8)
        y = 0xf8;
    oam[0] = y;
    oam[1] = (uint8_t)(0x64 ^ ((g_FrameCounter >> 2) & 1));
    oam[2] = (uint8_t)(0x02 | ((g_FrameCounter & s_EnemyAnimTimingBMask[0]) ? 0xc0 : 0));
    oam[3] = x;
    firebar_player_collision(e, x, y);
}

void draw_bowser_flame(const EnemySlot* e, uint8_t slot) {
    uint8_t base = SpriteOffset_Enemy(slot);
    uint8_t attr = (g_FrameCounter & 0x02) ? 0x82 : 0x02;
    uint8_t i;

    for (i = 0; i < 3; i++) {
        uint8_t *oam = &g_SpriteData[(uint8_t)(base + i * 4)];
        oam[0] = e->rel_y;
        oam[1] = (uint8_t)(0x51 + i);
        oam[2] = attr;
        oam[3] = (uint8_t)(e->rel_x + i * 8);
    }
    if (e->offscreen_bits & 0x08) g_SpriteData[base + 0] = 0xf8;
    if (e->offscreen_bits & 0x04) g_SpriteData[base + 4] = 0xf8;
    if (e->offscreen_bits & 0x02) g_SpriteData[base + 8] = 0xf8;
}

/* FlagpoleGfxHandler (main.asm:10578-10627).  The flag and its score use
 * the same shared Enemy_SprDataOffset slot; the six-sprite offscreen branch
 * is intentionally kept separate from the normal three-row enemy mask. */
void draw_flagpole(const EnemySlot *e) {
    uint8_t base = SpriteOffset_Enemy(POWERUP_SLOT);
    uint8_t x = e->rel_x;
    uint8_t x_after_flag = (uint8_t)(x + 0x08);
    uint8_t floaty_x = (uint8_t)(x_after_flag + 0x0c);
    uint8_t floaty_carry = (uint8_t)(floaty_x < x_after_flag);
    uint8_t i;

    for (i = 0; i < 3; i++) {
        uint8_t *oam = &g_SpriteData[base + i * 4];
        /* FlagpoleGfxHandler (main.asm:10578-10596) leaves the carry from
         * the floatey-number X calculation live across DumpTwoSpr, then
         * uses that carry in ADC #$08 for the third flag sprite's Y. */
        oam[0] = (uint8_t)(e->y + (i == 2 ? 8 + floaty_carry : 0));
        oam[1] = (i == 1) ? 0x7f : 0x7e;
        oam[2] = 0x01;
        oam[3] = (uint8_t)(x + (i == 0 ? 0 : 8));
    }

    if (g_FlagpoleCollisionYPos != 0) {
        uint8_t score = g_FlagpoleScore < 5 ? g_FlagpoleScore : 4;
        uint8_t *oam = &g_SpriteData[base + 12];
        oam[0] = g_FlagpoleFNumYPos;
        oam[1] = s_FlagpoleScoreNumTiles[score * 2];
        oam[2] = 0x01;
        oam[3] = (uint8_t)(x + 0x14);
        oam[4] = g_FlagpoleFNumYPos;
        oam[5] = s_FlagpoleScoreNumTiles[score * 2 + 1];
        oam[6] = 0x01;
        oam[7] = (uint8_t)(x + 0x1c);
    }

    if ((e->offscreen_bits & 0x0e) != 0) {
        for (i = 0; i < 6; i++)
            g_SpriteData[base + i * 4] = 0xf8;
    }
}

void draw_bowser_bridge_pair(EnemySlot *e, uint8_t slot) {
    uint8_t rear = linked_duplicate_slot(slot);

    run_bowser_half(e, slot, 1);
    if (rear != 0xff) {
        EnemySlot *rear_object = &enemies[rear];
        uint16_t x = (uint16_t)e->x +
                     ((e->moving_dir & 1) ? 0xf0 : 0x10);
        /* BridgeCollapse falls through the same BowserGfxHandler; the
         * duplicate page and high-Y byte are not written by the 6502
         * routine. */
        rear_object->x = (uint8_t)x;
        rear_object->y = (uint8_t)(e->y + 8);
        rear_object->state = e->state;
        rear_object->moving_dir = e->moving_dir;
        run_bowser_half(rear_object, rear, 2);
    }
    s_Bowser.gfx_flag = 0;
}
