/* DOS Mode X hooks used by the shared compositor. SDL does not compile this. */

#ifndef SMB_DOS_VGA_H
#define SMB_DOS_VGA_H

#include <stdint.h>

int dos_vga_on_camera(uint16_t cam, uint32_t serial, int delta, int reset);
void dos_vga_update_palette(uint8_t dirty_mask);
void dos_vga_fill_backdrop(void);
int dos_vga_blit_tile(int screen_x, int screen_y, uint8_t tile, uint8_t pal,
                      const uint8_t *texels, const uint8_t *lut);
void dos_vga_flush_tiles(void);
void dos_vga_present_hud(void);
void dos_vga_sprite_begin(int slot, int screen_x, int screen_y);
void dos_vga_sprite_pixel(int screen_x, int screen_y, uint8_t color);

#endif
