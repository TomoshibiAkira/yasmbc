#include "state_stream.h"

#include <string.h>

#include "constants/defs.h"
#include "constants/globals.h"
#include "level/level.h"
#include "misc.h"
#include "enemy/enemy.h"
#include "collision.h"
#include "sprite-offsets.h"

static void mark(uint8_t *mask, uint16_t address) {
    mask[address >> 3] |= (uint8_t)(1u << (address & 7));
}

static void put(SmbStateSnapshot *s, uint16_t address, uint8_t value,
                int gameplay) {
    s->ram[address] = value;
    mark(s->valid_mask, address);
    if (gameplay) mark(s->gameplay_mask, address);
}

static void put_block(SmbStateSnapshot *s, uint16_t address,
                      const uint8_t *values, uint16_t count, int gameplay) {
    uint16_t i;
    for (i = 0; i < count && address + i < SMB_STATE_RAM_SIZE; ++i)
        put(s, (uint16_t)(address + i), values[i], gameplay);
}

static int enemy_is_firebar(uint8_t id) {
    /* InitShortFirebar/InitLongFirebar (main.asm:5410-5435) occupy IDs
     * $1b-$1f.  FirebarSpinDirection is a six-byte indexed overlay at $34+x;
     * slots 0/1 also have named C aliases (DestinationPageLoc and
     * VictoryWalkControl), while slot 5 overlaps PowerUpType. */
    return id >= 0x1b && id <= 0x1f;
}

void smb_state_snapshot(SmbStateSnapshot *s) {
    MiscBubbleState bubbles[3];
    LevelAreaParserState parser;
    EnemyVerifierState enemy;
    CollisionVerifierState collision;
    uint8_t sprite_offsets[25], sprite_shuffle, sprite_offset_ctrl;
    uint8_t block_buffer_1[208], block_buffer_2[208];
    unsigned i;

    memset(s, 0, sizeof(*s));
    Enemy_GetVerifierState(&enemy);
    Collision_GetVerifierState(&collision);

    /* BubbleCheck owns these persistent object arrays at the physical NES
     * addresses below (main.asm:3298-3344).  They are gameplay state because
     * the next BubbleCheck consults them before deciding whether to write the
     * shared AirBubbleTimer at $0792. */
    Misc_GetBubbleState(bubbles);
    for (i = 0; i < 3; ++i) {
        put(s, (uint16_t)(0x0083 + i), bubbles[i].page, 1);
        put(s, (uint16_t)(0x009c + i), bubbles[i].x, 1);
        put(s, (uint16_t)(0x00cb + i), bubbles[i].y_high, 1);
        put(s, (uint16_t)(0x00e4 + i), bubbles[i].y, 1);
        put(s, (uint16_t)(0x042c + i), bubbles[i].y_mf_dummy, 1);
    }

    /* Persistent zero-page gameplay state.  Addresses are the canonical
     * symbols in smb1-disasm/constants/constants.asm. */
    put(s, 0x0009, (uint8_t)g_FrameCounter, 1);
    put(s, 0x000a, g_A_B_Buttons, 1);
    put(s, 0x000b, g_Up_Down_Buttons, 1);
    put(s, 0x000c, g_Left_Right_Buttons, 1);
    put(s, 0x000d, g_PreviousA_B_Buttons, 1);
    put(s, 0x000e, g_GameEngineSubroutine, 1);
    put(s, 0x001d, g_Player_State, 1);
    put(s, 0x0033, g_PlayerFacingDir, 1);
    for (i = 0; i < 6; ++i) {
        uint8_t value = 0;
        if (enemy_is_firebar(enemy.id[i]) && enemy.flag[i] != 0) {
            value = enemy.firebar_spin_direction[i];
        } else if (i == 0) {
            value = g_DestinationPageLoc;
        } else if (i == 1) {
            value = g_VictoryWalkControl;
        } else if (i == 5) {
            value = enemy.power_up_type;
        }
        put(s, (uint16_t)(0x0034 + i), value, 1);
    }
    put(s, 0x0045, g_Player_MovingDir, 1);
    put(s, 0x0057, g_Player_X_Speed, 1);
    put(s, 0x006d, g_Player_PageLoc, 1);
    put(s, 0x0086, g_Player_X_Position, 1);
    put(s, 0x009f, g_Player_Y_Speed, 1);
    put(s, 0x00b5, g_Player_Y_HighPos, 1);
    put(s, 0x00ce, g_Player_Y_Position, 1);
    put_block(s, 0x000f, enemy.flag, 6, 1);
    put_block(s, 0x0016, enemy.id, 6, 1);
    put_block(s, 0x001e, enemy.state, 6, 1);
    put_block(s, 0x0024, enemy.fireball_state, 2, 1);
    put_block(s, 0x0026, collision.block_state, 4, 1);
    put_block(s, 0x002a, collision.misc_state, 9, 1);
    put_block(s, 0x003a, enemy.fireball_bouncing, 2, 1);
    put_block(s, 0x003c, enemy.hammer_jump_timer, 6, 1);
    put_block(s, 0x0046, enemy.moving_dir, 6, 1);
    put_block(s, 0x0058, enemy.x_speed, 6, 1);
    put_block(s, 0x005e, enemy.fireball_x_speed, 2, 1);
    put_block(s, 0x0060, collision.block_x_speed, 4, 1);
    put_block(s, 0x0064, collision.misc_x_speed, 9, 1);
    put_block(s, 0x006e, enemy.page, 6, 1);
    put_block(s, 0x0074, enemy.fireball_page, 2, 1);
    put_block(s, 0x0076, collision.block_page, 4, 1);
    put_block(s, 0x007a, collision.misc_page, 9, 1);
    put_block(s, 0x0087, enemy.x, 6, 1);
    put_block(s, 0x008d, enemy.fireball_x, 2, 1);
    put_block(s, 0x008f, collision.block_x, 4, 1);
    put_block(s, 0x0093, collision.misc_x, 9, 1);
    put_block(s, 0x00a0, enemy.y_speed, 6, 1);
    put_block(s, 0x00a6, enemy.fireball_y_speed, 2, 1);
    put_block(s, 0x00a8, collision.block_y_speed, 4, 1);
    put_block(s, 0x00ac, collision.misc_y_speed, 9, 1);
    put_block(s, 0x00b6, enemy.y_high, 6, 1);
    put_block(s, 0x00bc, enemy.fireball_y_high, 2, 1);
    put_block(s, 0x00be, collision.block_y_high, 4, 1);
    put_block(s, 0x00c2, collision.misc_y_high, 9, 1);
    put_block(s, 0x00cf, enemy.y, 6, 1);
    put_block(s, 0x00d5, enemy.fireball_y, 2, 1);
    put_block(s, 0x00d7, collision.block_y, 4, 1);
    put_block(s, 0x00db, collision.misc_y, 9, 1);
    put(s, 0x010d, g_FlagpoleFNumYPos, 1);
    put(s, 0x010e, g_FlagpoleFNumYMF_Dummy, 1);
    put(s, 0x010f, g_FlagpoleScore, 1);
    put_block(s, 0x0133, g_DigitModifier, 7, 1);

    /* CPU-visible OAM and VRAM queues are mapped for diagnostics.  They are
     * intentionally not gameplay gates because raster presentation remains
     * a separate verifier channel. */
    put_block(s, 0x0200, g_SpriteData, 0x100, 0);
    put(s, 0x0300, g_VRAM_Buffer1_Offset, 0);
    put_block(s, 0x0301, g_VRAM_Buffer1, 0x3f, 0);
    put(s, 0x0340, g_VRAM_Buffer2_Offset, 0);
    /* VRAM_Buffer2 is the physical $0341-$03ff workspace.  The disassembly
     * gives several object fields in the same bytes (for example
     * Player_Rel_XPos=$03ad, Player_Rel_YPos=$03b8,
     * Player_SprAttrib=$03c4, and Player_OffscreenBits=$03d0).  They are
     * aliases, not independent RAM.  Map the physical buffer once so parser
     * writes cannot be mistaken for an independent gameplay field. */
    put_block(s, 0x0341, g_VRAM_Buffer2, 0xbf, 0);

    /* These physical bytes alias the VRAM/OAM workspaces but are persistent
     * gameplay owners at the NMI boundary.  Later puts deliberately promote
     * them from diagnostic to gameplay in the semantic mask. */
    put(s, 0x03a0, g_BalPlatformAlignment, 1);
    put(s, 0x03a1, g_Platform_X_Scroll, 1);
    /* Relative positions, sprite attributes, and the current-pass offscreen
     * bytes are written by RelativePosition/Get*OffscreenBits immediately before
     * their consumers (main.asm:12189-12364).  They are presentation/scratch
     * results at an NMI boundary, not persistent object owners.  Leave the
     * physical Buffer2 projection above as diagnostic and do not promote these
     * addresses merely because similarly named C fields exist. */
    SpriteOffsets_GetVerifierState(&sprite_shuffle, &sprite_offset_ctrl,
                                   sprite_offsets);
    put(s, 0x03ee, sprite_offset_ctrl, 1);
    put(s, 0x06e0, sprite_shuffle, 1);
    /* SpriteOffsets_GetVerifierState exposes the host pool through index 24,
     * but physical $06fc is SavedJoypadBits in the ROM.  The input put below
     * intentionally wins at that one aliased byte; policy v3 records
     * $06e4-$06fb as the offset owner and $06fc-$06fd as processed input. */
    put_block(s, 0x06e4, sprite_offsets, sizeof(sprite_offsets), 1);

    put(s, 0x0400, g_Player_X_MF2, 1);
    put_block(s, 0x0401, enemy.x_mf, 6, 1);
    put(s, 0x0416, g_Player_YMF_Dummy, 1);
    put_block(s, 0x0417, enemy.y_dummy, 6, 1);
    put(s, 0x0433, g_Player_Y_MoveForce, 1);
    put_block(s, 0x0434, enemy.y_mf, 6, 1);
    put_block(s, 0x043c, collision.block_y_force, 4, 1);
    put_block(s, 0x0388, enemy.firebar_spin_speed, 6, 1);
    put(s, 0x0398, g_VineFlagOffset, 1);
    put(s, 0x0399, g_VineHeight, 1);
    put_block(s, 0x039a, g_VineObjOffset, 3, 1);
    put(s, 0x039d, g_VineStartY, 1);
    put_block(s, 0x03a2, enemy.hammer_throw_timer, 6, 1);
    put_block(s, 0x03d8, enemy.offscreen_masked, 6, 1);
    put_block(s, 0x03e4, collision.block_orig_y, 2, 1);
    put_block(s, 0x03e6, collision.block_bbuf_low, 2, 1);
    put_block(s, 0x03e8, collision.block_metatile, 2, 1);
    put_block(s, 0x03ea, collision.block_page2, 2, 1);
    put_block(s, 0x03ec, collision.block_rep_flag, 2, 1);
    put(s, 0x03f0, collision.block_residual_counter, 1);
    put_block(s, 0x03f1, collision.block_orig_x, 2, 1);
    put(s, 0x0450, g_MaximumLeftSpeed, 1);
    put(s, 0x0456, g_MaximumRightSpeed, 1);
    put(s, 0x046a, g_WhirlpoolOffset, 1);
    for (i = 0; i < 6; ++i) {
        const MiscCannonRecord *record = Misc_GetCannon((uint8_t)i);
        if (record != NULL) {
            put(s, (uint16_t)(0x046b + i), record->page, 1);
            put(s, (uint16_t)(0x0471 + i), record->x_or_left, 1);
            put(s, (uint16_t)(0x0477 + i), record->y_or_length, 1);
            put(s, (uint16_t)(0x047d + i), record->timer, 1);
        }
    }
    put(s, 0x0490, g_Player_CollisionBits, 1);
    put_block(s, 0x0491, enemy.collision_bits, 6, 1);
    put(s, 0x0499, g_Player_BoundBoxCtrl, 1);
    put_block(s, 0x049a, enemy.bbox_ctrl, 6, 1);
    put_block(s, 0x04a0, enemy.fireball_bbox_ctrl, 2, 1);
    put_block(s, 0x04a2, collision.misc_bbox_ctrl, 9, 1);
    put_block(s, 0x04ac, g_Player_BoundingBox, 4, 1);
    for (i = 0; i < 6; ++i)
        put_block(s, (uint16_t)(0x04b0 + i * 4), enemy.bbox[i], 4, 1);
    put_block(s, 0x0110, enemy.floaty_control, 6, 1);
    put_block(s, 0x0117, enemy.floaty_x, 6, 1);
    put_block(s, 0x011e, enemy.floaty_y, 6, 1);
    put_block(s, 0x0125, enemy.shell_chain, 6, 1);
    put_block(s, 0x012c, enemy.floaty_timer, 6, 1);
    put(s, 0x0484, enemy.stomp_chain_counter, 1);
    put_block(s, 0x0363, enemy.bowser, 8, 1);
    put(s, 0x0483, enemy.bowser[8], 1);
    put(s, 0x06dc, enemy.bowser[9], 1);
    put(s, 0x06d1, enemy.lakitu_reappear_timer, 1);
    put_block(s, 0x06ae, collision.hammer_source, 9, 1);
    put_block(s, 0x06be, collision.misc_collision, 9, 1);

    /* Parser, scrolling, input, timers and mode state. */
    put(s, 0x06bc, g_BrickCoinTimerFlag, 1);
    put(s, 0x06cc, g_SecondaryHardMode, 1);
    put(s, 0x06cb, g_EnemyFrenzyBuffer, 1);
    put(s, 0x06cd, g_EnemyFrenzyQueue, 1);
    put(s, 0x06ce, g_FireballCounter, 1);
    put(s, 0x06d3, g_NumberofGroupEnemies, 1);
    put(s, 0x06d4, g_ColorRotateOffset, 1);
    put(s, 0x06d5, g_PlayerGfxOffset, 1);
    put(s, 0x06d6, g_WarpZoneControl, 1);
    put(s, 0x06d7, g_FireworksCounter, 1);
    put(s, 0x06d9, g_MultiLoopCorrectCntr, 1);
    put(s, 0x06da, g_MultiLoopPassCntr, 1);
    put(s, 0x06db, g_JumpspringForce, 1);
    put(s, 0x06dd, g_BitMFilter, 1);
    put(s, 0x06de, g_ChangeAreaTimer, 1);
    put(s, 0x06fc, g_SavedJoypadBits, 1);
    put(s, 0x06fd, g_SavedJoypad2Bits, 1);
    put(s, 0x06ff, g_Player_X_Scroll, 1);
    put(s, 0x0700, g_Player_XSpeedAbsolute, 1);
    put(s, 0x0703, g_RunningSpeed, 1);
    put(s, 0x0704, g_SwimmingFlag, 1);
    put(s, 0x0705, g_Player_X_MoveForce, 1);
    put(s, 0x0706, g_DiffToHaltJump, 1);
    put(s, 0x0707, g_JumpOrigin_Y_HighPos, 1);
    put(s, 0x0708, g_JumpOrigin_Y, 1);
    put(s, 0x0709, g_VerticalForce, 1);
    put(s, 0x070a, g_FallMForce, 1);
    put(s, 0x070b, g_PlayerChangeSizeFlag, 1);
    put(s, 0x070c, g_PlayerAnimTimerSet, 1);
    put(s, 0x070d, g_PlayerAnimCtrl, 1);
    put(s, 0x070e, g_JumpspringAnimCtrl, 1);
    put(s, 0x070f, g_FlagpoleCollisionYPos, 1);
    put(s, 0x0710, g_PlayerEntranceCtrl, 1);
    put(s, 0x0711, g_FireballThrowingTimer, 1);
    put(s, 0x0712, g_DeathMusicLoaded, 1);
    put(s, 0x0713, g_FlagpoleSoundQueue, 1);
    put(s, 0x0714, g_CrouchingFlag, 1);
    put(s, 0x0715, g_GameTimerSetting, 1);
    put(s, 0x0716, g_DisableCollisionDet, 1);
    put(s, 0x0717, g_DemoAction, 1);
    put(s, 0x0718, g_DemoActionTimer, 1);
    put(s, 0x0719, g_PrimaryMsgCounter, 1);
    put(s, 0x071a, g_ScreenLeft_PageLoc, 1);
    put(s, 0x071b, g_ScreenRight_PageLoc, 1);
    put(s, 0x071c, g_ScreenLeft_X_Pos, 1);
    put(s, 0x071d, g_ScreenRight_X_Pos, 1);
    put(s, 0x071e, g_ColumnSets, 1);
    put(s, 0x071f, g_AreaParserTaskNum, 1);
    put(s, 0x0720, g_CurrentNTAddrHigh, 1);
    put(s, 0x0721, g_CurrentNTAddrLow, 1);
    put(s, 0x0722, g_Sprite0HitDetectFlag, 0);
    put(s, 0x0723, g_ScrollLock, 1);
    put(s, 0x0725, g_CurrentPageLoc, 1);
    put(s, 0x0726, g_CurrentColumnPos, 1);
    put(s, 0x0727, g_TerrainControl, 1);
    put(s, 0x0728, g_BackloadingFlag, 1);
    put(s, 0x0729, g_BehindAreaParserFlag, 1);
    Level_GetAreaParserState(&parser);
    Level_GetVerifierBlockBuffers(block_buffer_1, block_buffer_2);
    put(s, 0x072a, parser.object_page_loc, 1);
    put(s, 0x072b, parser.object_page_sel, 1);
    put(s, 0x072c, parser.data_offset, 1);
    put_block(s, 0x072d, parser.object_offsets, 3, 1);
    put_block(s, 0x0730, parser.object_lengths, 3, 1);
    put(s, 0x06a0, parser.block_buffer_column, 1);
    put_block(s, 0x06a1, parser.metatile_buffer,
              sizeof(parser.metatile_buffer), 1);
    put_block(s, 0x0500, block_buffer_1, sizeof(block_buffer_1), 1);
    put_block(s, 0x05d0, block_buffer_2, sizeof(block_buffer_2), 1);
    put(s, 0x0734, g_StaircaseControl, 1);
    put(s, 0x0733, g_AreaStyle, 1);
    put(s, 0x0739, g_EnemyDataOffset, 1);
    put(s, 0x073a, g_EnemyObjectPageLoc, 1);
    put(s, 0x073b, g_EnemyObjectPageSel, 1);
    put(s, 0x073c, g_ScreenRoutineTask, 1);
    put(s, 0x073d, g_ScrollThirtyTwo, 1);
    put(s, 0x073f, g_HorizontalScroll, 1);
    put(s, 0x0740, g_Screen_Y_Position, 1);
    put(s, 0x0741, g_ForegroundScenery, 1);
    put(s, 0x0742, g_BackgroundScenery, 1);
    put(s, 0x0743, g_CloudTypeOverride, 1);
    put(s, 0x0744, g_BackgroundColorCtrl, 1);
    put(s, 0x0745, g_LoopCommand, 1);
    put(s, 0x0746, g_StarFlagTaskControl, 1);
    put(s, 0x0747, g_TimerControl, 1);
    put(s, 0x0748, g_CoinTallyFor1Ups, 1);
    put(s, 0x0749, g_SecondaryMsgCounter, 1);
    put(s, 0x074a, g_JoypadBitMask[0], 1);
    put(s, 0x074b, g_JoypadBitMask[1], 1);
    put(s, 0x074e, g_AreaType, 1);
    put(s, 0x0750, g_AreaPointer, 1);
    put(s, 0x0751, g_EntrancePage, 1);
    put(s, 0x0752, g_AltEntranceControl, 1);
    put(s, 0x0753, g_CurrentPlayer, 1);
    put(s, 0x0754, g_PlayerSize, 1);
    put(s, 0x0755, g_Player_Pos_ForScroll, 1);
    put(s, 0x0756, g_PlayerStatus, 1);
    put(s, 0x0757, g_FetchNewGameTimerFlag, 1);
    put(s, 0x0758, g_JoypadOverride, 1);
    put(s, 0x0759, g_GameTimerExpiredFlag, 1);
    put_block(s, 0x075a, g_OnscreenPlayerInfo, 7, 1);
    put_block(s, 0x0761, g_OffscreenPlayerInfo, 7, 1);
    put(s, 0x0768, g_ScrollFractional, 1);
    put(s, 0x0769, g_DisableIntermediate, 1);
    put(s, 0x076a, g_PrimaryHardMode, 1);
    put(s, 0x076b, g_WorldSelectNumber, 1);
    put(s, 0x0770, g_OperMode, 1);
    put(s, 0x0772, g_OperMode_Task, 1);
    put(s, 0x0773, g_VRAM_Buffer_AddrCtrl, 0);
    put(s, 0x0774, g_DisableScreenFlag, 0);
    put(s, 0x0775, g_ScrollAmount, 1);
    put(s, 0x0776, g_GamePauseStatus, 1);
    put(s, 0x0777, g_GamePauseTimer, 1);
    put(s, 0x0778, g_MirrorPPUCtrl1, 0);
    put(s, 0x077a, g_NumberOfPlayers, 1);
    put(s, 0x077f, g_IntervalTimerControl, 1);
    put_block(s, 0x0780, g_Timers, TIMERS_ARRAY_SIZE, 1);
    put_block(s, 0x07a7, g_PseudoRandomBitReg, 8, 1);
    put_block(s, 0x07d7, g_DisplayDigits, 36, 1);
    put(s, 0x07fc, g_WorldSelectEnableFlag, 1);
    put(s, 0x07fd, g_ContinueWorld, 1);
}

static int write_u16(FILE *stream, uint16_t value) {
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return fwrite(bytes, 1, 2, stream) == 2 ? 0 : -1;
}

static int write_u32(FILE *stream, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return fwrite(bytes, 1, 4, stream) == 4 ? 0 : -1;
}

int smb_state_write_header(FILE *stream, const SmbStateSnapshot *s) {
    static const uint8_t magic[8] = {'S','M','B','S','T','A','2','\0'};
    if (fwrite(magic, 1, sizeof(magic), stream) != sizeof(magic) ||
        write_u16(stream, SMB_STATE_RAM_SIZE) < 0 ||
        write_u16(stream, SMB_STATE_MASK_SIZE) < 0 ||
        write_u32(stream, 8u + SMB_STATE_RAM_SIZE) < 0 ||
        fwrite(s->valid_mask, 1, SMB_STATE_MASK_SIZE, stream) != SMB_STATE_MASK_SIZE ||
        fwrite(s->gameplay_mask, 1, SMB_STATE_MASK_SIZE, stream) != SMB_STATE_MASK_SIZE)
        return -1;
    return 0;
}

int smb_state_write_record(FILE *stream, uint32_t video_frame, uint8_t flags,
                           uint8_t raw_movie_input0, uint8_t raw_movie_input1,
                           const SmbStateSnapshot *s) {
    if (write_u32(stream, video_frame) < 0 || fputc(flags, stream) == EOF ||
        fputc(raw_movie_input0, stream) == EOF ||
        fputc(raw_movie_input1, stream) == EOF || fputc(0, stream) == EOF ||
        fwrite(s->ram, 1, SMB_STATE_RAM_SIZE, stream) != SMB_STATE_RAM_SIZE)
        return -1;
    return 0;
}
