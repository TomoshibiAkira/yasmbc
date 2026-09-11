/* sfx_constants.h - Sound effect constants */
/* NOTE: Includes controller button constants for compatibility */

#ifndef SMB_SFX_CONSTANTS_H
#define SMB_SFX_CONSTANTS_H

/* Sound Effect Flags - Low Nibble */
#define SFX_SMALL_JUMP        0x80
#define SFX_FLAGPOLE          0x40
#define SFX_FIREBALL          0x20
#define SFX_PIPE_DOWN_INJURY  0x10
#define SFX_ENEMY_SMACK       0x08
#define SFX_ENEMY_STOMP       0x04
#define SFX_BUMP              0x02
#define SFX_BIG_JUMP          0x01

/* Sound Effect Flags - High Nibble */
#define SFX_BOWSER_FALL       0x80
#define SFX_EXTRA_LIFE        0x40
#define SFX_POWERUP_GRAB      0x20
#define SFX_TIMER_TICK        0x10
#define SFX_BLAST             0x08
#define SFX_GROW_VINE         0x04
#define SFX_GROW_POWERUP      0x02
#define SFX_COIN_GRAB        0x01

/* Sound Effect Flags - Extra */
#define SFX_BOWSER_FLAME      0x02
#define SFX_BRICK_SHATTER     0x01

/* Backward compatibility */
#define Sfx_SmallJump         SFX_SMALL_JUMP
#define Sfx_Flagpole          SFX_FLAGPOLE
#define Sfx_Fireball          SFX_FIREBALL
#define Sfx_PipeDown_Injury   SFX_PIPE_DOWN_INJURY
#define Sfx_EnemySmack        SFX_ENEMY_SMACK
#define Sfx_EnemyStomp        SFX_ENEMY_STOMP
#define Sfx_Bump              SFX_BUMP
#define Sfx_BigJump           SFX_BIG_JUMP

#define Sfx_BowserFall        SFX_BOWSER_FALL
#define Sfx_ExtraLife         SFX_EXTRA_LIFE
#define Sfx_PowerUpGrab       SFX_POWERUP_GRAB
#define Sfx_TimerTick         SFX_TIMER_TICK
#define Sfx_Blast             SFX_BLAST
#define Sfx_GrowVine          SFX_GROW_VINE
#define Sfx_GrowPowerUp       SFX_GROW_POWERUP
#define Sfx_CoinGrab          SFX_COIN_GRAB

#define Sfx_BowserFlame       SFX_BOWSER_FLAME
#define Sfx_BrickShatter      SFX_BRICK_SHATTER

#endif /* SMB_SFX_CONSTANTS_H */
