/* EnemyDataAddrLow/High payloads loaded from extracted assets/enemies. */
#include "enemy/enemy-data.h"
#include "assets.h"

#include <stdio.h>

static const char *const s_EnemyAssetNames[ENEMY_DATA_STREAM_COUNT] = {
    "castle_1", "castle_2", "castle_3", "castle_4", "castle_5", "castle_6",
    "ground_1", "ground_2", "ground_3", "ground_4", "ground_5", "ground_6",
    "ground_7", "ground_8", "ground_9", "ground_10", "ground_11", "ground_12",
    "ground_13", "ground_14", "ground_15", "ground_16", "ground_17", "ground_18",
    "ground_19", "ground_20", "ground_21", "ground_22",
    "underground_1", "underground_2", "underground_3",
    "water_1", "water_2", "water_3"
};

static const uint8_t *s_EnemyDataStreams[ENEMY_DATA_STREAM_COUNT];
static uint16_t s_EnemyDataLengths[ENEMY_DATA_STREAM_COUNT];

int EnemyData_Load(void) {
    uint8_t i;
    for (i = 0; i < ENEMY_DATA_STREAM_COUNT; i++) {
        char path[64];
        size_t n = 0;
        snprintf(path, sizeof(path), "enemies/%s.bin", s_EnemyAssetNames[i]);
        s_EnemyDataStreams[i] = Assets_Load(path, &n);
        s_EnemyDataLengths[i] = (uint16_t)n;
        if (!s_EnemyDataStreams[i] || n == 0) {
            fprintf(stderr, "EnemyData_Load: failed %s\n", path);
            return -1;
        }
    }
    return 0;
}

const uint8_t *EnemyData_GetStream(uint8_t index, uint16_t *length) {
    if (length) *length = 0;
    if (index >= ENEMY_DATA_STREAM_COUNT) return 0;
    if (length) *length = s_EnemyDataLengths[index];
    return s_EnemyDataStreams[index];
}
