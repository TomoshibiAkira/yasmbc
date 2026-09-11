#!/usr/bin/env python3
"""Extract playable assets from a user-supplied Super Mario Bros. (W) [!] iNES ROM.

The running game never opens the iNES file. This generator is the only program
that reads it. Offsets: PRG file offset = CPU - $8000 + 16-byte iNES header.

Usage: python3 tools/extract_assets.py <rom.nes> <output_dir>
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys

EXPECTED_SHA1 = "ea343f4e445a9050d4b4fbac2c77d0693b1d0922"
INES_HEADER = 16
PRG_BANK = 0x4000
CHR_BANK = 0x2000
CPU_PRG_BASE = 0x8000
MUSIC_CPU = 0xF90D
MUSIC_END_INCLUSIVE = 0xFFF9  # audio/music.asm ends before interrupt vectors
TITLE_CHR_OFF = 0x1EC0
TITLE_LEN = 0x13A

OBSOLETE_GENERATED_ASSETS = (
    "audio/prg_f90d.bin",
    "tables/world_addr_offsets.bin",
    "tables/area_addr_offsets.bin",
    "tables/game_text_offsets.bin",
    "tables/game_text.bin",
    "tables/area_data_h_offsets.bin",
    "tables/enemy_addr_h_offsets.bin",
    "tables/bubble_rom_window.bin",
    "tables/fireball_x_spd_window.bin",
    "tables/prandom_subtracter_window.bin",
    "tables/fly_cc_bpriority_window.bin",
    "tables/climb_adder_window.bin",
)

# ROM storage order in src/levels.asm (not AreaDataAddr index order).
AREA_LABELS = (
    [f"L_CastleArea{i}" for i in range(1, 7)]
    + [f"L_GroundArea{i}" for i in range(1, 23)]
    + [f"L_UndergroundArea{i}" for i in range(1, 4)]
    + [f"L_WaterArea{i}" for i in range(1, 4)]
)
ENEMY_LABELS = [
    "E_CastleArea1", "E_CastleArea2", "E_CastleArea3", "E_CastleArea4",
    "E_CastleArea5", "E_CastleArea6",
    "E_GroundArea1", "E_GroundArea2", "E_GroundArea3", "E_GroundArea4",
    "E_GroundArea5", "E_GroundArea6", "E_GroundArea7", "E_GroundArea8",
    "E_GroundArea9", "E_GroundArea10", "E_GroundArea11", "E_GroundArea12",
    "E_GroundArea13", "E_GroundArea14", "E_GroundArea15", "E_GroundArea16",
    "E_GroundArea17", "E_GroundArea18", "E_GroundArea19", "E_GroundArea20",
    "E_GroundArea21", "E_GroundArea22",
    "E_UndergroundArea1", "E_UndergroundArea2", "E_UndergroundArea3",
    "E_WaterArea1", "E_WaterArea2", "E_WaterArea3",
]
TABLE_GROUPS = [
    ["Palette0_MTiles"],
    ["Palette1_MTiles"],
    ["Palette2_MTiles"],
    ["Palette3_MTiles"],
    ["TerrainMetatiles", "TerrainRenderBits"],
    ["WarpZoneNumbers"],
    ["PlayerGfxTblOffsets"],
    ["PlayerGraphicsTable"],
    ["ChangeSizeOffsetAdder"],
    ["ClimbAdderLow", "ClimbAdderHigh"],
    ["JumpMForceData", "FallMForceData", "PlayerYSpdData", "InitMForceData",
     "MaxLeftXSpdData", "MaxRightXSpdData", "FrictionData",
     "Climb_Y_SpeedData", "Climb_Y_MForceData"],
    ["PlayerAnimTmrData"],
    ["PlayerColors"],
    ["BackgroundColors"],
    ["WaterPaletteData"],
    ["GroundPaletteData"],
    ["UndergroundPaletteData"],
    ["CastlePaletteData"],
    ["DaySnowPaletteData"],
    ["NightSnowPaletteData"],
    ["MushroomPaletteData"],
    ["BowserPaletteData"],
    ["MarioThanksMessage"],
    ["LuigiThanksMessage"],
    ["MushroomRetainerSaved"],
    ["PrincessSaved1"],
    ["PrincessSaved2"],
    ["WorldSelectMessage1"],
    ["WorldSelectMessage2"],
    ["X_SubtracterData", "OffscrJoypadBitsData"],
    ["IntermediatePlayerData"],
]

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
DB_RE = re.compile(r"\.db\s+(.+)$", re.IGNORECASE)


def port_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def repo_root():
    return os.path.dirname(port_root())


def cpu_to_prg(cpu):
    return cpu - CPU_PRG_BASE


def cpu_to_file(cpu):
    return INES_HEADER + cpu_to_prg(cpu)


CONST_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([\$%]?[0-9A-Fa-f_]+)\s*$"
)


def load_named_constants():
    constants = {}
    const_dir = os.path.join(repo_root(), "smb1-disasm", "constants")
    for filename in ("entity_constants.asm", "music_constants.asm"):
        path = os.path.join(const_dir, filename)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                match = CONST_RE.match(raw.split(";", 1)[0].strip())
                if match:
                    token = match.group(2).replace("_", "")
                    base = 16 if token.startswith("$") else 2 if token.startswith("%") else 10
                    constants[match.group(1)] = int(token.lstrip("$%"), base)
    return constants


PARSED_CONSTANTS = None


def parse_db_tokens(rest):
    constants = PARSED_CONSTANTS if PARSED_CONSTANTS is not None else {}
    rest = rest.split(";", 1)[0].strip()
    if not rest:
        return []
    out = []
    for tok in rest.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if tok[0] in "<>":
            return None
        if tok.startswith("%"):
            out.append(int(tok[1:].replace("_", ""), 2))
        elif tok.startswith("$"):
            out.append(int(tok[1:], 16))
        elif re.fullmatch(r"[0-9]+", tok):
            out.append(int(tok, 10))
        elif tok in constants:
            out.append(constants[tok])
        else:
            return None
    return out


def collect_labeled_dbs(paths):
    blobs = {}
    current = None
    pending = []

    def flush():
        nonlocal current, pending
        if current is not None and pending:
            blobs[current] = bytes(pending)
        pending = []

    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                line = raw.strip()
                if not line or line.startswith(";"):
                    continue
                match = LABEL_RE.match(line)
                if match:
                    flush()
                    current = match.group(1)
                    rest = line[match.end():].strip()
                    if rest:
                        db = DB_RE.search(rest)
                        if db:
                            vals = parse_db_tokens(db.group(1))
                            if vals is None:
                                current = None
                            else:
                                pending.extend(vals)
                    continue
                db = DB_RE.search(line)
                if db and current is not None:
                    vals = parse_db_tokens(db.group(1))
                    if vals is None:
                        flush()
                        current = None
                    else:
                        pending.extend(vals)
    flush()
    return blobs


def read_ines(path):
    with open(path, "rb") as fh:
        data = fh.read()
    digest = hashlib.sha1(data).hexdigest()
    if digest != EXPECTED_SHA1:
        raise SystemExit(
            f"ROM SHA-1 {digest} does not match {EXPECTED_SHA1}. "
            "Only Super Mario Bros. (W) [!] is accepted."
        )
    if data[:4] != b"NES\x1a":
        raise SystemExit(f"Not an iNES ROM: {path}")
    prg_banks = data[4]
    chr_banks = data[5]
    flags6 = data[6]
    off = INES_HEADER
    if flags6 & 0x04:
        off += 512
    prg = data[off:off + prg_banks * PRG_BANK]
    chr_data = data[off + prg_banks * PRG_BANK:
                    off + prg_banks * PRG_BANK + chr_banks * CHR_BANK]
    return data, prg, chr_data


def find_unique(haystack, needle, name):
    if not needle:
        raise SystemExit(f"{name}: empty blob")
    idx = haystack.find(needle)
    if idx < 0:
        raise SystemExit(f"{name}: not found in ROM")
    if haystack.find(needle, idx + 1) >= 0:
        raise SystemExit(f"{name}: ambiguous match in ROM")
    return idx


OUTPUT_RECORDS = []


def asset_category(rel):
    if rel.startswith("compat-rom-windows/"):
        return "rom-layout-compatibility"
    if rel.startswith("audio/"):
        return "audio-data"
    if rel.startswith("areas/") or rel.startswith("enemies/"):
        return "level-data"
    if rel == "tiles.chr" or rel.startswith("tables/vram_"):
        return "presentation-data"
    return "game-data"


def write_file(root, rel, payload, source_label=None, source_offset=None,
               extraction="rom-slice"):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(payload)
    OUTPUT_RECORDS.append({
        "path": rel,
        "category": asset_category(rel),
        "source_label": source_label,
        "prg_or_chr_offset": source_offset,
        "extraction": extraction,
        "size": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    })
    print(f"  {rel}: {len(payload)} bytes")


def table_relpath(label):
    mapping = {
        "Palette0_MTiles": "tables/palette0_mtiles.bin",
        "Palette1_MTiles": "tables/palette1_mtiles.bin",
        "Palette2_MTiles": "tables/palette2_mtiles.bin",
        "Palette3_MTiles": "tables/palette3_mtiles.bin",
        "TerrainMetatiles": "tables/terrain_metatiles.bin",
        "TerrainRenderBits": "tables/terrain_render_bits.bin",
        "WarpZoneNumbers": "tables/warp_zone_numbers.bin",
        "PlayerGfxTblOffsets": "tables/player_gfx_tbl_offsets.bin",
        "PlayerGraphicsTable": "tables/player_graphics.bin",
        "ChangeSizeOffsetAdder": "tables/change_size_offset_adder.bin",
        "MaxRightXSpdData": "tables/max_right_x_spd.bin",
        "MaxLeftXSpdData": "tables/max_left_x_spd.bin",
        "FrictionData": "tables/friction.bin",
        "JumpMForceData": "tables/jump_mforce.bin",
        "FallMForceData": "tables/fall_mforce.bin",
        "PlayerYSpdData": "tables/player_y_spd.bin",
        "InitMForceData": "tables/init_mforce.bin",
        "Climb_Y_SpeedData": "tables/climb_y_speed.bin",
        "Climb_Y_MForceData": "tables/climb_y_mforce.bin",
        "ClimbAdderLow": "tables/climb_adder_low.bin",
        "ClimbAdderHigh": "tables/climb_adder_high.bin",
        "PlayerAnimTmrData": "tables/player_anim_tmr.bin",
        "PlayerColors": "tables/player_colors.bin",
        "BackgroundColors": "tables/background_colors.bin",
        "WaterPaletteData": "tables/vram_water_palette.bin",
        "GroundPaletteData": "tables/vram_ground_palette.bin",
        "UndergroundPaletteData": "tables/vram_underground_palette.bin",
        "CastlePaletteData": "tables/vram_castle_palette.bin",
        "DaySnowPaletteData": "tables/vram_snow_day.bin",
        "NightSnowPaletteData": "tables/vram_snow_night.bin",
        "MushroomPaletteData": "tables/vram_mushroom.bin",
        "BowserPaletteData": "tables/vram_bowser.bin",
        "MarioThanksMessage": "tables/vram_mario_thanks.bin",
        "LuigiThanksMessage": "tables/vram_luigi_thanks.bin",
        "MushroomRetainerSaved": "tables/vram_retainer.bin",
        "PrincessSaved1": "tables/vram_princess_1.bin",
        "PrincessSaved2": "tables/vram_princess_2.bin",
        "WorldSelectMessage1": "tables/vram_world_select_1.bin",
        "WorldSelectMessage2": "tables/vram_world_select_2.bin",
        "FloateyNumTileData": "tables/floatey_num_tiles.bin",
        "ScoreUpdateData": "tables/score_update.bin",
        "DefaultSprOffsets": "tables/default_spr_offsets.bin",
        "Sprite0Data": "tables/sprite0.bin",
        "MusicSelectData": "tables/music_select.bin",
        "HalfwayPageNybbles": "tables/halfway_page_nybbles.bin",
        "BSceneDataOffsets": "tables/bscene_offsets.bin",
        "BackSceneryData": "tables/back_scenery.bin",
        "BackSceneryMetatiles": "tables/back_scenery_metatiles.bin",
        "FSceneDataOffsets": "tables/fscene_offsets.bin",
        "ForeSceneryData": "tables/fore_scenery.bin",
        "Bitmasks": "tables/bitmasks.bin",
        "Enemy17YPosData": "tables/enemy17_ypos.bin",
        "SwimCC_IDData": "tables/swim_cc_id.bin",
        "FirebarPosLookupTbl": "tables/firebar_pos_lookup.bin",
        "FirebarMirrorData": "tables/firebar_mirror.bin",
        "FirebarTblOffsets": "tables/firebar_tbl_offsets.bin",
        "FirebarYPos": "tables/firebar_ypos.bin",
        "BlockBufferAdderData": "tables/block_buffer_adder.bin",
        "BlockBuffer_X_Adder": "tables/block_buffer_x_adder.bin",
        "BlockBuffer_Y_Adder": "tables/block_buffer_y_adder.bin",
        "EnemyGraphicsTable": "tables/enemy_graphics.bin",
        "EnemyGfxTableOffsets": "tables/enemy_gfx_tbl_offsets.bin",
        "EnemyAttributeData": "tables/enemy_attribute.bin",
        "EnemyAnimTimingBMask": "tables/enemy_anim_timing.bin",
        "JumpspringFrameOffsets": "tables/jumpspring_frame_offsets.bin",
        "XOffscreenBitsData": "tables/x_offscreen_bits.bin",
        "DefaultXOnscreenOfs": "tables/default_x_onscreen_ofs.bin",
        "YOffscreenBitsData": "tables/y_offscreen_bits.bin",
        "DefaultYOnscreenOfs": "tables/default_y_onscreen_ofs.bin",
        "HighPosUnitData": "tables/high_pos_unit.bin",
        "StatusBarData": "tables/status_bar_data.bin",
        "StatusBarOffset": "tables/status_bar_offset.bin",
        "TopStatusBarLine": "tables/top_status_bar.bin",
        "WorldLivesDisplay": "tables/world_lives_display.bin",
        "TwoPlayerTimeUp": "tables/two_player_time_up.bin",
        "OnePlayerTimeUp": "tables/one_player_time_up.bin",
        "TwoPlayerGameOver": "tables/two_player_game_over.bin",
        "OnePlayerGameOver": "tables/one_player_game_over.bin",
        "WarpZoneWelcome": "tables/warp_zone_welcome.bin",
        "LuigiName": "tables/luigi_name.bin",
        "ColorRotatePalette": "tables/color_rotate_palette.bin",
        "BlankPalette": "tables/blank_palette.bin",
        "Palette3Data": "tables/palette3.bin",
        "BlockGfxData": "tables/block_gfx.bin",
        "PulleyRopeMetatiles": "tables/pulley_rope_metatiles.bin",
        "CastleMetatiles": "tables/castle_metatiles.bin",
        "VerticalPipeData": "tables/vertical_pipe.bin",
        "SidePipeShaftData": "tables/side_pipe_shaft.bin",
        "SidePipeTopPart": "tables/side_pipe_top.bin",
        "SidePipeBottomPart": "tables/side_pipe_bottom.bin",
        "SolidBlockMetatiles": "tables/solid_block_metatiles.bin",
        "BrickMetatiles": "tables/brick_metatiles.bin",
        "CoinMetatileData": "tables/coin_metatiles.bin",
        "C_ObjectRow": "tables/c_object_row.bin",
        "C_ObjectMetatile": "tables/c_object_metatile.bin",
        "StaircaseHeightData": "tables/staircase_height.bin",
        "StaircaseRowData": "tables/staircase_row.bin",
        "HoleMetatiles": "tables/hole_metatiles.bin",
        "AreaDataOfsLoopback": "tables/area_data_ofs_loopback.bin",
        "LoopCmdWorldNumber": "tables/loop_cmd_world.bin",
        "LoopCmdPageNumber": "tables/loop_cmd_page.bin",
        "LoopCmdYPosition": "tables/loop_cmd_y.bin",
        "BrickQBlockMetatiles": "tables/brick_qblock_metatiles.bin",
        "CoinTallyOffsets": "tables/coin_tally_offsets.bin",
        "ScoreOffsets": "tables/score_offsets.bin",
        "StatusBarNybbles": "tables/status_bar_nybbles.bin",
        "VineHeightData": "tables/vine_height.bin",
        "FrenzyIDData": "tables/frenzy_id.bin",
        "FlyCCXPositionData": "tables/fly_cc_x_position.bin",
        "FlyCCXSpeedData": "tables/fly_cc_x_speed.bin",
        "FlyCCTimerData": "tables/fly_cc_timer.bin",
        "FlameYPosData": "tables/flame_ypos.bin",
        "FireworksXPosData": "tables/fireworks_xpos.bin",
        "FireworksYPosData": "tables/fireworks_ypos.bin",
        "XSpeedAdderData": "tables/x_speed_adder.bin",
        "RevivedXSpeed": "tables/revived_x_spd.bin",
        "HammerThrowTmrData": "tables/hammer_throw_timer.bin",
        "LakituDiffAdj": "tables/lakitu_diff_adj.bin",
        "BowserIdentities": "tables/bowser_identities.bin",
        "KickedShellXSpdData": "tables/kicked_shell_x_spd.bin",
        "KickedShellPtsData": "tables/kicked_shell_pts.bin",
        "ClimbXPosAdder": "tables/climb_x_pos_adder.bin",
        "ClimbPLocAdder": "tables/climb_ploc_adder.bin",
        "FlagpoleYPosData": "tables/flagpole_ypos.bin",
        "BoundBoxCtrlData": "tables/bound_box_ctrl.bin",
        "FlagpoleScoreNumTiles": "tables/flagpole_score_tiles.bin",
        "PowerUpGfxTable": "tables/powerup_gfx.bin",
        "PowerUpAttributes": "tables/powerup_attributes.bin",
        "EnemyBGCStateData": "tables/enemy_bgc_state.bin",
        "AreaPalette": "tables/area_palette.bin",
        "BGColorCtrl_Addr": "tables/bg_color_ctrl_addr.bin",
        "DemoActionData": "tables/demo_action.bin",
        "DemoTimingData": "tables/demo_timing.bin",
        "WSelectBufferTemplate": "tables/wselect_buffer.bin",
        "MushroomIconData": "tables/mushroom_icon.bin",
        "PlayerStarting_X_Pos": "tables/player_starting_x.bin",
        "AltYPosOffset": "tables/alt_y_pos_offset.bin",
        "PlayerStarting_Y_Pos": "tables/player_starting_y.bin",
        "PlayerBGPriorityData": "tables/player_bg_priority.bin",
        "GameTimerData": "tables/game_timer.bin",
        "Hidden1UpCoinAmts": "tables/hidden_1up_coin_amts.bin",
        "EnemyAddrHOffsets": "tables/enemy_type_base_index.bin",
        "AreaDataHOffsets": "tables/area_type_base_index.bin",
        "SwimStompEnvelopeData": "tables/swim_stomp_envelope.bin",
        "ExtraLifeFreqData": "tables/extra_life_freq.bin",
        "PowerUpGrabFreqData": "tables/powerup_grab_freq.bin",
        "PUp_VGrow_FreqData": "tables/powerup_vgrow_freq.bin",
        "BrickShatterFreqData": "tables/brick_shatter_freq.bin",
        "Jumpspring_Y_PosData": "tables/jumpspring_ypos.bin",
        "FlagpoleScoreMods": "tables/flagpole_score_mods.bin",
        "FlagpoleScoreDigits": "tables/flagpole_score_digits.bin",
        "CannonBitmasks": "tables/cannon_bitmasks.bin",
        "HammerEnemyOfsData": "tables/hammer_enemy_ofs.bin",
        "HammerXSpdData": "tables/hammer_x_spd.bin",
        "BlockYPosAdderData": "tables/block_ypos_adder.bin",
        "PRDiffAdjustData": "tables/pr_diff_adjust.bin",
        "BridgeCollapseData": "tables/bridge_collapse.bin",
        "ExplosionTiles": "tables/explosion_tiles.bin",
        "FireballXSpdData": "tables/fireball_x_spd.bin",
        "FlameTimerData": "tables/bowser_flame_timer.bin",
        "PRandomRange": "tables/bowser_prandom_range.bin",
        "SetBitsMask": "tables/set_bits_mask.bin",
        "ClearBitsMask": "tables/clear_bits_mask.bin",
        "FirstSprXPos": "tables/first_spr_x.bin",
        "FirstSprYPos": "tables/first_spr_y.bin",
        "SecondSprXPos": "tables/second_spr_x.bin",
        "SecondSprYPos": "tables/second_spr_y.bin",
        "FirstSprTilenum": "tables/first_spr_tile.bin",
        "SecondSprTilenum": "tables/second_spr_tile.bin",
        "HammerSprAttrib": "tables/hammer_spr_attrib.bin",
        "PRandomSubtracter": "tables/prandom_subtracter.bin",
        "FlyCCBPriority": "tables/fly_cc_bpriority.bin",
        "Bubble_MForceData": "tables/bubble_mforce.bin",
        "BubbleTimerData": "tables/bubble_timer.bin",
        "BlockBuffLowBounds": "tables/block_buff_low_bounds.bin",
        "BulletBillXSpdData": "tables/bullet_bill_x_spd.bin",
        "MaxSpdBlockData": "tables/max_spd_block.bin",
        "NormalXSpdData": "tables/normal_x_spd.bin",
        "HBroWalkingTimerData": "tables/hbro_walking_timer.bin",
        "FirebarSpinSpdData": "tables/firebar_spin_spd.bin",
        "FirebarSpinDirData": "tables/firebar_spin_dir.bin",
        "FlameYMFAdderData": "tables/flame_ymf_adder.bin",
        "PlatPosDataLow": "tables/plat_pos_low.bin",
        "PlatPosDataHigh": "tables/plat_pos_high.bin",
        "HammerBroJumpLData": "tables/hammer_bro_jump_l.bin",
        "PlayerPosSPlatData": "tables/player_pos_splat.bin",
        "SolidMTileUpperExt": "tables/solid_mtile_upper.bin",
        "ClimbMTileUpperExt": "tables/climb_mtile_upper.bin",
        "DefaultBlockObjTiles": "tables/default_block_obj_tiles.bin",
        "X_SubtracterData": "tables/x_subtracter.bin",
        "OffscrJoypadBitsData": "tables/offscr_joypad_bits.bin",
        "JumpingCoinTiles": "tables/jumping_coin_tiles.bin",
        "StarFlagYPosAdder": "tables/star_flag_y_adder.bin",
        "StarFlagXPosAdder": "tables/star_flag_x_adder.bin",
        "StarFlagTileData": "tables/star_flag_tiles.bin",
        "IntermediatePlayerData": "tables/intermediate_player.bin",
    }
    if label not in mapping:
        raise SystemExit(f"no asset path for {label}")
    return mapping[label]


# Tables extracted from labeled .db blobs. Order is source appearance within
# each ASM file so adjacent ROM tables can be grouped when a short blob is
# not unique by itself. Labels with code between them are listed separately.
MAIN_REMAINING = [
    "FloateyNumTileData", "ScoreUpdateData",
    "ColorRotatePalette", "BlankPalette", "Palette3Data", "BlockGfxData",
    "TopStatusBarLine", "WorldLivesDisplay", "TwoPlayerTimeUp", "OnePlayerTimeUp",
    "TwoPlayerGameOver", "OnePlayerGameOver", "WarpZoneWelcome",
    "StatusBarData", "StatusBarOffset",
    "DefaultSprOffsets", "Sprite0Data",
    "MusicSelectData", "HalfwayPageNybbles",
    "BSceneDataOffsets", "BackSceneryData", "BackSceneryMetatiles",
    "FSceneDataOffsets", "ForeSceneryData",
    "BlockBuffLowBounds",
    "FrenzyIDData", "PulleyRopeMetatiles", "CastleMetatiles",
    "SidePipeShaftData", "SidePipeTopPart", "SidePipeBottomPart", "VerticalPipeData",
    "CoinMetatileData", "C_ObjectRow", "C_ObjectMetatile",
    "SolidBlockMetatiles", "BrickMetatiles",
    "StaircaseHeightData", "StaircaseRowData", "HoleMetatiles",
    "AreaDataOfsLoopback", "LoopCmdWorldNumber", "LoopCmdPageNumber",
    "LoopCmdYPosition", "BrickQBlockMetatiles",
    "CoinTallyOffsets", "ScoreOffsets", "StatusBarNybbles",
    "FlagpoleScoreMods", "FlagpoleScoreDigits",
    "Jumpspring_Y_PosData",
    "CannonBitmasks",
    "BulletBillXSpdData",
    "HammerEnemyOfsData", "HammerXSpdData",
    "BlockYPosAdderData", "MaxSpdBlockData",
    "PRDiffAdjustData", "NormalXSpdData", "HBroWalkingTimerData",
    "FirebarSpinSpdData", "FirebarSpinDirData",
    "HammerBroJumpLData",
    "PlatPosDataLow", "PlatPosDataHigh",
    "PlayerPosSPlatData",
    "SolidMTileUpperExt", "ClimbMTileUpperExt",
    "DefaultBlockObjTiles",
    "StarFlagYPosAdder", "StarFlagXPosAdder", "StarFlagTileData",
    "JumpingCoinTiles",
    "VineHeightData",
    "FlyCCXPositionData", "FlyCCXSpeedData", "FlyCCTimerData",
    "FlameYPosData", "FlameYMFAdderData",
    "FireworksXPosData", "FireworksYPosData",
    "Bitmasks", "Enemy17YPosData", "SwimCC_IDData",
    "HammerThrowTmrData", "XSpeedAdderData", "RevivedXSpeed", "LakituDiffAdj",
    "FirebarPosLookupTbl", "FirebarMirrorData", "FirebarTblOffsets", "FirebarYPos",
    "BridgeCollapseData",
    "PRandomRange", "FlameTimerData",
    "BowserIdentities", "KickedShellXSpdData", "KickedShellPtsData",
    "SetBitsMask", "ClearBitsMask",
    "ClimbXPosAdder", "ClimbPLocAdder", "FlagpoleYPosData",
    "BoundBoxCtrlData",
    "BlockBufferAdderData", "BlockBuffer_X_Adder", "BlockBuffer_Y_Adder",
    "FirstSprXPos", "FirstSprYPos", "SecondSprXPos", "SecondSprYPos",
    "FirstSprTilenum", "SecondSprTilenum", "HammerSprAttrib",
    "FlagpoleScoreNumTiles",
    "PowerUpGfxTable", "PowerUpAttributes",
    "EnemyGraphicsTable", "EnemyGfxTableOffsets", "EnemyAttributeData",
    "EnemyAnimTimingBMask", "JumpspringFrameOffsets",
    "EnemyBGCStateData",
    "XOffscreenBitsData", "DefaultXOnscreenOfs",
    "YOffscreenBitsData", "DefaultYOnscreenOfs", "HighPosUnitData",
    "SwimStompEnvelopeData", "ExtraLifeFreqData", "PowerUpGrabFreqData",
    "PUp_VGrow_FreqData", "BrickShatterFreqData",
    "ExplosionTiles",
    "FireballXSpdData", "Bubble_MForceData", "BubbleTimerData",
    "PRandomSubtracter", "FlyCCBPriority",
]
OTHER_REMAINING = [
    "AreaPalette", "BGColorCtrl_Addr",
    "DemoActionData", "DemoTimingData",
    "WSelectBufferTemplate", "MushroomIconData",
    "PlayerStarting_X_Pos", "AltYPosOffset", "PlayerStarting_Y_Pos",
    "PlayerBGPriorityData", "GameTimerData", "Hidden1UpCoinAmts",
    "EnemyAddrHOffsets", "AreaDataHOffsets",
]
GAME_TEXT_LABELS = [
    "TopStatusBarLine", "WorldLivesDisplay",
    "TwoPlayerTimeUp", "OnePlayerTimeUp",
    "TwoPlayerGameOver", "OnePlayerGameOver", "WarpZoneWelcome",
]
WORLD_AREA_LABELS = [f"World{i}Areas" for i in range(1, 9)]


def all_finds(haystack, needle):
    hits = []
    i = 0
    while True:
        j = haystack.find(needle, i)
        if j < 0:
            return hits
        hits.append(j)
        i = j + 1


def extract_tables_unique(prg, blobs, labels, out_dir):
    pending = list(labels)
    located = {}

    def write_label(name, start):
        payload = blobs[name]
        if name == "PowerUpGrabFreqData":
            payload = payload[:27]
        if prg[start:start + len(payload)] != payload:
            raise SystemExit(f"{name}: ROM slice mismatch")
        write_file(out_dir, table_relpath(name), payload, name, start)
        located[name] = start

    changed = True
    while pending and changed:
        changed = False
        still = []
        for name in pending:
            if name not in blobs:
                raise SystemExit(f"missing table {name} in disassembly")
            payload = blobs[name]
            hits = all_finds(prg, payload)
            if not hits:
                raise SystemExit(f"{name}: not found in ROM")
            if len(hits) == 1:
                write_label(name, hits[0])
                changed = True
            else:
                still.append(name)
        pending = still
        if not pending:
            break

        index = {name: i for i, name in enumerate(labels)}
        still = []
        for name in pending:
            payload = blobs[name]
            idx = index[name]
            lo = 0
            hi = len(prg)
            for other, start in located.items():
                oi = index.get(other)
                if oi is None:
                    continue
                end = start + len(blobs[other])
                if oi < idx:
                    lo = max(lo, end)
                elif oi > idx:
                    hi = min(hi, start)
            hits = [h for h in all_finds(prg, payload) if lo <= h < hi]
            if len(hits) == 1:
                write_label(name, hits[0])
                changed = True
            else:
                still.append(name)
        pending = still

    if pending:
        raise SystemExit(
            "ambiguous tables: " + ", ".join(pending)
        )
    return located


def extract_window(prg, blobs, labels, before, after, relpath, out_dir):
    missing = [n for n in labels if n not in blobs]
    if missing:
        raise SystemExit(f"missing table {missing} for window {relpath}")
    concat = b"".join(blobs[n] for n in labels)
    candidates = []
    i = 0
    while True:
        start = prg.find(concat, i)
        if start < 0:
            break
        lo = start - before
        hi = start + len(concat) + after
        if lo >= 0 and hi <= len(prg):
            window = prg[lo:hi]
            if prg.find(window, prg.find(window) + 1) < 0:
                candidates.append(window)
        i = start + 1
    unique = []
    for window in candidates:
        if window not in unique:
            unique.append(window)
    if len(unique) != 1:
        raise SystemExit(
            f"{relpath}: could not uniquely locate window "
            f"({len(unique)} candidates, core {labels})"
        )
    write_file(out_dir, relpath, unique[0])


def stream_filename(label, prefix):
    match = re.match(r"^[EL]_([A-Za-z]+)Area(\d+)$", label)
    if not match:
        raise SystemExit(f"unexpected stream label {label}")
    return f"{prefix}/{match.group(1).lower()}_{int(match.group(2))}.bin"


def extract_label_group(prg, blobs, labels, dest_prefix, out_dir):
    missing = [n for n in labels if n not in blobs]
    if missing:
        raise SystemExit(f"disassembly missing labels: {missing}")
    ordered = [blobs[n] for n in labels]
    concat = b"".join(ordered)
    start = find_unique(prg, concat, dest_prefix)
    cursor = start
    for name, payload in zip(labels, ordered):
        if prg[cursor:cursor + len(payload)] != payload:
            raise SystemExit(f"{name}: ROM slice mismatch at PRG ${cursor:04X}")
        write_file(out_dir, stream_filename(name, dest_prefix), payload,
                   name, cursor)
        cursor += len(payload)


def extract_world_tables(prg, blobs, out_dir):
    missing = [n for n in WORLD_AREA_LABELS if n not in blobs]
    if missing:
        raise SystemExit(f"missing world area tables: {missing}")
    concat = b"".join(blobs[n] for n in WORLD_AREA_LABELS)
    start = find_unique(prg, concat, "AreaAddrOffsets")
    if start < 8:
        raise SystemExit("WorldAddrOffsets: no room before AreaAddrOffsets")
    cursor = start
    for world, name in enumerate(WORLD_AREA_LABELS, 1):
        payload = blobs[name]
        write_file(out_dir, f"tables/world_{world}_areas.bin", payload,
                   name, cursor)
        cursor += len(payload)


def extract_game_text(prg, blobs, out_dir):
    missing = [n for n in GAME_TEXT_LABELS if n not in blobs]
    if missing:
        raise SystemExit(f"missing game text: {missing}")
    concat = b"".join(blobs[n] for n in GAME_TEXT_LABELS)
    game_text_off = find_unique(prg, concat, "GameText")
    luigi_off = game_text_off + len(concat)
    luigi = blobs.get("LuigiName")
    if not luigi:
        raise SystemExit("LuigiName missing from disassembly")
    if prg[luigi_off:luigi_off + len(luigi)] != luigi:
        raise SystemExit("LuigiName: ROM slice mismatch after GameText")
    write_file(out_dir, "tables/luigi_name.bin", luigi, "LuigiName",
               luigi_off)
    # GameTextOffsets is structural metadata. The C loader derives message
    # starts by scanning these seven $ff-terminated records.


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 tools/extract_assets.py <rom.nes> <output_dir>")
        sys.exit(1)
    rom_path, out_dir = sys.argv[1], sys.argv[2]
    if not os.path.isfile(rom_path):
        raise SystemExit(f"ROM not found: {rom_path}")

    print(f"Extracting {rom_path} -> {out_dir}")
    global PARSED_CONSTANTS
    PARSED_CONSTANTS = load_named_constants()
    rom, prg, chr_data = read_ines(rom_path)
    os.makedirs(out_dir, exist_ok=True)
    for rel in OBSOLETE_GENERATED_ASSETS:
        obsolete = os.path.join(out_dir, rel)
        if os.path.isfile(obsolete):
            os.unlink(obsolete)

    disasm = os.path.join(repo_root(), "smb1-disasm")
    blobs = collect_labeled_dbs([
        os.path.join(disasm, "src", "levels.asm"),
        os.path.join(disasm, "main.asm"),
        os.path.join(disasm, "engine", "game-mode", "player-movement.asm"),
        os.path.join(disasm, "engine", "screen", "routine", "colors.asm"),
        os.path.join(disasm, "engine", "title-mode", "demo.asm"),
        os.path.join(disasm, "engine", "title-mode", "titlescreen.asm"),
        os.path.join(disasm, "engine", "game-mode", "routine", "game-timer-setup.asm"),
        os.path.join(disasm, "engine", "game-mode", "routine", "player-end-level.asm"),
        os.path.join(disasm, "engine", "game-mode", "scroll.asm"),
    ])

    write_file(out_dir, "tiles.chr", chr_data, "CHR", 0)
    write_file(out_dir, "tables/title_rle.bin",
               chr_data[TITLE_CHR_OFF:TITLE_CHR_OFF + TITLE_LEN],
               "TitleScreenData", TITLE_CHR_OFF)

    music_off = cpu_to_prg(MUSIC_CPU)
    music_len = MUSIC_END_INCLUSIVE - MUSIC_CPU + 1
    write_file(out_dir, "audio/music_data.bin",
               prg[music_off:music_off + music_len],
               "MusicHeaderData..BrickShatterEnvData", music_off)

    extract_label_group(prg, blobs, AREA_LABELS, "areas", out_dir)
    extract_label_group(prg, blobs, ENEMY_LABELS, "enemies", out_dir)

    for group in TABLE_GROUPS:
        items = []
        for label in group:
            if label not in blobs:
                raise SystemExit(f"missing table {label} in disassembly")
            items.append((label, table_relpath(label), blobs[label]))
        concat = b"".join(item[2] for item in items)
        start = find_unique(prg, concat, "+".join(group))
        cursor = start
        for label, rel, payload in items:
            if prg[cursor:cursor + len(payload)] != payload:
                raise SystemExit(f"{label}: ROM slice mismatch")
            write_file(out_dir, rel, payload, label, cursor)
            cursor += len(payload)

    located = {}
    wanted = set(MAIN_REMAINING + OTHER_REMAINING)
    asm_files = [
        os.path.join(disasm, "main.asm"),
        os.path.join(disasm, "src", "levels.asm"),
        os.path.join(disasm, "engine", "game-mode", "player-movement.asm"),
        os.path.join(disasm, "engine", "screen", "routine", "colors.asm"),
        os.path.join(disasm, "engine", "title-mode", "demo.asm"),
        os.path.join(disasm, "engine", "title-mode", "titlescreen.asm"),
        os.path.join(disasm, "engine", "game-mode", "routine", "game-timer-setup.asm"),
        os.path.join(disasm, "engine", "game-mode", "routine", "player-end-level.asm"),
        os.path.join(disasm, "engine", "game-mode", "scroll.asm"),
    ]
    for path in asm_files:
        file_blobs = collect_labeled_dbs([path])
        labels = [name for name in file_blobs if name in wanted]
        if labels:
            located.update(extract_tables_unique(prg, file_blobs, labels, out_dir))

    extract_world_tables(prg, blobs, out_dir)
    extract_game_text(prg, blobs, out_dir)

    def window_from_label(label, before, after, relpath, extra=0):
        if label not in located:
            raise SystemExit(f"{relpath}: {label} was not located")
        start = located[label]
        payload_len = extra if extra else len(blobs[label])
        lo = start - before
        hi = start + payload_len + after
        write_file(out_dir, relpath, prg[lo:hi], label, lo,
                   "rom-layout-window")

    window_from_label("Bubble_MForceData", 0, 0,
                      "compat-rom-windows/bubble.bin", extra=258)
    # FireballObjCore (main.asm:3257-3259): ldy PlayerFacingDir / dey /
    # lda FireballXSpdData,y.  C indexes that same Y+1 FacingDir value, so
    # slot 0 is the 6502 underflow at table+$FF ($A9), not the byte before
    # the two-byte table.
    if "FireballXSpdData" not in located:
        raise SystemExit("compat fireball window: FireballXSpdData was not located")
    fireball_x = located["FireballXSpdData"]
    write_file(out_dir, "compat-rom-windows/fireball_x_speed.bin", bytes([
        prg[fireball_x + 0xFF],
        prg[fireball_x + 0],
        prg[fireball_x + 1],
        prg[fireball_x + 2],
    ]), "FireballXSpdData indexed at -1..2", fireball_x,
       "discontiguous-rom-layout-window")
    window_from_label("PRandomSubtracter", 0, 0,
                      "compat-rom-windows/prandom_subtracter.bin", extra=16)
    window_from_label("FlyCCBPriority", 0, 0,
                      "compat-rom-windows/fly_cc_priority.bin", extra=16)
    window_from_label("ClimbXPosAdder", 1, 1,
                      "compat-rom-windows/climb_adder.bin", extra=4)

    manifest = {
        "schema": "smb2-assets-v1",
        "source_rom_sha1": EXPECTED_SHA1,
        "assets": sorted(OUTPUT_RECORDS, key=lambda item: item["path"]),
    }
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2, sort_keys=True)
        fh.write("\n")

    stamp = os.path.join(out_dir, ".extracted")
    with open(stamp, "w", encoding="utf-8") as fh:
        fh.write(EXPECTED_SHA1 + "\n")
    print("Complete.")


if __name__ == "__main__":
    main()
