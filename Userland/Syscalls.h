#pragma once
#include <stdint.h>

int32_t file_open(const char *path, uint64_t flags);
int64_t file_read(int32_t fd, void *buffer, uint64_t len);
int32_t file_close(int32_t fd);
void* kmalloc(uint32_t size);
void kfree(void* ptr);