#include "FAT32_Main.h"
#include "../../../Serial.h"
#include <string.h>

#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_LFN       0x0F
#define FAT32_DIR_ENTRY_SIZE 32

#define FAT32_MAX_NAME_LEN 260
#define FAT32_LFN_CHARS_PER_ENTRY 13
#define FAT32_MAX_LFN_ORDER ((FAT32_MAX_NAME_LEN + FAT32_LFN_CHARS_PER_ENTRY - 1) / FAT32_LFN_CHARS_PER_ENTRY)

static FAT32_BPB bpb;

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static bool string_case_equal(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        if (to_upper_ascii(*a) != to_upper_ascii(*b)) {
            return false;
        }
        ++a;
        ++b;
    }

    return (*a == '\0' && *b == '\0');
}

static void string_copy_limit(char *dst, uint32_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }

    uint32_t i = 0;
    if (src) {
        while (src[i] != '\0' && i + 1 < dst_size) {
            dst[i] = src[i];
            ++i;
        }
    }

    dst[i] = '\0';
}

static const char *skip_path_separators(const char *path) {
    if (!path) {
        return NULL;
    }

    while (*path == '/' || *path == '\\') {
        ++path;
    }

    return path;
}

static const char *extract_path_component(const char *path,
                                          char *component,
                                          uint32_t component_size,
                                          bool *has_more) {
    if (!path || !component || component_size == 0 || !has_more) {
        return NULL;
    }

    const char *p = skip_path_separators(path);
    uint32_t len = 0;

    while (*p != '\0' && *p != '/' && *p != '\\') {
        if (len + 1 >= component_size) {
            return NULL;
        }
        component[len++] = *p++;
    }

    component[len] = '\0';
    p = skip_path_separators(p);
    *has_more = (*p != '\0');
    return p;
}

static void fat32_short_name_to_string(const uint8_t *entry, char out_name[13]) {
    uint32_t n = 0;

    for (int i = 0; i < 8; ++i) {
        uint8_t ch = entry[i];
        if (ch == ' ') {
            break;
        }
        if (n + 1 < 13) {
            out_name[n++] = (char)ch;
        }
    }

    bool has_ext = false;
    for (int i = 8; i < 11; ++i) {
        if (entry[i] != ' ') {
            has_ext = true;
            break;
        }
    }

    if (has_ext && n + 1 < 13) {
        out_name[n++] = '.';
    }

    if (has_ext) {
        for (int i = 8; i < 11; ++i) {
            uint8_t ch = entry[i];
            if (ch == ' ') {
                break;
            }
            if (n + 1 < 13) {
                out_name[n++] = (char)ch;
            }
        }
    }

    out_name[n] = '\0';
}

static void fat32_lfn_store_char(char *lfn_name,
                                 uint32_t lfn_size,
                                 uint32_t index,
                                 uint16_t value) {
    if (!lfn_name || lfn_size == 0 || index >= lfn_size - 1) {
        return;
    }

    if (value == 0x0000 || value == 0xFFFF) {
        if (lfn_name[index] == '\0') {
            return;
        }
        lfn_name[index] = '\0';
        return;
    }

    if (value < 0x80) {
        lfn_name[index] = (char)value;
    } else {
        lfn_name[index] = '?';
    }
}

static bool fat32_decode_lfn_entry(const uint8_t *entry,
                                   char *lfn_name,
                                   uint32_t lfn_size) {
    uint8_t order = entry[0] & 0x1F;
    if (order == 0 || order > FAT32_MAX_LFN_ORDER) {
        return false;
    }

    uint32_t base = (uint32_t)(order - 1u) * FAT32_LFN_CHARS_PER_ENTRY;
    static const uint8_t offsets[FAT32_LFN_CHARS_PER_ENTRY] = {
        1, 3, 5, 7, 9,
        14, 16, 18, 20, 22, 24,
        28, 30
    };

    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; ++i) {
        fat32_lfn_store_char(lfn_name,
                             lfn_size,
                             base + i,
                             read_u16(&entry[offsets[i]]));
    }

    return true;
}

static void fat32_fill_file_from_entry(const uint8_t *entry,
                                       const char *resolved_name,
                                       FAT32_FILE *file) {
    uint16_t high = read_u16(&entry[20]);
    uint16_t low  = read_u16(&entry[26]);
    file->first_cluster = ((uint32_t)high << 16) | low;
    file->size = read_u32(&entry[28]);
    file->attributes = entry[11];
    string_copy_limit(file->name, sizeof(file->name), resolved_name);
}

static uint32_t fat_start_lba(void) {
    return bpb.reserved_sectors;
}

static uint32_t data_start_lba(void) {
    return bpb.reserved_sectors + bpb.num_fats * bpb.fat_size_sectors;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return data_start_lba() + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t fat_get_next_cluster(uint32_t cluster) {
    if (cluster < 2 || cluster >= 0x0FFFFFF8) return 0x0FFFFFFF;

    uint32_t fat_offset = cluster * 4;
    uint32_t sector = fat_start_lba() + (fat_offset / bpb.bytes_per_sector);
    uint32_t offset = fat_offset % bpb.bytes_per_sector;

    if (offset + 4 > bpb.bytes_per_sector) return 0x0FFFFFFF;

    uint8_t buf[bpb.bytes_per_sector];
    if (!disk_read(sector, buf, 1)) return 0x0FFFFFFF;

    uint32_t val =
        buf[offset] |
        (buf[offset + 1] << 8) |
        (buf[offset + 2] << 16) |
        (buf[offset + 3] << 24);

    return val & 0x0FFFFFFF;
}

static bool __attribute__((unused))
fat_set_next_cluster(uint32_t cluster, uint32_t next) {
    if (cluster < 2 || cluster >= 0x0FFFFFF8) return false;

    uint32_t fat_offset = cluster * 4;
    uint32_t offset = fat_offset % bpb.bytes_per_sector;
    if (offset + 4 > bpb.bytes_per_sector) return false;

    for (uint32_t fat = 0; fat < bpb.num_fats; fat++) {
        uint32_t sector = fat_start_lba() +
                          fat * bpb.fat_size_sectors +
                          (fat_offset / bpb.bytes_per_sector);

        uint8_t buf[bpb.bytes_per_sector];
        if (!disk_read(sector, buf, 1)) return false;

        buf[offset]     = next & 0xFF;
        buf[offset + 1] = (next >> 8) & 0xFF;
        buf[offset + 2] = (next >> 16) & 0xFF;
        buf[offset + 3] = (buf[offset + 3] & 0xF0) | ((next >> 24) & 0x0F);

        if (!disk_write(sector, buf, 1)) return false;
    }

    return true;
}

static bool fat32_lookup_entry_in_directory(uint32_t dir_cluster,
                                            const char *target_name,
                                            FAT32_FILE *out_file) {
    if (dir_cluster < 2 || !target_name || !out_file || target_name[0] == '\0') {
        return false;
    }

    uint8_t buf[bpb.bytes_per_sector];
    char lfn_name[FAT32_MAX_NAME_LEN];
    int lfn_valid = 0;

    memset(lfn_name, 0, sizeof(lfn_name));

    uint32_t cluster = dir_cluster;
    uint32_t max_clusters = bpb.fat_size_sectors * bpb.bytes_per_sector / 4;

    for (uint32_t c = 0; c < max_clusters; ++c) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!lba) {
            return false;
        }

        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; ++sec) {
            if (!disk_read(lba + sec, buf, 1)) {
                return false;
            }

            for (uint32_t i = 0; i < bpb.bytes_per_sector; i += FAT32_DIR_ENTRY_SIZE) {
                const uint8_t *entry = &buf[i];
                uint8_t first = entry[0];

                if (first == 0x00) {
                    return false;
                }
                if (first == 0xE5) {
                    lfn_valid = 0;
                    memset(lfn_name, 0, sizeof(lfn_name));
                    continue;
                }

                uint8_t attr = entry[11];

                if (attr == FAT32_ATTR_LFN) {
                    uint8_t seq = entry[0];
                    uint8_t order = seq & 0x1F;

                    if (order == 0 || order > FAT32_MAX_LFN_ORDER) {
                        lfn_valid = 0;
                        memset(lfn_name, 0, sizeof(lfn_name));
                        continue;
                    }

                    if (seq & 0x40) {
                        memset(lfn_name, 0, sizeof(lfn_name));
                        lfn_valid = 1;
                    }

                    if (!lfn_valid ||
                        !fat32_decode_lfn_entry(entry, lfn_name, sizeof(lfn_name))) {
                        lfn_valid = 0;
                        memset(lfn_name, 0, sizeof(lfn_name));
                    }
                    continue;
                }

                if (attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_valid = 0;
                    memset(lfn_name, 0, sizeof(lfn_name));
                    continue;
                }

                char short_name[13];
                fat32_short_name_to_string(entry, short_name);

                const char *candidate =
                    (lfn_valid && lfn_name[0] != '\0') ? lfn_name : short_name;
                    
                if (string_case_equal(candidate, target_name)) {
                    fat32_fill_file_from_entry(entry, candidate, out_file);
                    return true;
                }

                lfn_valid = 0;
                memset(lfn_name, 0, sizeof(lfn_name));
            }
        }

        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2 || next >= 0x0FFFFFF8) {
            break;
        }
        cluster = next;
    }

    return false;
}

static bool fat32_lookup_path(const char *path, FAT32_FILE *out_entry) {
    if (!path || !out_entry) {
        return false;
    }

    const char *cursor = skip_path_separators(path);
    if (!cursor || *cursor == '\0') {
        return false;
    }

    uint32_t current_dir_cluster = bpb.root_cluster;
    FAT32_FILE current_entry;
    char component[FAT32_MAX_NAME_LEN];
    bool has_more = false;

    while (1) {
        const char *next = extract_path_component(cursor,
                                                  component,
                                                  sizeof(component),
                                                  &has_more);
        if (!next || component[0] == '\0') {
            return false;
        }

        if (!fat32_lookup_entry_in_directory(current_dir_cluster,
                                             component,
                                             &current_entry)) {
            return false;
        }

        if (!has_more) {
            *out_entry = current_entry;
            return true;
        }

        if ((current_entry.attributes & FAT32_ATTR_DIRECTORY) == 0) {
            return false;
        }

        if (current_entry.first_cluster < 2) {
            return false;
        }

        current_dir_cluster = current_entry.first_cluster;
        cursor = next;
    }
}

uint32_t fat32_get_file_size(FAT32_FILE *file) {
    if (!file) return 0;
    return file->size;
}

bool fat32_init(void) {
    uint8_t sector[512];
    if (!disk_read(0, sector, 1)) return false;

    bpb.bytes_per_sector    = read_u16(&sector[11]);
    bpb.sectors_per_cluster = sector[13];
    bpb.reserved_sectors    = read_u16(&sector[14]);
    bpb.num_fats            = sector[16];
    bpb.fat_size_sectors    = read_u32(&sector[36]);
    bpb.root_cluster        = read_u32(&sector[44]);

    if (bpb.bytes_per_sector == 0 || bpb.bytes_per_sector > 4096) return false;
    if (bpb.sectors_per_cluster == 0 || bpb.sectors_per_cluster > 128) return false;
    if (bpb.num_fats == 0 || bpb.num_fats > 2) return false;
    if (bpb.root_cluster < 2) return false;
    
    serial_write_string("[OS] [FAT32] Init Success\n");
    return true;
}

bool fat32_find_file(const char *filename, FAT32_FILE *file) {
    if (!filename || !file) {
        return false;
    }

    if (!fat32_lookup_path(filename, file)) {
        return false;
    }

    if (file->attributes & FAT32_ATTR_DIRECTORY) {
        return false;
    }

    return true;
}

void fat32_list_root_files(void) {
    uint32_t cluster = bpb.root_cluster;
    uint8_t buf[bpb.bytes_per_sector];
    char lfn_name[FAT32_MAX_NAME_LEN];
    int lfn_valid = 0;

    memset(lfn_name, 0, sizeof(lfn_name));

    uint32_t max_clusters = bpb.fat_size_sectors * bpb.bytes_per_sector / 4;

    for (uint32_t c = 0; c < max_clusters; c++) {
        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            uint32_t lba = cluster_to_lba(cluster);
            if (!lba) return;
            if (!disk_read(lba + sec, buf, 1)) return;

            for (uint32_t i = 0; i < bpb.bytes_per_sector; i += 32) {
                if (buf[i] == 0x00) return;
                if (buf[i] == 0xE5) {
                    lfn_valid = 0;
                    memset(lfn_name, 0, sizeof(lfn_name));
                    continue;
                }

                uint8_t attr = buf[i + 11];

                if (attr == FAT32_ATTR_LFN) {
                    uint8_t seq = buf[i];
                    uint8_t order = seq & 0x1F;

                    if (order == 0 || order > FAT32_MAX_LFN_ORDER) {
                        lfn_valid = 0;
                        memset(lfn_name, 0, sizeof(lfn_name));
                        continue;
                    }

                    if (seq & 0x40) {
                        memset(lfn_name, 0, sizeof(lfn_name));
                        lfn_valid = 1;
                    }

                    if (!lfn_valid ||
                        !fat32_decode_lfn_entry(&buf[i], lfn_name, sizeof(lfn_name))) {
                        lfn_valid = 0;
                        memset(lfn_name, 0, sizeof(lfn_name));
                    }
                    continue;
                }

                if (attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_valid = 0;
                    memset(lfn_name, 0, sizeof(lfn_name));
                    continue;
                }

                char short_name[13];
                fat32_short_name_to_string(&buf[i], short_name);

                const char *display_name =
                    (lfn_valid && lfn_name[0] != '\0') ? lfn_name : short_name;

                serial_write_string(display_name);
                if (attr & FAT32_ATTR_DIRECTORY) {
                    serial_write_string("/");
                }
                serial_write_string("\n");
                
                lfn_valid = 0;
                memset(lfn_name, 0, sizeof(lfn_name));
            }
        }

        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2 || next >= 0x0FFFFFF8) break;
        cluster = next;
    }
}

bool fat32_read_file(FAT32_FILE *file, uint8_t *buffer) {
    if (!file || !buffer) return false;

    uint32_t cluster = file->first_cluster;
    uint32_t bytes_left = file->size;
    uint32_t cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    uint8_t buf[cluster_size];

    while (bytes_left) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!lba) return false;
        
        uint32_t n_read = bytes_left > cluster_size ? cluster_size : bytes_left;
        
        if (!disk_read(lba, buf, bpb.sectors_per_cluster)) {
            serial_write_string("[OS] [FAT32] Disk read failed\n");
            return false;
        }
        
        memcpy(buffer, buf, n_read);
        buffer += n_read;
        bytes_left -= n_read;

        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2 || next >= 0x0FFFFFF8) break;
        cluster = next;
    }

    return bytes_left == 0;
}

bool fat32_write_file(FAT32_FILE *file, const uint8_t *buffer) {
    if (!file || !buffer) return false;

    uint32_t cluster = file->first_cluster;
    uint32_t bytes_left = file->size;
    uint8_t buf[bpb.bytes_per_sector];
    uint32_t max_clusters = bpb.fat_size_sectors * bpb.bytes_per_sector / 4;

    for (uint32_t c = 0; c < max_clusters && bytes_left; c++) {
        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster && bytes_left; sec++) {
            uint32_t lba = cluster_to_lba(cluster);
            if (!lba) return false;

            uint32_t n = bytes_left > bpb.bytes_per_sector ?
                         bpb.bytes_per_sector : bytes_left;

            if (n == bpb.bytes_per_sector) {
                if (!disk_write(lba + sec, buffer, 1)) return false;
            } else {
                if (!disk_read(lba + sec, buf, 1)) return false;
                memcpy(buf, buffer, n);
                if (!disk_write(lba + sec, buf, 1)) return false;
            }

            buffer += n;
            bytes_left -= n;
        }

        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2 || next >= 0x0FFFFFF8) break;
        cluster = next;
    }

    return bytes_left == 0;
}