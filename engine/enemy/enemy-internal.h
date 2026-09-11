/* engine/enemy/enemy-internal.h - Shared types and declarations for split enemy TUs */
#ifndef SMB_ENEMY_INTERNAL_H
#define SMB_ENEMY_INTERNAL_H

#include "enemy/enemy.h"
#include "spr-object.h"

#define GOOMBA_ID 0x06
#define ENEMY_SLOT_COUNT 6
#define ENEMY_ALLOC_COUNT 5
#define POWERUP_SLOT 5
#define FIREBALL_SLOT_COUNT 2

typedef struct {
    uint8_t ul_x;
    uint8_t ul_y;
    uint8_t lr_x;
    uint8_t lr_y;
} CollisionBox;

typedef struct {
    uint8_t flag;
    uint8_t id;
    uint8_t page, x, y, y_high;
    uint8_t rel_x, rel_y, offscreen_bits;
    uint8_t bbox_ctrl, offscreen_masked;
    uint8_t bbox_ul_x, bbox_ul_y, bbox_lr_x, bbox_lr_y;
    uint8_t collision_bits;
    uint8_t shell_chain_counter;
    uint8_t state;
    uint8_t moving_dir;
    uint8_t firebar_spin_speed;
    uint8_t firebar_spin_direction;
    union {
        uint8_t x_speed;
        uint8_t y_platform_center;
        uint8_t x_move_secondary_counter;
    };
    union {
        uint8_t x_mf;
        uint8_t y_platform_top;
    };
    uint8_t y_speed, y_mf, y_dummy;
    uint8_t spr_attrib;
    uint8_t interval_timer;
    union {
        uint8_t hammer_throw_timer;
        uint8_t platform_collision_flag;
    };
    uint8_t hammer_jump_timer;
    uint8_t duplicate_slot;
    uint8_t floaty_control, floaty_timer, floaty_y, floaty_x;
} EnemySlot;

typedef struct {
    uint8_t body_controls;
    uint8_t feet_counter;
    uint8_t movement_speed;
    uint8_t orig_x;
    uint8_t flame_timer_ctrl;
    uint8_t front_slot;
    uint8_t bridge_offset;
    uint8_t gfx_flag;
    uint8_t hit_points;
    uint8_t max_range;
    uint8_t rear_slot;
} BowserState;

typedef struct {
    uint8_t state;
    uint8_t page, x;
    uint8_t y_high, y;
    uint8_t x_speed, y_speed;
    uint8_t bbox_ctrl;
    uint8_t x_mf, y_mf, y_dummy;
    uint8_t rel_x, rel_y;
    uint8_t offscreen_bits;
    uint8_t bouncing;
} FireballSlot;

enum EnemyRowFlipMode {
    ENEMY_ROWS_UNFLIPPED = 0,
    ENEMY_ROWS_SWAP_MIDDLE = 1,
    ENEMY_ROWS_SWAP_OUTER = 2
};

typedef enum {
    ENEMY_INIT_NO_CODE = 0,
    ENEMY_INIT_NORMAL,
    ENEMY_INIT_RED_KOOPA,
    ENEMY_INIT_GOOMBA,
    ENEMY_INIT_BLOOBER,
    ENEMY_INIT_BULLET_BILL,
    ENEMY_INIT_CHEEP,
    ENEMY_INIT_PODOBOO,
    ENEMY_INIT_PIRANHA,
    ENEMY_INIT_JUMPING_TROOPA,
    ENEMY_INIT_RED_PARATROOPA,
    ENEMY_INIT_HORIZ_FLY_SWIM,
    ENEMY_INIT_HAMMER,
    ENEMY_INIT_LAKITU,
    ENEMY_INIT_FIREBAR_SHORT,
    ENEMY_INIT_FIREBAR_LONG,
    ENEMY_INIT_BOWSER,
    ENEMY_INIT_BOWSER_FLAME,
    ENEMY_INIT_RETAINER,
    ENEMY_INIT_BAL_PLATFORM,
    ENEMY_INIT_VERT_PLATFORM,
    ENEMY_INIT_LARGE_LIFT_UP,
    ENEMY_INIT_LARGE_LIFT_DOWN,
    ENEMY_INIT_HORI_PLATFORM,
    ENEMY_INIT_DROP_PLATFORM,
    ENEMY_INIT_SMALL_LIFT_UP,
    ENEMY_INIT_SMALL_LIFT_DOWN,
    ENEMY_INIT_FRENZY,
    ENEMY_INIT_END_FRENZY,
    ENEMY_INIT_VINE,
    ENEMY_INIT_DEFERRED
} EnemyInitKind;

enum {
    ENEMY_RUN_NORMAL = 0,
    ENEMY_RUN_BOWSER_FLAME,
    ENEMY_RUN_FIREWORKS,
    ENEMY_RUN_NO_CODE,
    ENEMY_RUN_FIREBAR,
    ENEMY_RUN_LARGE_PLATFORM,
    ENEMY_RUN_SMALL_PLATFORM,
    ENEMY_RUN_BOWSER,
    ENEMY_RUN_POWERUP,
    ENEMY_RUN_VINE,
    ENEMY_RUN_STAR_FLAG,
    ENEMY_RUN_JUMPSPRING,
    ENEMY_RUN_WARP_ZONE,
    ENEMY_RUN_RETAINER,
    ENEMY_RUN_DEFERRED
};

extern EnemySlot enemies[ENEMY_SLOT_COUNT];
extern uint8_t s_Enemy_OffscreenBits;
extern FireballSlot fireballs[FIREBALL_SLOT_COUNT];
extern uint8_t s_PowerUpType;
extern uint8_t s_LakituReappearTimer;
extern BowserState s_Bowser;
extern uint8_t s_StompChainCounter;
extern uint8_t s_BowserFlameTimerData[8];
extern uint8_t s_BowserPRandomRange[4];
extern uint8_t s_BoundBoxCtrlData[12][4];
extern uint8_t s_PRandomSubtracterRomWindow[16];
extern uint8_t s_FlyCCBPriorityRomWindow[16];
extern uint8_t s_PRDiffAdjustData[3][4];
extern uint8_t s_ExplosionTiles[3];
extern uint8_t s_FlagpoleScoreMods[5];
extern uint8_t s_FlagpoleScoreDigits[5];
extern uint8_t s_BridgeCollapseData[15];
extern const uint8_t s_EnemyRunDispatch[0x36];

uint8_t next_bowser_flame_timer(void);
void Enemy_SetTitleBufferAliases(const uint8_t *source);
void Enemy_ClearTitleBufferAliases(void);
void Enemy_GetVerifierState(EnemyVerifierState *out);
void draw_enemy_rows(const EnemySlot* e, uint8_t sprite_base,
                            const uint8_t* gfx, uint8_t base_y,
                            uint8_t moving_dir, const uint8_t attrs[6],
                            uint8_t row_flip_mode, uint8_t offscreen_bits);
uint8_t enemy_slot_index(const EnemySlot* e);
uint8_t firebar_direction_value(const EnemySlot* e);
uint8_t enemy_try_spawn_hammer(uint8_t object_offset);
uint8_t duplicate_enemy_slot(uint8_t owner_slot);
uint8_t linked_duplicate_slot(uint8_t owner_slot);
void set_enemy_interval_timer(EnemySlot* e, uint8_t value);
void set_enemy_frame_timer(EnemySlot* e, uint8_t value);
void erase_enemy(EnemySlot* e);
void Enemy_Reset(void);
uint8_t Enemy_GetFireballY(uint8_t slot);
void Enemy_AreaFrenzy(uint8_t area_object_parameter);
void Enemy_ApplyLoopback(void);
void Enemy_KillAllForLoop(void);
void Enemy_KillByID(uint8_t id);
void Enemy_SetupFlagpole(uint8_t page, uint8_t x);
void Enemy_SetupStarFlag(uint8_t page, uint8_t x, uint8_t y);
void Enemy_SetupJumpspring(uint8_t page, uint8_t x, uint8_t y);
void Enemy_SetupPiranhaPlant(uint8_t page, uint8_t x, uint8_t y);
uint8_t Enemy_FlagpoleSlideInput(uint8_t *input);
void Enemy_SetupEntranceVine(void);
void Enemy_SetEntranceBubbleAlias(uint8_t x_position, uint8_t page_loc,
                                  uint8_t y_high_position,
                                  uint8_t y_position);
void setup_vine_record(EnemySlot *e, uint8_t page, uint8_t x,
                              uint8_t y);
void Enemy_SetupVineFromBlock(uint8_t page, uint8_t x, uint8_t y);
void init_vine_from_parser_source(EnemySlot *e, uint8_t source_index);
void init_vine_from_jump_engine(EnemySlot *e);
void Enemy_SetupPowerUp(uint8_t page, uint8_t x, uint8_t y,
                        uint8_t requested_type);
void Enemy_ResetStompChain(void);
uint16_t screen_right(void);
uint8_t enemy_offscreen_bits(EnemySlot* e);
void draw_vine_segment(const EnemySlot *e, uint8_t slot,
                              uint8_t segment);
void run_vine(EnemySlot* e, uint8_t slot);
void init_normal_enemy(EnemySlot* e);
void init_small_bbox(EnemySlot* e);
void init_flying_cheep_cheep(EnemySlot* e, uint8_t object_offset);
void init_tall_bbox(EnemySlot* e);
void init_red_paratroopa(EnemySlot* e);
void init_hammer_bro(EnemySlot* e);
void init_lakitu(EnemySlot* e);
void init_spiny(EnemySlot* e);
void init_firebar(EnemySlot* e);
void init_bowser(EnemySlot* e);
void init_bowser_flame(EnemySlot* e);
void init_enemy_frenzy(EnemySlot* e, uint8_t object_offset);
void end_enemy_frenzy(EnemySlot* e);
void position_platform(EnemySlot* e, uint8_t table_index);
void init_platform_common(EnemySlot* e);
void init_balance_platform(EnemySlot* e);
void init_vertical_platform(EnemySlot* e);
void init_small_lift(EnemySlot* e, uint8_t upwards);
void init_large_lift(EnemySlot* e, uint8_t upwards);
void init_platform(EnemySlot* e, EnemyInitKind kind);
void init_checkpointed_enemy(EnemySlot* e);
void init_enemy_slot(uint8_t slot, uint8_t id, uint8_t page,
                            uint8_t x, uint8_t y, uint8_t reset_state);
void spawn_group(uint8_t diff);
void check_frenzy_buffer(uint8_t object_offset);
int parse_row0e(const uint8_t *stream, uint16_t stream_len,
                       uint16_t cursor);
void parse_stream(uint8_t object_offset);
uint8_t move_enemy_x(EnemySlot* e);
uint8_t enemy_player_diff_with_carry(const EnemySlot* e,
                                            uint8_t* negative,
                                            uint8_t* no_borrow);
uint8_t enemy_player_diff(const EnemySlot* e, uint8_t* negative);
void move_enemy_world_x(EnemySlot* e, uint8_t amount, uint8_t right);
void move_enemy_gravity(EnemySlot* e, uint8_t downward,
                               uint8_t upward, uint8_t max_speed);
void move_slow_special_enemy(EnemySlot* e);
void move_bloober(EnemySlot* e);
void move_bullet_bill(EnemySlot* e);
void move_flying_cheep_cheep(EnemySlot* e);
void move_swimming_cheep(EnemySlot* e);
void move_podoboo(EnemySlot* e);
void move_piranha(EnemySlot* e);
void enemy_offscreen_bounds(EnemySlot* e);
void check_enemy_side(EnemySlot* e);
void enemy_apply_no_bg_state(EnemySlot* e);
void enemy_landing(EnemySlot* e);
void enemy_process_direction(EnemySlot *e);
uint8_t enemy_ground_side_check(EnemySlot* e);
void hammer_bro_bg_check(EnemySlot* e);
void enemy_jumping_bg_check(EnemySlot* e);
void enemy_to_bg_collision_det(EnemySlot *e);
void enemy_get_bound_box(EnemySlot* e);
void make_collision_box(uint8_t rel_x, uint8_t rel_y,
                               uint8_t bbox_ctrl, CollisionBox* box);
uint8_t collision_axis(uint8_t first_ul, uint8_t first_lr,
                              uint8_t second_ul, uint8_t second_lr);
uint8_t collision_boxes_overlap(const CollisionBox* enemy,
                                       const CollisionBox* player);
uint8_t player_collision_vertical_ok(void);
uint8_t enemy_face_player(EnemySlot* e);
void force_injury(void);
void injure_player(void);
void Enemy_ForceInjury(void);
void stomp_shellable_enemy(EnemySlot* e);
void stomp_special_enemy(EnemySlot* e, uint8_t points);
void demote_paratroopa(EnemySlot* e);
void stomp_enemy(EnemySlot* e);
void check_to_stun_enemy(EnemySlot* e, uint8_t classification);
void shell_or_block_defeat(EnemySlot* e);
void defeat_enemy_by_star(EnemySlot* e);
void kick_enemy_shell(EnemySlot* e);
void check_player_injury(EnemySlot* e);
void player_enemy_collision(EnemySlot* e);
uint8_t Enemy_CheckHammerCollision(uint8_t bbox_ul_x, uint8_t bbox_ul_y,
                                   uint8_t bbox_lr_x, uint8_t bbox_lr_y);
void Enemy_HammerInjury(void);
void enemy_turn_around(EnemySlot* e);
void proc_enemy_collisions(EnemySlot* current, EnemySlot* other);
void enemy_enemy_collision(EnemySlot* current, uint8_t current_slot);
uint8_t fireball_metatile_is_nonsolid(uint8_t metatile);
void fireball_bg_collision(FireballSlot* f);
void defeat_enemy_by_fireball(EnemySlot* e);
uint8_t fireball_defeats_enemy(const EnemySlot *e);
void hurt_bowser_by_fireball(EnemySlot *hit, EnemySlot *bowser);
void fireball_enemy_collision(FireballSlot* f, uint8_t slot);
void draw_fireball(const FireballSlot* f, uint8_t slot);
void draw_fireball_explosion(FireballSlot* f, uint8_t slot);
void fireball_process(uint8_t slot);
void draw_floaty(EnemySlot* e, uint8_t oam_byte_offset);
uint8_t floaty_oam_offset(const EnemySlot* e, int slot);
void move_enemy_vertically(EnemySlot* e);
void move_enemy_steady(EnemySlot* e, uint8_t slow);
void move_normal_enemy(EnemySlot* e);
void move_jumping_paratroopa(EnemySlot* e);
void move_red_paratroopa(EnemySlot* e);
uint8_t move_with_xm_counters(EnemySlot* e);
void move_flying_paratroopa(EnemySlot* e);
void move_hammer_bro(EnemySlot* e);
uint8_t player_lakitu_diff_with_adjust(
    EnemySlot* e, const uint8_t adjust_data[3]);
uint8_t player_lakitu_diff(EnemySlot* e);
void put_enemy_at_right_extent(EnemySlot* e, uint8_t y);
void set_17_id_and_spawn(EnemySlot* e, uint8_t object_offset);
void bullet_bill_cheep_cheep(EnemySlot* e, uint8_t object_offset);
void init_fireworks(EnemySlot* e);
void draw_fireworks_explosion(const EnemySlot* e, uint8_t slot,
                                     uint8_t animation);
void run_fireworks(EnemySlot* e, uint8_t slot);
void draw_star_flag(const EnemySlot* e, uint8_t slot);
void run_star_flag(EnemySlot* e, uint8_t slot);
void frenzy_lakitu_and_spiny(EnemySlot* current,
                                    uint8_t current_offset);
void move_lakitu(EnemySlot* e);
void move_spiny(EnemySlot* e);
void enemy_movement_dispatch(EnemySlot* e);
void draw_goomba(const EnemySlot* e, int slot_base);
void enemy_graphics_offscreen_cleanup(EnemySlot *e);
void draw_special_enemy(const EnemySlot* e, uint8_t slot);
void run_jumpspring_enemy(EnemySlot* e, uint8_t slot);
void draw_koopa_family(const EnemySlot* e, uint8_t slot);
void platform_write_bound_box(EnemySlot *e);
void platform_get_bound_box(EnemySlot *e, uint8_t small);
void position_player_on_vplat(const EnemySlot *platform,
                                     uint8_t platform_y);
void position_player_on_splat(const EnemySlot *platform,
                                     uint8_t collision_flag);
void position_player_on_hplat(uint8_t displacement);
uint8_t platform_collision_box(EnemySlot *owner,
                                      EnemySlot *surface,
                                      uint8_t small, uint8_t box_index);
void platform_collision(EnemySlot *e, uint8_t small);
void draw_large_platform(const EnemySlot *e, uint8_t slot);
void draw_small_platform(const EnemySlot *e, uint8_t slot);
void move_lift_platforms(EnemySlot *e);
void move_platform_gravity(EnemySlot *e, uint8_t upwards);
void stop_platforms(EnemySlot *e, uint8_t y_target);
void init_platform_fall(EnemySlot *current, EnemySlot *other);
void move_falling_platform(EnemySlot *e);
void platform_fall(EnemySlot *e, EnemySlot *other);
void setup_platform_rope(const EnemySlot *platform, uint8_t speed,
                                uint8_t *high, uint8_t *low);
void write_platform_rope(const EnemySlot *e, const EnemySlot *other,
                                uint8_t speed);
void balance_platform(EnemySlot *e, uint8_t slot);
void vertical_platform(EnemySlot *e);
void horizontal_platform(EnemySlot *e, uint8_t right_platform);
void right_platform(EnemySlot *e);
void drop_platform(EnemySlot *e);
void run_large_platform(EnemySlot *e, uint8_t slot);
void run_small_platform(EnemySlot *e, uint8_t slot);
void draw_powerup(const EnemySlot* e);
void powerup_offscreen_bounds(EnemySlot* e);
uint8_t powerup_boxes_overlap(const EnemySlot* e);
void setup_floaty_number(EnemySlot* e, uint8_t control);
void handle_powerup_collision(EnemySlot* e);
void powerup_player_collision(EnemySlot* e);
void process_powerup(EnemySlot* e);
uint8_t byte_abs_delta(uint8_t left, uint8_t right);
uint8_t firebar_coordinate_delta(uint8_t x, uint8_t center_x);
void firebar_spin(EnemySlot* e);
void firebar_position(const EnemySlot* e, uint8_t segment,
                             uint8_t* horizontal, uint8_t* vertical,
                             uint8_t* mirror);
void firebar_player_collision(const EnemySlot* e, uint8_t x,
                                     uint8_t y);
void draw_firebar_segment(const EnemySlot* e, uint8_t base,
                                 uint8_t segment, uint8_t center_x,
                                 uint8_t center_y);
void run_firebar(EnemySlot* e, uint8_t slot);
void draw_bowser_flame(const EnemySlot* e, uint8_t slot);
void run_bowser_flame(EnemySlot* e, uint8_t slot);
void run_bowser_half(EnemySlot* e, uint8_t slot,
                            uint8_t graphics_flag);
void run_bowser(EnemySlot* e, uint8_t slot);
void draw_flagpole(const EnemySlot *e);
void Enemy_RunFlagpoleRoutine(void);
void draw_bowser_bridge_pair(EnemySlot *e, uint8_t slot);
void Enemy_BridgeCollapse(void);
void run_retainer_enemy(EnemySlot* e);
void run_normal_enemy(EnemySlot* e, uint8_t slot);
void run_cannon_bullet(EnemySlot* e, uint8_t slot);
void run_warp_zone(EnemySlot* e);
void Enemy_ProcessCannons(void);
void Enemy_ProcessObjectPass(uint8_t i);
void Enemy_ProcessFloateyNumbersPass(uint8_t i);
void Enemies_Core(void);
void Enemy_ProcessHammers(void);
void Enemy_ProcFireballBubble(void);
SprObjectView fireball_object(FireballSlot* f);
SprObjectView platform_object(EnemySlot *e);
CollisionBox enemy_collision_box(const EnemySlot *e);
CollisionBox player_collision_box(void);
EnemySlot *fireball_bowser_target(EnemySlot *hit);

#endif
