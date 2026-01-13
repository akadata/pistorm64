// SPDX-License-Identifier: MIT
// Minimal RTG stub to allow builds without raylib/SDL backends.

#include <stdint.h>
#include "rtg.h"

uint8_t busy = 0;
uint8_t rtg_on = 0;
uint8_t rtg_initialized = 0;
uint8_t emulator_exiting = 0;
uint8_t rtg_output_in_vblank = 0;
uint8_t rtg_dpms = 0;
uint8_t shutdown = 0;
uint32_t cur_rtg_frame = 0;

void rtg_init_display(void) {
  rtg_initialized = 1;
  rtg_on = 0;
}

void rtg_shutdown_display(void) {
  rtg_on = 0;
  rtg_initialized = 0;
}

void rtg_enable_mouse_cursor(uint8_t enable) {
  (void)enable;
}
void rtg_set_clut_entry(uint8_t index, uint32_t xrgb) {
  (void)index;
  (void)xrgb;
}
void rtg_set_mouse_cursor_pos(int16_t x, int16_t y) {
  (void)x;
  (void)y;
}
void rtg_set_cursor_clut_entry(uint8_t r, uint8_t g, uint8_t b, uint8_t idx) {
  (void)r;
  (void)g;
  (void)b;
  (void)idx;
}
void rtg_set_mouse_cursor_image(uint8_t* src, uint8_t w, uint8_t h) {
  (void)src;
  (void)w;
  (void)h;
}
void rtg_show_fps(uint8_t enable) {
  (void)enable;
}
void rtg_set_scale_mode(uint16_t scale_mode) {
  (void)scale_mode;
}
uint16_t rtg_get_scale_mode(void) {
  return 0;
}
void rtg_set_scale_rect(uint16_t scale_mode, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  (void)scale_mode;
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
}
void rtg_set_scale_filter(uint16_t _filter_mode) {
  (void)_filter_mode;
}
void rtg_set_screen_width(uint32_t width) {
  (void)width;
}
void rtg_set_screen_height(uint32_t height) {
  (void)height;
}
void rtg_show_clut_cursor(uint8_t show) {
  (void)show;
}
void rtg_set_clut_cursor(uint8_t* bmp, uint32_t* pal, int16_t offs_x, int16_t offs_y, uint16_t w,
                         uint16_t h, uint8_t mask_color) {
  (void)bmp;
  (void)pal;
  (void)offs_x;
  (void)offs_y;
  (void)w;
  (void)h;
  (void)mask_color;
}
uint16_t rtg_get_scale_filter(void) {
  return 0;
}
void rtg_palette_debug(uint8_t enable) {
  (void)enable;
}
