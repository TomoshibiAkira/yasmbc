/* Index-only software compositor shared by SDL and DOS hosts. */

#ifndef SMB_VIDEO_SOFT_H
#define SMB_VIDEO_SOFT_H

#include <stdint.h>

#define VIDEO_WIDTH  256
#define VIDEO_HEIGHT 240

int video_init(void);
void video_shutdown(void);
void video_render_begin(void);
void video_render_end(void);
const uint8_t *video_indices(void);
const uint32_t *video_dirty_mask(void);
uint32_t video_bg_serial(void);
uint16_t video_world_cam(void);

#endif
