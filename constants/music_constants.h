/* music_constants.h - Music and event music constants */

#ifndef SMB_MUSIC_CONSTANTS_H
#define SMB_MUSIC_CONSTANTS_H

/* Music Selection Bits */
#define MUSIC_SILENCE         0x80

/* Area Music Bits */
#define MUSIC_STAR_POWER      0x40
#define MUSIC_PIPE_INTRO      0x20
#define MUSIC_CLOUD           0x10
#define MUSIC_CASTLE          0x08
#define MUSIC_UNDERGROUND      0x04
#define MUSIC_WATER           0x02
#define MUSIC_GROUND          0x01

/* Event Music Bits */
#define MUSIC_TIME_RUNNING    0x40
#define MUSIC_END_LEVEL       0x20
#define MUSIC_ALT_GAME_OVER   0x10
#define MUSIC_END_CASTLE      0x08
#define MUSIC_VICTORY         0x04
#define MUSIC_GAME_OVER       0x02
#define MUSIC_DEATH           0x01

/* Backward compatibility */
#define Silence               MUSIC_SILENCE
#define StarPowerMusic        MUSIC_STAR_POWER
#define PipeIntroMusic        MUSIC_PIPE_INTRO
#define CloudMusic            MUSIC_CLOUD
#define CastleMusic           MUSIC_CASTLE
#define UndergroundMusic       MUSIC_UNDERGROUND
#define WaterMusic            MUSIC_WATER
#define GroundMusic           MUSIC_GROUND

#define TimeRunningOutMusic   MUSIC_TIME_RUNNING
#define EndOfLevelMusic      MUSIC_END_LEVEL
#define AltGameOverMusic      MUSIC_ALT_GAME_OVER
#define EndOfCastleMusic      MUSIC_END_CASTLE
#define VictoryMusic         MUSIC_VICTORY
#define GameOverMusic        MUSIC_GAME_OVER
#define DeathMusic           MUSIC_DEATH

#endif /* SMB_MUSIC_CONSTANTS_H */
