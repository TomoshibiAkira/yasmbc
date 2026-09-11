/* engine/assets.h - Load extracted ROM payloads from assets/ */

#ifndef SMB_ENGINE_ASSETS_H
#define SMB_ENGINE_ASSETS_H

#include <stddef.h>
#include <stdint.h>

int Assets_Init(void);
void Assets_SetArgv0(const char *argv0);
const uint8_t *Assets_Load(const char *relative_path, size_t *length);
int Assets_Copy(const char *relative_path, void *dst, size_t expected_len);

#endif
