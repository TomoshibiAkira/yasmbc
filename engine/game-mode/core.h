/* engine/game-mode/core.h - GameCoreRoutine / GameEngine */

#ifndef SMB_GAME_MODE_CORE_H
#define SMB_GAME_MODE_CORE_H

#include "constants/types.h"

void GameMode_MainLoop(void);
void GameMode_AutoControl(uint8_t input);
void GameMode_UpdateAreaMusicQueue(void);
int GameMode_TransposePlayers(void);
void GameMode_ContinueGame(void);
void GameMode_TerminateGame(void);
void PlayerGfxHandler(void);
int GameMode_LoadRom(void);

#endif /* SMB_GAME_MODE_CORE_H */
