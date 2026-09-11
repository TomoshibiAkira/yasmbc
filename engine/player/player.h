/* engine/player/player.h - Player system interface */

#ifndef SMB_PLAYER_H
#define SMB_PLAYER_H

#include "constants/types.h"

/* Public interface - called from game-mode/core.c */
void Player_Init(void);
void Player_UpdateControl(void);
void Player_MovementSubs(void);
void Player_UpdateMovingDirection(void);
void Player_GetAnimSpeed(void);
void Player_AnimationControl(void);
void Player_UpdateOffscreenBits(void);
void Player_UpdateSprite(void);
void Player_ColorRotation(void);
void Player_ApplyWhirlpoolGravity(void);
int Player_LoadRomTables(void);

#endif /* SMB_PLAYER_H */
