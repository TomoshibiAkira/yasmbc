/* engine/enemy/enemy.h - Enemy object system */

#include "constants/types.h"

#ifndef ENGINE_ENEMY_H

#define ENGINE_ENEMY_H

#define ENEMY_VERIFIER_SLOT_COUNT 6
#define ENEMY_VERIFIER_FIREBALL_COUNT 2

/* Stable semantic export for the verifier.  This is intentionally arranged
 * by ASM RAM owner rather than exposing enemy.c's private host structs. */
typedef struct EnemyVerifierState {
    uint8_t flag[6], id[6], state[6], moving_dir[6];
    uint8_t page[6], x[6], y_high[6], y[6];
    uint8_t x_speed[6], y_speed[6], x_mf[6], y_mf[6], y_dummy[6];
    uint8_t collision_bits[6], bbox_ctrl[6], spr_attrib[6];
    uint8_t offscreen_masked[6];
    uint8_t firebar_spin_speed[6], firebar_spin_direction[6];
    uint8_t hammer_throw_timer[6];
    uint8_t shell_chain[6], floaty_control[6], floaty_timer[6];
    uint8_t hammer_jump_timer[6];
    uint8_t floaty_x[6], floaty_y[6], bbox[6][4];
    uint8_t current_rel_x, current_offscreen_bits;
    uint8_t fireball_state[2], fireball_page[2], fireball_x[2];
    uint8_t fireball_y_high[2], fireball_y[2];
    uint8_t fireball_x_speed[2], fireball_y_speed[2], fireball_bbox_ctrl[2];
    uint8_t fireball_bouncing[2];
    uint8_t power_up_type, lakitu_reappear_timer, stomp_chain_counter;
    uint8_t bowser[10]; /* $0363-$036a, $0483, $06dc */
} EnemyVerifierState;

/* DrawTitleScreen/ClearBuffersDrawIcon copy and clear the physical parallel
 * SprObject banks at $0401-$0406, $0417-$041c, and $0434-$0439.  These bytes
 * are represented by the corresponding enemy-slot aliases while the title
 * buffer owns that RAM. */
void Enemy_SetTitleBufferAliases(const uint8_t *source);
void Enemy_ClearTitleBufferAliases(void);

void Enemy_GetVerifierState(EnemyVerifierState *state);

int Enemy_LoadRomTables(void);

void Enemies_Core(void);
void Enemy_ProcessCannons(void);
void Enemy_ProcessObjectPass(uint8_t object_offset);
void Enemy_ProcessFloateyNumbersPass(uint8_t object_offset);
void Enemy_AreaFrenzy(uint8_t area_object_parameter);
void Enemy_ProcFireballBubble(void);
void Enemy_SetupPowerUp(uint8_t page, uint8_t x, uint8_t y,
                        uint8_t requested_type);
void Enemy_SetupVineFromBlock(uint8_t page, uint8_t x, uint8_t y);
void Enemy_SetupStarFlag(uint8_t page, uint8_t x, uint8_t y);
void Enemy_SetupJumpspring(uint8_t page, uint8_t x, uint8_t y);
void Enemy_SetupPiranhaPlant(uint8_t page, uint8_t x, uint8_t y);
void Enemy_SetupEntranceVine(void);
/* SetupBubble with X=$05 aliases the ordinary enemy arrays and EnemyDataLow;
 * keep those writes in the same enemy-RAM owner as the 6502 overlay. */
void Enemy_SetEntranceBubbleAlias(uint8_t x_position, uint8_t page_loc,
                                  uint8_t y_high_position,
                                  uint8_t y_position);
void Enemy_SetupFlagpole(uint8_t page, uint8_t x);
uint8_t Enemy_FlagpoleSlideInput(uint8_t *input);
void Enemy_RunFlagpoleRoutine(void);
void Enemy_ForceInjury(void);
void Enemy_KillByID(uint8_t id);
void Enemy_BridgeCollapse(void);
void Enemy_Reset(void);
void Enemy_ApplyLoopback(void);
void Enemy_KillAllForLoop(void);
void Enemy_ResetStompChain(void);
/* Hammer misc objects use Enemy_State/MovingDir/Page/X/Y as their live
 * producer-owned source.  These entry points keep that source in enemy.c
 * while collision.c owns the shared Misc_State slots. */
void Enemy_ProcessHammers(void);
uint8_t Enemy_CheckHammerCollision(uint8_t bbox_ul_x, uint8_t bbox_ul_y,
                                   uint8_t bbox_lr_x, uint8_t bbox_lr_y);
void Enemy_HammerInjury(void);
/* Fireball_Y_Position+$01 is the zero-page alias read by Setup_Vine's
 * JumpEngine index $60 (Block_PageLoc+$60 == $d6). */
uint8_t Enemy_GetFireballY(uint8_t slot);

#endif
