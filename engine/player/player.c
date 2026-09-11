/* engine/player/player.c - Player ROM-table load dispatch */

#include "player/player.h"

int Player_LoadPhysicsTables(void);
int Player_LoadSpriteTables(void);

int Player_LoadRomTables(void) {
    if (Player_LoadPhysicsTables() != 0)
        return -1;
    return Player_LoadSpriteTables();
}
