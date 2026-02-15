#include "Syscall_File.h"

#include "../Drivers/FileSystem/FAT32/FAT32_Main.h"
#include "../Memory/Memory_Main.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define FILE_MAX_FD 16

typedef struct {
    uint8_t used;
    uint8_t writable;
    FAT32_FILE file;
    uint32_t offset;
} kernel_file_t;

static kernel_file_t g_files[FILE_MAX_FD];

void syscall_file_init(void) {
    memset(g_files, 0, sizeof(g_files));
}

int32_t syscall_file_open(const char *path, uint64_t flags) {
    FAT32_FILE file;

    if (!path || path[0] == '\0') {
        return -1;
    }

    if (!fat32_find_file(path, &file)) {
        return -1;
    }

    for (int32_t fd = 0; fd < FILE_MAX_FD; ++fd) {
        if (!g_files[fd].used) {
            g_files[fd].used = 1;
            g_files[fd].writable = (flags & 1u) ? 1u : 0u;
            g_files[fd].file = file;
            g_files[fd].offset = 0;
            return fd;
        }
    }

    return -1;
}

int64_t syscall_file_read(int32_t fd, uint8_t *buffer, uint64_t len) {
    if (fd < 0 || fd >= FILE_MAX_FD || !buffer || !g_files[fd].used) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    kernel_file_t *f = &g_files[fd];
    uint32_t file_size = f->file.size;

    if (f->offset >= file_size) {
        return 0;
    }

    uint64_t remaining = (uint64_t)file_size - (uint64_t)f->offset;
    uint64_t to_read = len < remaining ? len : remaining;

    uint8_t *all_data = (uint8_t *)kmalloc(file_size);
    if (!all_data) {
        return -1;
    }

    if (!fat32_read_file(&f->file, all_data)) {
        kfree(all_data);
        return -1;
    }

    memcpy(buffer, all_data + f->offset, (size_t)to_read);
    f->offset += (uint32_t)to_read;
    kfree(all_data);

    return (int64_t)to_read;
}

int64_t syscall_file_write(int32_t fd, const uint8_t *buffer, uint64_t len) {
    if (fd < 0 || fd >= FILE_MAX_FD || !buffer || !g_files[fd].used) {
        return -1;
    }
    if (!g_files[fd].writable) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    kernel_file_t *f = &g_files[fd];
    uint32_t file_size = f->file.size;

    if (f->offset >= file_size) {
        return 0;
    }

    uint64_t remaining = (uint64_t)file_size - (uint64_t)f->offset;
    uint64_t to_write = len < remaining ? len : remaining;

    uint8_t *all_data = (uint8_t *)kmalloc(file_size);
    if (!all_data) {
        return -1;
    }

    if (!fat32_read_file(&f->file, all_data)) {
        kfree(all_data);
        return -1;
    }

    memcpy(all_data + f->offset, buffer, (size_t)to_write);
    if (!fat32_write_file(&f->file, all_data)) {
        kfree(all_data);
        return -1;
    }

    f->offset += (uint32_t)to_write;
    kfree(all_data);
    return (int64_t)to_write;
}

int32_t syscall_file_close(int32_t fd) {
    if (fd < 0 || fd >= FILE_MAX_FD || !g_files[fd].used) {
        return -1;
    }

    memset(&g_files[fd], 0, sizeof(g_files[fd]));
    return 0;
}
