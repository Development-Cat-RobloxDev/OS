#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FAT32_MAX_SECTOR_SIZE       4096u
#define FAT32_CLUSTER_BUFFER_SIZE   65536u
#define FAT32_PATH_MAX              512u
#define FAT32_NAME_MAX              260u

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t total_sectors;
} FAT32_BPB;

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    uint32_t dir_entry_sector;
    uint32_t dir_cluster;
    uint16_t dir_entry_offset;
    uint8_t  attributes;
    uint8_t  lfn_entry_count;
    char     name[FAT32_NAME_MAX];
} FAT32_FILE;

typedef struct {
    char     name[FAT32_NAME_MAX];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t  attributes;
} FAT32_DIRENT;

bool     fat32_init(void);

bool     fat32_find_file(const char *path, FAT32_FILE *out);
uint32_t fat32_get_file_size(FAT32_FILE *file);

bool     fat32_read_file(FAT32_FILE *file, uint8_t *buf);
bool     fat32_write_file(FAT32_FILE *file, const uint8_t *buf);
bool     fat32_read_at(FAT32_FILE *file, uint32_t offset, uint8_t *buf, uint32_t size);
bool     fat32_write_at(FAT32_FILE *file, uint32_t offset, const uint8_t *buf, uint32_t size);
bool     fat32_truncate(FAT32_FILE *file, uint32_t new_size);

bool     fat32_creat(const char *path);
bool     fat32_mkdir(const char *path);
bool     fat32_unlink(const char *path);

int32_t  fat32_opendir(const char *path);
int32_t  fat32_readdir(int32_t handle, FAT32_DIRENT *out);
int32_t  fat32_closedir(int32_t handle);

void     fat32_list_root_files(void);

void     fat32_set_case_sensitive_lookup(bool enabled);
bool     fat32_get_case_sensitive_lookup(void);

typedef struct {
    bool     (*init)(void);
    bool     (*find_file)(const char *, FAT32_FILE *);
    bool     (*read_file)(FAT32_FILE *, uint8_t *);
    bool     (*write_file)(FAT32_FILE *, const uint8_t *);
    bool     (*read_at)(FAT32_FILE *, uint32_t, uint8_t *, uint32_t);
    bool     (*write_at)(FAT32_FILE *, uint32_t, const uint8_t *, uint32_t);
    uint32_t (*get_file_size)(FAT32_FILE *);
    void     (*list_root_files)(void);
    bool     (*creat)(const char *);
    bool     (*mkdir)(const char *);
    int32_t  (*opendir)(const char *);
    int32_t  (*readdir)(int32_t, FAT32_DIRENT *);
    int32_t  (*closedir)(int32_t);
    bool     (*unlink)(const char *);
    bool     (*truncate)(FAT32_FILE *, uint32_t);
    void     (*set_case_sensitive_lookup)(bool);
    bool     (*get_case_sensitive_lookup)(void);
} fat32_driver_t;