/* Shared SprDataOffset pool (main.asm:69-111, 1513-1518). */
#ifndef SMB_SPRITE_OFFSETS_H
#define SMB_SPRITE_OFFSETS_H

#include "constants/types.h"

int SpriteOffsets_LoadRom(void);
void SpriteOffsets_InstallSprite0(uint8_t *sprite_data);
void SpriteOffsets_Reset(void);
void SpriteOffsets_ClearMemory(void);
void SpriteOffsets_Shuffle(void);
uint8_t SpriteOffsets_GetControl(void);
void SpriteOffsets_SetControl(uint8_t control);
/* Canonical RAM encoder support: copies SprShuffleAmtOffset ($06e0),
 * SprDataOffset_Ctrl ($03ee), and the 25-byte SprDataOffset pool ($06e4). */
void SpriteOffsets_GetVerifierState(uint8_t *shuffle_offset,
                                    uint8_t *control,
                                    uint8_t offsets[25]);

uint8_t SpriteOffset_Player(void);
uint8_t SpriteOffset_Enemy(uint8_t slot);
uint8_t SpriteOffset_Fireball(uint8_t slot);
uint8_t SpriteOffset_FireballExplosion(uint8_t slot);
uint8_t SpriteOffset_Alt(uint8_t slot);
uint8_t SpriteOffset_Bubble(uint8_t slot);
uint8_t SpriteOffset_Block(uint8_t slot);
uint8_t SpriteOffset_MiscRaw(uint8_t slot);
uint8_t SpriteOffset_Misc(uint8_t slot);
uint8_t SpriteOffset_Floaty(void);

#endif
