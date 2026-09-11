#ifndef SMB_ENEMY_DATA_H
#define SMB_ENEMY_DATA_H

#include <stdint.h>

#define ENEMY_DATA_STREAM_COUNT 34

int EnemyData_Load(void);
const uint8_t *EnemyData_GetStream(uint8_t index, uint16_t *length);

#endif /* SMB_ENEMY_DATA_H */
