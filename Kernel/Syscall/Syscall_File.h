#pragma once

#include <stdint.h>

void syscall_file_init(void);
int32_t syscall_file_open(const char *path, uint64_t flags);
int64_t syscall_file_read(int32_t fd, uint8_t *buffer, uint64_t len);
int64_t syscall_file_write(int32_t fd, const uint8_t *buffer, uint64_t len);
int32_t syscall_file_close(int32_t fd);
