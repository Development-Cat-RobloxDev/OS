#pragma once
#include <stddef.h>
#include <stdint.h>
#include "MouseState.h"

int32_t file_open(const char *path, uint64_t flags);
int64_t file_read(int32_t fd, void *buffer, uint64_t len);
int64_t file_write(int32_t fd, const void *buffer, uint64_t len);
int32_t file_close(int32_t fd);
void* kmalloc(uint32_t size);
void  kfree(void *ptr);
void* memcpy(void *dst, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
void serial_write_string(const char *str);
int32_t wm_create_window(uint32_t width, uint32_t height);
void    draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void    draw_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void    draw_present(void);
void process_yield(void);
int  mouse_read(user_mouse_state_t *out);
void mouse_set_position(int32_t x, int32_t y);
void mouse_set_bounds(int32_t min_x, int32_t min_y,
                      int32_t max_x, int32_t max_y);