#pragma once
#include <stddef.h>
#include <stdint.h>

int32_t file_open(const char *path, uint64_t flags);
int64_t file_read(int32_t fd, void *buffer, uint64_t len);
int32_t file_close(int32_t fd);
void* kmalloc(uint32_t size);
void kfree(void* ptr);
void* memcpy(void* dst, const void* src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
static void serial_write_string(const char *str);

static void draw_fill_rect(uint32_t x,
                           uint32_t y,
                           uint32_t w,
                           uint32_t h,
                           uint32_t color);

static void draw_present(void);
static void process_yield(void);