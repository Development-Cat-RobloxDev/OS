#include "FAT32_Main.h"
#include "../../../Serial.h"
#ifdef IMPLUS_DRIVER_MODULE
#include "../../DriverBinary.h"
#endif
#include <string.h>

#ifdef IMPLUS_DRIVER_MODULE
static const driver_kernel_api_t *g_driver_api = NULL;

#define disk_read              g_driver_api->disk_read
#define disk_write             g_driver_api->disk_write
#define serial_write_string    g_driver_api->serial_write_string
#define memset                 g_driver_api->memset
#define memcpy                 g_driver_api->memcpy
#endif

#define FAT32_ATTR_VOLUME_ID        0x08u
#define FAT32_ATTR_DIRECTORY        0x10u
#define FAT32_ATTR_ARCHIVE          0x20u
#define FAT32_ATTR_LFN              0x0Fu
#define FAT32_DIR_ENTRY_SIZE        32u

#define FAT32_MAX_NAME_LEN          260u
#define FAT32_LFN_CHARS_PER_ENTRY   13u
#define FAT32_MAX_LFN_ORDER \
    ((FAT32_MAX_NAME_LEN + FAT32_LFN_CHARS_PER_ENTRY - 1u) / FAT32_LFN_CHARS_PER_ENTRY)

#define FAT32_MIN_SECTOR_SIZE           512u
#define FAT32_MAX_SECTORS_PER_CLUSTER   128u
#define FAT32_MAX_RESERVED_SECTORS      65535u
#define FAT32_MAX_NUM_FATS              2u
#define FAT32_MAX_FAT_SIZE_SECTORS      0x10000000UL
#define FAT32_EOC_MARKER                0x0FFFFFFFu
#define FAT32_DIR_HANDLE_MAX            16

static FAT32_BPB bpb;

static uint8_t g_sector_buffer[FAT32_MAX_SECTOR_SIZE];
static uint8_t g_read_buffer[FAT32_CLUSTER_BUFFER_SIZE];
static bool    g_case_sensitive_lookup = false;

typedef struct {
    uint8_t  used;
    uint32_t directory_cluster;
    uint32_t current_cluster;
    uint32_t entry_offset;
} fat32_dir_handle_t;

static fat32_dir_handle_t g_dir_handles[FAT32_DIR_HANDLE_MAX];

static uint16_t read_u16(const uint8_t *p) {
    if (!p) return 0;
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    if (!p) return 0;
    return (uint32_t)p[0]        |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16)|
           ((uint32_t)p[3] << 24);
}

static uint32_t fat32_strlen(const char *s)
{
    if (!s) return 0;
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static bool string_case_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (to_upper_ascii(*a) != to_upper_ascii(*b)) return false;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static bool string_exact_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static bool fat32_name_equal(const char *a, const char *b) {
    return g_case_sensitive_lookup ? string_exact_equal(a, b)
                                   : string_case_equal(a, b);
}

static void string_copy_limit(char *dst, uint32_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    uint32_t i = 0;
    if (src) {
        while (src[i] != '\0' && i + 1 < dst_size) {
            dst[i] = src[i]; ++i;
        }
    }
    dst[i] = '\0';
}

static const char *skip_path_separators(const char *path) {
    if (!path) return NULL;
    while (*path == '/' || *path == '\\') ++path;
    return path;
}

static const char *extract_path_component(const char *path,
                                           char *component,
                                           uint32_t component_size,
                                           bool *has_more) {
    if (!path || !component || component_size == 0 || !has_more) return NULL;
    const char *p = skip_path_separators(path);
    uint32_t len = 0;
    while (*p != '\0' && *p != '/' && *p != '\\') {
        if (len + 1 >= component_size) return NULL;
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
        if (ch == ' ') break;
        if (n + 1 < 13) out_name[n++] = (char)ch;
    }
    bool has_ext = false;
    for (int i = 8; i < 11; ++i) { if (entry[i] != ' ') { has_ext = true; break; } }
    if (has_ext && n + 1 < 13) out_name[n++] = '.';
    if (has_ext) {
        for (int i = 8; i < 11; ++i) {
            uint8_t ch = entry[i];
            if (ch == ' ') break;
            if (n + 1 < 13) out_name[n++] = (char)ch;
        }
    }
    out_name[n] = '\0';
}

static bool fat32_name_is_simple_83(const char *name) {
    if (!name || name[0] == '\0') return false;
    uint32_t base_len = 0, ext_len = 0;
    int seen_dot = 0;
    for (const char *p = name; *p != '\0'; ++p) {
        char ch = *p;
        if (ch == '.' && !seen_dot) { seen_dot = 1; continue; }
        if (ch == '.' || ch == '/' || ch == '\\' || ch == ' ') return false;
        if (seen_dot) ++ext_len; else ++base_len;
    }
    return (base_len > 0 && base_len <= 8 && ext_len <= 3);
}

static bool fat32_build_short_name_83(const char *name, uint8_t out_name[11]) {
    if (!fat32_name_is_simple_83(name) || !out_name) return false;
    for (uint32_t i = 0; i < 11; ++i) out_name[i] = ' ';
    uint32_t base_idx = 0, ext_idx = 0;
    int seen_dot = 0;
    for (const char *p = name; *p != '\0'; ++p) {
        char ch = *p;
        if (ch == '.') { seen_dot = 1; continue; }
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - ('a' - 'A'));
        if (!seen_dot) out_name[base_idx++] = (uint8_t)ch;
        else           out_name[8u + ext_idx++] = (uint8_t)ch;
    }
    return true;
}

static void fat32_lfn_store_char(char *lfn_name, uint32_t lfn_size,
                                  uint32_t index, uint16_t value) {
    if (!lfn_name || lfn_size == 0 || index >= lfn_size - 1u) return;
    if (value == 0x0000u || value == 0xFFFFu) {
        if (lfn_name[index] == '\0') return;
        lfn_name[index] = '\0';
        return;
    }
    lfn_name[index] = (value < 0x80u) ? (char)value : '?';
}

static bool fat32_decode_lfn_entry(const uint8_t *entry,
                                    char *lfn_name, uint32_t lfn_size) {
    uint8_t order = entry[0] & 0x1Fu;
    if (order == 0 || order > FAT32_MAX_LFN_ORDER) return false;

    uint32_t base = (uint32_t)(order - 1u) * FAT32_LFN_CHARS_PER_ENTRY;
    static const uint8_t offsets[FAT32_LFN_CHARS_PER_ENTRY] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; ++i) {
        if (base + i >= lfn_size - 1u) break;
        fat32_lfn_store_char(lfn_name, lfn_size,
                             base + i, read_u16(&entry[offsets[i]]));
    }
    return true;
}

static uint8_t fat32_lfn_checksum(const uint8_t name11[11])
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; ++i)
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + name11[i]);
    return sum;
}

static uint8_t fat32_lfn_count_needed(const char *name)
{
    uint32_t len = fat32_strlen(name);
    if (len == 0) return 0;
    return (uint8_t)((len + FAT32_LFN_CHARS_PER_ENTRY - 1u) / FAT32_LFN_CHARS_PER_ENTRY);
}

static void fat32_build_lfn_entry(uint8_t *buf,
                                   uint8_t  order,
                                   bool     is_last,
                                   uint8_t  checksum,
                                   const char *name,
                                   uint32_t    name_len)
{
    static const uint8_t char_offsets[FAT32_LFN_CHARS_PER_ENTRY] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };

    memset(buf, 0xFF, FAT32_DIR_ENTRY_SIZE);
    buf[0]  = order | (is_last ? 0x40u : 0u);
    buf[11] = FAT32_ATTR_LFN;
    buf[12] = 0x00u;
    buf[13] = checksum;
    buf[26] = 0x00u;
    buf[27] = 0x00u;

    uint32_t base = (uint32_t)(order - 1u) * FAT32_LFN_CHARS_PER_ENTRY;
    bool end_reached = false;

    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; ++i) {
        uint32_t pos = base + i;
        uint16_t val;
        if (end_reached) {
            val = 0xFFFFu;
        } else if (pos < name_len) {
            val = (uint8_t)name[pos];
        } else if (pos == name_len) {
            val = 0x0000u;
            end_reached = true;
        } else {
            val = 0xFFFFu;
            end_reached = true;
        }
        buf[char_offsets[i]]     = (uint8_t)(val & 0xFFu);
        buf[char_offsets[i] + 1] = (uint8_t)(val >> 8);
    }
}

static bool fat32_is_valid_83_char(char c)
{
    if ((unsigned char)c < 0x21u) return false;
    switch (c) {
        case '"': case '*': case '+': case ',': case '.':
        case '/': case ':': case ';': case '<': case '=':
        case '>': case '?': case '[': case '\\': case ']':
        case '|':
            return false;
    }
    return true;
}

static uint8_t fat32_sanitize_83_char(char c)
{
    c = to_upper_ascii(c);
    return fat32_is_valid_83_char(c) ? (uint8_t)c : (uint8_t)'_';
}

static uint32_t fat32_u32_to_str(uint32_t n, char buf[10])
{
    if (n == 0) { buf[0] = '0'; return 1u; }
    uint32_t len = 0;
    char tmp[10];
    while (n > 0) { tmp[len++] = (char)('0' + (n % 10u)); n /= 10u; }
    for (uint32_t i = 0; i < len; ++i) buf[i] = tmp[len - 1u - i];
    return len;
}

static uint32_t fat_start_lba(void) { return bpb.reserved_sectors; }

static uint32_t data_start_lba(void) {
    uint32_t fat_area = bpb.num_fats * bpb.fat_size_sectors;
    if (fat_area > 0xFFFFFFFFUL - bpb.reserved_sectors) return 0;
    return bpb.reserved_sectors + fat_area;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2u) return 0u;
    uint32_t data_lba = data_start_lba();
    if (data_lba == 0u) return 0u;
    uint32_t offset;
    if (__builtin_mul_overflow(cluster - 2u, bpb.sectors_per_cluster, &offset)) return 0u;
    if (offset > 0xFFFFFFFFUL - data_lba) return 0u;
    return data_lba + offset;
}


static uint32_t fat_get_next_cluster(uint32_t cluster) {
    if (cluster < 2u || cluster >= 0x0FFFFFF8u) return 0x0FFFFFFFu;

    uint32_t fat_offset   = cluster * 4u;
    uint32_t sector_offset = fat_offset / bpb.bytes_per_sector;
    if (sector_offset > 0xFFFFFFFFUL - fat_start_lba()) return 0x0FFFFFFFu;

    uint32_t sector = fat_start_lba() + sector_offset;
    uint32_t offset = fat_offset % bpb.bytes_per_sector;
    if (offset + 4u > bpb.bytes_per_sector) return 0x0FFFFFFFu;

    if (!disk_read(sector, g_sector_buffer, 1)) return 0x0FFFFFFFu;

    uint32_t val = (uint32_t)g_sector_buffer[offset]       |
                  ((uint32_t)g_sector_buffer[offset + 1u] << 8)  |
                  ((uint32_t)g_sector_buffer[offset + 2u] << 16) |
                  ((uint32_t)g_sector_buffer[offset + 3u] << 24);
    return val & 0x0FFFFFFFu;
}

static bool __attribute__((unused))
fat_set_next_cluster(uint32_t cluster, uint32_t next) {
    if (cluster < 2u || cluster >= 0x0FFFFFF8u) return false;

    uint32_t fat_offset = cluster * 4u;
    uint32_t offset     = fat_offset % bpb.bytes_per_sector;
    if (offset + 4u > bpb.bytes_per_sector) return false;

    for (uint32_t fat = 0; fat < bpb.num_fats; ++fat) {
        uint32_t sector_offset = fat_offset / bpb.bytes_per_sector;
        uint32_t fat_lba = fat * bpb.fat_size_sectors;
        if (fat_lba > 0xFFFFFFFFUL - fat_start_lba() ||
            sector_offset > 0xFFFFFFFFUL - (fat_start_lba() + fat_lba)) return false;

        uint32_t sector = fat_start_lba() + fat_lba + sector_offset;
        if (!disk_read(sector, g_sector_buffer, 1)) return false;

        g_sector_buffer[offset]     = (uint8_t)(next & 0xFFu);
        g_sector_buffer[offset + 1] = (uint8_t)((next >> 8)  & 0xFFu);
        g_sector_buffer[offset + 2] = (uint8_t)((next >> 16) & 0xFFu);
        g_sector_buffer[offset + 3] = (uint8_t)((g_sector_buffer[offset + 3] & 0xF0u)
                                                 | ((next >> 24) & 0x0Fu));
        if (!disk_write(sector, g_sector_buffer, 1)) return false;
    }
    return true;
}

static uint32_t fat32_cluster_size_bytes(void) {
    return (uint32_t)bpb.sectors_per_cluster * bpb.bytes_per_sector;
}

static uint32_t fat32_total_clusters(void) {
    return (bpb.fat_size_sectors * bpb.bytes_per_sector) / 4u;
}

static bool fat32_zero_cluster(uint32_t cluster) {
    uint32_t lba = cluster_to_lba(cluster);
    if (lba == 0u) return false;
    uint32_t cluster_size = fat32_cluster_size_bytes();
    if (cluster_size == 0u || cluster_size > FAT32_CLUSTER_BUFFER_SIZE) return false;
    memset(g_read_buffer, 0, cluster_size);
    return disk_write(lba, g_read_buffer, bpb.sectors_per_cluster);
}

static uint32_t fat32_find_free_cluster(void) {
    uint32_t total = fat32_total_clusters();
    for (uint32_t c = 2u; c < total; ++c)
        if (fat_get_next_cluster(c) == 0u) return c;
    return 0u;
}

static uint32_t fat32_allocate_cluster_zeroed(void) {
    uint32_t cluster = fat32_find_free_cluster();
    if (cluster < 2u) return 0u;
    if (!fat_set_next_cluster(cluster, FAT32_EOC_MARKER)) return 0u;
    if (!fat32_zero_cluster(cluster)) {
        (void)fat_set_next_cluster(cluster, 0u);
        return 0u;
    }
    return cluster;
}

static bool fat32_free_cluster_chain(uint32_t first_cluster) {
    if (first_cluster < 2u) return true;
    uint32_t cluster = first_cluster;
    uint32_t guard   = fat32_total_clusters();
    for (uint32_t i = 0u; i < guard; ++i) {
        uint32_t next = fat_get_next_cluster(cluster);
        if (!fat_set_next_cluster(cluster, 0u)) return false;
        if (next < 2u || next >= FAT32_EOC_MARKER) return true;
        cluster = next;
    }
    return false;
}

static uint32_t fat32_clusters_for_size(uint32_t size, uint32_t cluster_size) {
    if (size == 0u || cluster_size == 0u) return 0u;
    return (size + cluster_size - 1u) / cluster_size;
}

static bool fat32_get_cluster_at_index(const FAT32_FILE *file,
                                        uint32_t index, uint32_t *cluster_out) {
    if (!file || !cluster_out || file->first_cluster < 2u) return false;
    uint32_t cluster = file->first_cluster;
    for (uint32_t i = 0u; i < index; ++i) {
        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= FAT32_EOC_MARKER) return false;
        cluster = next;
    }
    *cluster_out = cluster;
    return true;
}

static bool fat32_get_last_cluster(const FAT32_FILE *file,
                                    uint32_t *cluster_out, uint32_t *count_out) {
    if (!file || !cluster_out || !count_out || file->first_cluster < 2u) return false;
    uint32_t cluster = file->first_cluster, count = 1u;
    uint32_t guard = fat32_total_clusters();
    for (uint32_t i = 0u; i < guard; ++i) {
        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= FAT32_EOC_MARKER) {
            *cluster_out = cluster; *count_out = count; return true;
        }
        cluster = next; ++count;
    }
    return false;
}

static bool fat32_ensure_cluster_count(FAT32_FILE *file, uint32_t required_clusters) {
    if (!file) return false;
    if (required_clusters == 0u) return true;
    if (file->first_cluster < 2u) {
        uint32_t c = fat32_allocate_cluster_zeroed();
        if (c < 2u) return false;
        file->first_cluster = c;
    }
    uint32_t last = 0u, current = 0u;
    if (!fat32_get_last_cluster(file, &last, &current)) return false;
    while (current < required_clusters) {
        uint32_t nc = fat32_allocate_cluster_zeroed();
        if (nc < 2u) return false;
        if (!fat_set_next_cluster(last, nc)) { (void)fat_set_next_cluster(nc, 0u); return false; }
        last = nc; ++current;
    }
    return true;
}

static bool fat32_dir_pos_advance(uint32_t *sector, uint16_t *offset)
{
    if (!sector || !offset) return false;

    uint16_t next_off = (uint16_t)(*offset + FAT32_DIR_ENTRY_SIZE);
    if (next_off < (uint16_t)bpb.bytes_per_sector) {
        *offset = next_off;
        return true;
    }

    *offset = 0u;
    uint32_t data_lba      = data_start_lba();
    if (*sector < data_lba) return false;

    uint32_t rel           = *sector - data_lba;
    uint32_t sec_in_cluster = rel % bpb.sectors_per_cluster;

    if (sec_in_cluster + 1u < bpb.sectors_per_cluster) {
        (*sector)++;
        return true;
    }

    uint32_t cluster = rel / bpb.sectors_per_cluster + 2u;
    uint32_t next_c  = fat_get_next_cluster(cluster);
    if (next_c < 2u || next_c >= FAT32_EOC_MARKER) return false;
    *sector = cluster_to_lba(next_c);
    return true;
}

static bool fat32_dir_pos_prev(uint32_t dir_cluster,
                                uint32_t *sector, uint16_t *offset)
{
    if (!sector || !offset) return false;

    if (*offset >= FAT32_DIR_ENTRY_SIZE) {
        *offset = (uint16_t)(*offset - FAT32_DIR_ENTRY_SIZE);
        return true;
    }

    uint32_t data_lba = data_start_lba();
    if (*sector <= data_lba) return false;

    uint32_t rel            = *sector - data_lba;
    uint32_t cur_cluster    = rel / bpb.sectors_per_cluster + 2u;
    uint32_t sec_in_cluster = rel % bpb.sectors_per_cluster;

    if (sec_in_cluster > 0u) {
        (*sector)--;
        *offset = (uint16_t)(bpb.bytes_per_sector - FAT32_DIR_ENTRY_SIZE);
        return true;
    }

    if (cur_cluster == dir_cluster) return false;

    uint32_t prev = dir_cluster;
    uint32_t guard = fat32_total_clusters();
    for (uint32_t i = 0u; i < guard; ++i) {
        uint32_t nc = fat_get_next_cluster(prev);
        if (nc == cur_cluster) {
            *sector = cluster_to_lba(prev) + bpb.sectors_per_cluster - 1u;
            *offset = (uint16_t)(bpb.bytes_per_sector - FAT32_DIR_ENTRY_SIZE);
            return true;
        }
        if (nc < 2u || nc >= FAT32_EOC_MARKER) break;
        prev = nc;
    }
    return false;
}

static bool fat32_short_name_exists_impl(uint32_t dir_cluster,
                                          const uint8_t name11[11])
{
    uint32_t cluster = dir_cluster;
    uint32_t guard   = fat32_total_clusters();

    for (uint32_t g = 0u; g < guard; ++g) {
        if (cluster < 2u) break;
        uint32_t lba = cluster_to_lba(cluster);
        if (lba == 0u) return false;

        for (uint8_t sec = 0u; sec < bpb.sectors_per_cluster; ++sec) {
            if (!disk_read(lba + sec, g_sector_buffer, 1)) return false;
            for (uint32_t off = 0u; off < bpb.bytes_per_sector; off += FAT32_DIR_ENTRY_SIZE) {
                uint8_t first = g_sector_buffer[off];
                if (first == 0x00u) return false;
                if (first == 0xE5u) continue;
                uint8_t attr = g_sector_buffer[off + 11u];
                if (attr == FAT32_ATTR_LFN) continue;
                if (attr & FAT32_ATTR_VOLUME_ID) continue;
                bool match = true;
                for (int i = 0; i < 11; ++i) {
                    if (g_sector_buffer[off + (uint32_t)i] != name11[i]) { match = false; break; }
                }
                if (match) return true;
            }
        }
        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= FAT32_EOC_MARKER) break;
        cluster = next;
    }
    return false;
}

static bool fat32_gen_short_name(const char *long_name,
                                  uint32_t    dir_cluster,
                                  uint8_t     out11[11])
{
    if (!long_name || !out11) return false;

    if (fat32_name_is_simple_83(long_name))
        return fat32_build_short_name_83(long_name, out11);

    const char *last_dot = NULL;
    for (const char *p = long_name; *p; ++p)
        if (*p == '.') last_dot = p;

    uint8_t ext[3] = {' ', ' ', ' '};
    if (last_dot) {
        uint32_t ei = 0u;
        for (const char *p = last_dot + 1u; *p && ei < 3u; ++p) {
            if (*p == ' ' || *p == '.') continue;
            ext[ei++] = fat32_sanitize_83_char(*p);
        }
    }

    uint8_t base_chars[6];
    uint32_t base_len = 0u;
    for (const char *p = long_name;
         (last_dot ? (p < last_dot) : (*p != '\0')) && base_len < 6u;
         ++p) {
        if (*p == ' ' || *p == '.') continue;
        base_chars[base_len++] = fat32_sanitize_83_char(*p);
    }
    if (base_len == 0u) base_chars[base_len++] = '_';

    for (uint32_t n = 1u; n <= 9999u; ++n) {
        char     num_str[5];
        uint32_t num_len    = fat32_u32_to_str(n, num_str);
        uint32_t suffix_len = 1u + num_len;
        uint32_t base_part  = (8u > suffix_len) ? (8u - suffix_len) : 0u;
        if (base_part > base_len) base_part = base_len;

        uint8_t candidate[11];
        for (int i = 0; i < 11; ++i) candidate[i] = ' ';

        /* Base chars */
        for (uint32_t i = 0u; i < base_part; ++i) candidate[i] = base_chars[i];
        /* Tilde + number */
        candidate[base_part] = '~';
        for (uint32_t i = 0u; i < num_len; ++i)
            candidate[base_part + 1u + i] = (uint8_t)num_str[i];
        /* Extension */
        for (uint32_t i = 0u; i < 3u; ++i) candidate[8u + i] = ext[i];

        if (!fat32_short_name_exists_impl(dir_cluster, candidate)) {
            memcpy(out11, candidate, 11u);
            return true;
        }
    }
    return false;
}

static bool fat32_find_consecutive_free(uint32_t  dir_cluster,
                                         uint32_t  count,
                                         uint32_t *first_sector_out,
                                         uint16_t *first_offset_out)
{
    if (!first_sector_out || !first_offset_out || count == 0u || dir_cluster < 2u)
        return false;

    uint32_t run_sector = 0u;
    uint16_t run_offset = 0u;
    uint32_t run_len    = 0u;

    uint32_t cluster = dir_cluster;
    uint32_t guard   = fat32_total_clusters();
    uint32_t last_cluster = dir_cluster;

    for (uint32_t g = 0u; g <= guard; ++g) {
        if (cluster < 2u) break;
        last_cluster = cluster;

        uint32_t lba = cluster_to_lba(cluster);
        if (lba == 0u) return false;

        for (uint8_t sec = 0u; sec < bpb.sectors_per_cluster; ++sec) {
            if (!disk_read(lba + sec, g_sector_buffer, 1)) return false;

            for (uint32_t off = 0u; off < bpb.bytes_per_sector; off += FAT32_DIR_ENTRY_SIZE) {
                uint8_t first = g_sector_buffer[off];
                bool is_free  = (first == 0x00u || first == 0xE5u);

                if (is_free) {
                    if (run_len == 0u) {
                        run_sector = lba + sec;
                        run_offset = (uint16_t)off;
                    }
                    ++run_len;
                    if (run_len >= count) {
                        *first_sector_out = run_sector;
                        *first_offset_out = run_offset;
                        return true;
                    }

                    if (first == 0x00u) {
                        uint32_t entries_left_in_sector =
                            (bpb.bytes_per_sector - off) / FAT32_DIR_ENTRY_SIZE - 1u;
                        uint32_t entries_left =
                            entries_left_in_sector +
                            (uint32_t)(bpb.sectors_per_cluster - sec - 1u) *
                            (bpb.bytes_per_sector / FAT32_DIR_ENTRY_SIZE);
                        if (run_len + entries_left >= count) {
                            *first_sector_out = run_sector;
                            *first_offset_out = run_offset;
                            return true;
                        }
                        goto need_new_cluster;
                    }
                } else {
                    run_len = 0u;
                }
            }
        }

        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= FAT32_EOC_MARKER) goto need_new_cluster;
        cluster = next;
    }

need_new_cluster:;
    uint32_t new_cluster = fat32_allocate_cluster_zeroed();
    if (new_cluster < 2u) return false;

    if (!fat_set_next_cluster(last_cluster, new_cluster)) {
        (void)fat_set_next_cluster(new_cluster, 0u);
        return false;
    }

    uint32_t entries_in_new =
        ((uint32_t)bpb.sectors_per_cluster * bpb.bytes_per_sector) / FAT32_DIR_ENTRY_SIZE;

    if (run_len + entries_in_new >= count) {
        *first_sector_out = (run_len > 0u) ? run_sector : cluster_to_lba(new_cluster);
        *first_offset_out = (run_len > 0u) ? run_offset : 0u;
        return (*first_sector_out != 0u);
    }
    return false;
}

static bool fat32_write_entry_sequence(uint32_t    first_sector,
                                        uint16_t    first_offset,
                                        const char *long_name,
                                        const uint8_t short_name83[11],
                                        uint8_t     attr,
                                        uint32_t    first_cluster,
                                        uint32_t    file_size)
{
    uint32_t sector = first_sector;
    uint16_t offset = first_offset;

    uint32_t name_len  = fat32_strlen(long_name);
    bool     needs_lfn = !fat32_name_is_simple_83(long_name);
    uint8_t  lfn_count = needs_lfn ? fat32_lfn_count_needed(long_name) : 0u;
    uint8_t  checksum  = fat32_lfn_checksum(short_name83);

    for (uint8_t k = 0u; k < lfn_count; ++k) {
        uint8_t order   = (uint8_t)(lfn_count - k);
        bool    is_last = (k == 0u);

        uint8_t lfn_buf[FAT32_DIR_ENTRY_SIZE];
        fat32_build_lfn_entry(lfn_buf, order, is_last, checksum, long_name, name_len);

        if (!disk_read(sector, g_sector_buffer, 1)) return false;
        memcpy(&g_sector_buffer[offset], lfn_buf, FAT32_DIR_ENTRY_SIZE);
        if (!disk_write(sector, g_sector_buffer, 1)) return false;

        if (k + 1u < lfn_count) {
            if (!fat32_dir_pos_advance(&sector, &offset)) return false;
        }
    }

    if (lfn_count > 0u) {
        if (!fat32_dir_pos_advance(&sector, &offset)) return false;
    }

    if (!disk_read(sector, g_sector_buffer, 1)) return false;
    uint8_t *entry = &g_sector_buffer[offset];
    memset(entry, 0, FAT32_DIR_ENTRY_SIZE);
    memcpy(entry, short_name83, 11u);
    entry[11] = attr;

    entry[20] = (uint8_t)((first_cluster >> 16) & 0xFFu);
    entry[21] = (uint8_t)((first_cluster >> 24) & 0xFFu);
    entry[26] = (uint8_t)( first_cluster        & 0xFFu);
    entry[27] = (uint8_t)((first_cluster >>  8) & 0xFFu);

    entry[28] = (uint8_t)( file_size        & 0xFFu);
    entry[29] = (uint8_t)((file_size >>  8) & 0xFFu);
    entry[30] = (uint8_t)((file_size >> 16) & 0xFFu);
    entry[31] = (uint8_t)((file_size >> 24) & 0xFFu);
    return disk_write(sector, g_sector_buffer, 1);
}

static bool fat32_delete_preceding_lfn(uint32_t dir_cluster,
                                        uint32_t short_name_sector,
                                        uint16_t short_name_offset,
                                        uint8_t  lfn_count)
{
    if (lfn_count == 0u) return true;

    uint32_t sector = short_name_sector;
    uint16_t offset = short_name_offset;

    for (uint8_t i = 0u; i < lfn_count; ++i) {
        if (!fat32_dir_pos_prev(dir_cluster, &sector, &offset)) {
            serial_write_string("[OS] [FAT32] LFN unlink: can't navigate backward\n");
            return false;
        }
        if (!disk_read(sector, g_sector_buffer, 1)) return false;
        g_sector_buffer[offset] = 0xE5u;
        if (!disk_write(sector, g_sector_buffer, 1)) return false;
    }
    return true;
}

static void fat32_fill_file_from_entry(const uint8_t *entry,
                                        const char    *resolved_name,
                                        FAT32_FILE    *file,
                                        uint32_t       entry_sector,
                                        uint16_t       entry_offset,
                                        uint8_t        lfn_entry_count,
                                        uint32_t       dir_cluster)
{
    uint16_t high = read_u16(&entry[20]);
    uint16_t low  = read_u16(&entry[26]);
    file->first_cluster    = ((uint32_t)high << 16) | low;
    file->size             = read_u32(&entry[28]);
    file->attributes       = entry[11];
    file->dir_entry_sector = entry_sector;
    file->dir_entry_offset = entry_offset;
    file->lfn_entry_count  = lfn_entry_count;
    file->dir_cluster      = dir_cluster;
    string_copy_limit(file->name, sizeof(file->name), resolved_name);
}

static bool fat32_update_file_directory_entry(const FAT32_FILE *file) {
    if (!file || file->dir_entry_sector == 0u ||
        file->dir_entry_offset + FAT32_DIR_ENTRY_SIZE > bpb.bytes_per_sector)
        return false;
    if (!disk_read(file->dir_entry_sector, g_sector_buffer, 1)) return false;

    uint8_t *entry = &g_sector_buffer[file->dir_entry_offset];
    entry[20] = (uint8_t)((file->first_cluster >> 16) & 0xFFu);
    entry[21] = (uint8_t)((file->first_cluster >> 24) & 0xFFu);
    entry[26] = (uint8_t)( file->first_cluster        & 0xFFu);
    entry[27] = (uint8_t)((file->first_cluster >>  8) & 0xFFu);
    entry[28] = (uint8_t)( file->size        & 0xFFu);
    entry[29] = (uint8_t)((file->size >>  8) & 0xFFu);
    entry[30] = (uint8_t)((file->size >> 16) & 0xFFu);
    entry[31] = (uint8_t)((file->size >> 24) & 0xFFu);
    return disk_write(file->dir_entry_sector, g_sector_buffer, 1);
}

static bool fat32_lookup_entry_in_directory(uint32_t    dir_cluster,
                                             const char *target_name,
                                             FAT32_FILE *out_file)
{
    if (dir_cluster < 2u || !target_name || !out_file || target_name[0] == '\0')
        return false;

    char    lfn_name[FAT32_MAX_NAME_LEN];
    int     lfn_valid       = 0;
    uint8_t lfn_entry_count = 0u;
    memset(lfn_name, 0, sizeof(lfn_name));

    uint32_t cluster      = dir_cluster;
    uint32_t max_clusters = fat32_total_clusters();

    for (uint32_t c = 0u; c < max_clusters; ++c) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!lba) return false;

        for (uint8_t sec = 0u; sec < bpb.sectors_per_cluster; ++sec) {
            if (!disk_read(lba + sec, g_sector_buffer, 1)) return false;

            for (uint32_t i = 0u; i < bpb.bytes_per_sector; i += FAT32_DIR_ENTRY_SIZE) {
                const uint8_t *entry = &g_sector_buffer[i];
                uint8_t first = entry[0];

                if (first == 0x00u) return false;
                if (first == 0xE5u) {
                    lfn_valid = 0; lfn_entry_count = 0;
                    memset(lfn_name, 0, sizeof(lfn_name));
                    continue;
                }

                uint8_t attr = entry[11];

                if (attr == FAT32_ATTR_LFN) {
                    uint8_t seq   = entry[0];
                    uint8_t order = seq & 0x1Fu;
                    if (order == 0u || order > FAT32_MAX_LFN_ORDER) {
                        lfn_valid = 0; lfn_entry_count = 0;
                        memset(lfn_name, 0, sizeof(lfn_name));
                        continue;
                    }
                    if (seq & 0x40u) {
                        memset(lfn_name, 0, sizeof(lfn_name));
                        lfn_valid = 1; lfn_entry_count = 0;
                    }
                    if (!lfn_valid ||
                        !fat32_decode_lfn_entry(entry, lfn_name, sizeof(lfn_name))) {
                        lfn_valid = 0; lfn_entry_count = 0;
                        memset(lfn_name, 0, sizeof(lfn_name));
                    } else if (lfn_entry_count < 0xFFu) {
                        ++lfn_entry_count;
                    }
                    continue;
                }

                if (attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_valid = 0; lfn_entry_count = 0;
                    memset(lfn_name, 0, sizeof(lfn_name));
                    continue;
                }

                char short_name[13];
                fat32_short_name_to_string(entry, short_name);
                const char *candidate =
                    (lfn_valid && lfn_name[0] != '\0') ? lfn_name : short_name;

                if (fat32_name_equal(candidate, target_name)) {
                    fat32_fill_file_from_entry(entry, candidate, out_file,
                                               lba + sec, (uint16_t)i,
                                               lfn_entry_count,
                                               dir_cluster);  /* [BUG-4] */
                    return true;
                }

                lfn_valid = 0; lfn_entry_count = 0;
                memset(lfn_name, 0, sizeof(lfn_name));
            }
        }

        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= 0x0FFFFFF8u) break;
        cluster = next;
    }
    return false;
}

static bool fat32_validate_bpb(void) {
    if (bpb.bytes_per_sector < FAT32_MIN_SECTOR_SIZE ||
        bpb.bytes_per_sector > FAT32_MAX_SECTOR_SIZE) {
        serial_write_string("[OS] [FAT32] Invalid sector size\n"); return false;
    }
    if (bpb.sectors_per_cluster == 0 ||
        bpb.sectors_per_cluster > FAT32_MAX_SECTORS_PER_CLUSTER) {
        serial_write_string("[OS] [FAT32] Invalid sectors per cluster\n"); return false;
    }
    if (bpb.reserved_sectors == 0 ||
        bpb.reserved_sectors > FAT32_MAX_RESERVED_SECTORS) {
        serial_write_string("[OS] [FAT32] Invalid reserved sectors\n"); return false;
    }
    if (bpb.num_fats == 0 || bpb.num_fats > FAT32_MAX_NUM_FATS) {
        serial_write_string("[OS] [FAT32] Invalid number of FATs\n"); return false;
    }
    if (bpb.fat_size_sectors == 0 ||
        bpb.fat_size_sectors > FAT32_MAX_FAT_SIZE_SECTORS) {
        serial_write_string("[OS] [FAT32] Invalid FAT size\n"); return false;
    }
    if (bpb.num_fats > FAT32_MAX_FAT_SIZE_SECTORS / bpb.fat_size_sectors) {
        serial_write_string("[OS] [FAT32] FAT area overflow\n"); return false;
    }
    if (bpb.root_cluster < 2u) {
        serial_write_string("[OS] [FAT32] Invalid root cluster\n"); return false;
    }
    uint32_t cluster_size = (uint32_t)bpb.sectors_per_cluster * bpb.bytes_per_sector;
    if (cluster_size == 0u || cluster_size > FAT32_CLUSTER_BUFFER_SIZE) {
        serial_write_string("[OS] [FAT32] Cluster size exceeds buffer\n"); return false;
    }
    if (data_start_lba() == 0u &&
        bpb.reserved_sectors + bpb.num_fats * bpb.fat_size_sectors > 0u) {
        serial_write_string("[OS] [FAT32] Data area calculation overflow\n"); return false;
    }
    return true;
}

static bool fat32_lookup_path(const char *path, FAT32_FILE *out_entry) {
    if (!path || !out_entry) return false;
    const char *cursor = skip_path_separators(path);
    if (!cursor || *cursor == '\0') return false;

    uint32_t   current_dir_cluster = bpb.root_cluster;
    FAT32_FILE current_entry;
    char       component[FAT32_MAX_NAME_LEN];
    bool       has_more = false;

    while (1) {
        const char *next = extract_path_component(cursor, component,
                                                   sizeof(component), &has_more);
        if (!next || component[0] == '\0') return false;
        if (!fat32_lookup_entry_in_directory(current_dir_cluster, component,
                                              &current_entry)) return false;
        if (!has_more) { *out_entry = current_entry; return true; }
        if ((current_entry.attributes & FAT32_ATTR_DIRECTORY) == 0) return false;
        if (current_entry.first_cluster < 2u) return false;
        current_dir_cluster = current_entry.first_cluster;
        cursor = next;
    }
}

uint32_t fat32_get_file_size(FAT32_FILE *file) {
    return file ? file->size : 0u;
}

static bool fat32_seek_to_offset(const FAT32_FILE *file, uint32_t offset,
                                  uint32_t cluster_size,
                                  uint32_t *cluster_out,
                                  uint32_t *offset_in_cluster_out) {
    if (!file || !cluster_out || !offset_in_cluster_out || cluster_size == 0u) return false;
    uint32_t cluster      = file->first_cluster;
    uint32_t max_clusters = fat32_total_clusters();
    uint32_t skip         = offset / cluster_size;
    if (skip > max_clusters) return false;
    for (uint32_t i = 0u; i < skip; ++i) {
        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= 0x0FFFFFF8u) return false;
        cluster = next;
    }
    *cluster_out            = cluster;
    *offset_in_cluster_out  = offset % cluster_size;
    return true;
}

static bool fat32_write_existing_range(FAT32_FILE *file, uint32_t offset,
                                        const uint8_t *buffer, uint32_t size) {
    if (!file || !buffer) return false;
    if (size == 0u) return true;

    uint32_t cluster_size = fat32_cluster_size_bytes();
    if (cluster_size == 0u || cluster_size > FAT32_CLUSTER_BUFFER_SIZE) {
        serial_write_string("[OS] [FAT32] Invalid cluster size\n"); return false;
    }
    if (file->first_cluster < 2u) {
        serial_write_string("[OS] [FAT32] Invalid start cluster\n"); return false;
    }

    uint32_t cluster = 0u, offset_in_cluster = 0u;
    if (!fat32_seek_to_offset(file, offset, cluster_size, &cluster, &offset_in_cluster)) {
        serial_write_string("[OS] [FAT32] Invalid cluster chain\n"); return false;
    }

    uint32_t bytes_left = size;
    while (bytes_left) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!lba) { serial_write_string("[OS] [FAT32] Invalid cluster LBA\n"); return false; }

        uint32_t bytes_in_cluster = cluster_size - offset_in_cluster;
        uint32_t n_write = bytes_left > bytes_in_cluster ? bytes_in_cluster : bytes_left;

        if (offset_in_cluster == 0u && n_write == cluster_size) {
            if (!disk_write(lba, buffer, bpb.sectors_per_cluster)) {
                serial_write_string("[OS] [FAT32] Disk write failed\n"); return false;
            }
        } else {
            if (!disk_read(lba, g_read_buffer, bpb.sectors_per_cluster)) {
                serial_write_string("[OS] [FAT32] Disk read failed\n"); return false;
            }
            memcpy(g_read_buffer + offset_in_cluster, buffer, n_write);
            if (!disk_write(lba, g_read_buffer, bpb.sectors_per_cluster)) {
                serial_write_string("[OS] [FAT32] Disk write failed\n"); return false;
            }
        }
        buffer += n_write; bytes_left -= n_write; offset_in_cluster = 0u;
        if (bytes_left) {
            uint32_t next = fat_get_next_cluster(cluster);
            if (next < 2u || next >= FAT32_EOC_MARKER) {
                serial_write_string("[OS] [FAT32] Cluster chain ended early\n"); return false;
            }
            cluster = next;
        }
    }
    return true;
}

static bool fat32_zero_fill_range(FAT32_FILE *file, uint32_t offset, uint32_t size) {
    if (!file || size == 0u) return true;
    static uint8_t zero_buf[256];
    memset(zero_buf, 0, sizeof(zero_buf));
    uint32_t remaining = size, cursor = offset;
    while (remaining > 0u) {
        uint32_t chunk = remaining > (uint32_t)sizeof(zero_buf)
                         ? (uint32_t)sizeof(zero_buf) : remaining;
        if (!fat32_write_existing_range(file, cursor, zero_buf, chunk)) return false;
        cursor += chunk; remaining -= chunk;
    }
    return true;
}

bool fat32_read_at(FAT32_FILE *file, uint32_t offset, uint8_t *buffer, uint32_t size) {
    if (!file || !buffer) return false;
    if (size == 0u) return offset <= file->size;
    if (offset > file->size || size > file->size - offset) return false;

    uint32_t cluster_size = fat32_cluster_size_bytes();
    if (cluster_size == 0u || cluster_size > FAT32_CLUSTER_BUFFER_SIZE) {
        serial_write_string("[OS] [FAT32] Invalid cluster size\n"); return false;
    }
    if (file->first_cluster < 2u) {
        serial_write_string("[OS] [FAT32] Invalid start cluster\n"); return false;
    }

    uint32_t cluster = 0u, offset_in_cluster = 0u;
    if (!fat32_seek_to_offset(file, offset, cluster_size, &cluster, &offset_in_cluster)) {
        serial_write_string("[OS] [FAT32] Invalid cluster chain\n"); return false;
    }

    uint32_t bytes_left = size;
    while (bytes_left) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!lba) { serial_write_string("[OS] [FAT32] Invalid cluster LBA\n"); return false; }
        if (!disk_read(lba, g_read_buffer, bpb.sectors_per_cluster)) {
            serial_write_string("[OS] [FAT32] Disk read failed\n"); return false;
        }
        uint32_t bytes_in_cluster = cluster_size - offset_in_cluster;
        uint32_t n_read = bytes_left > bytes_in_cluster ? bytes_in_cluster : bytes_left;
        memcpy(buffer, g_read_buffer + offset_in_cluster, n_read);
        buffer += n_read; bytes_left -= n_read; offset_in_cluster = 0u;
        if (bytes_left) {
            uint32_t next = fat_get_next_cluster(cluster);
            if (next < 2u || next >= 0x0FFFFFF8u) {
                serial_write_string("[OS] [FAT32] Cluster chain ended early\n"); return false;
            }
            cluster = next;
        }
    }
    return true;
}

bool fat32_truncate(FAT32_FILE *file, uint32_t new_size) {
    if (!file) return false;
    if (new_size == file->size) return true;

    uint32_t cluster_size = fat32_cluster_size_bytes();
    if (cluster_size == 0u || cluster_size > FAT32_CLUSTER_BUFFER_SIZE) return false;

    if (new_size == 0u) {
        if (!fat32_free_cluster_chain(file->first_cluster)) return false;
        file->first_cluster = 0u;
        file->size          = 0u;
        return fat32_update_file_directory_entry(file);
    }

    uint32_t needed = fat32_clusters_for_size(new_size, cluster_size);
    if (!fat32_ensure_cluster_count(file, needed)) return false;

    if (new_size < file->size) {
        uint32_t last_keep = 0u;
        if (!fat32_get_cluster_at_index(file, needed - 1u, &last_keep)) return false;
        uint32_t tail = fat_get_next_cluster(last_keep);
        if (!fat_set_next_cluster(last_keep, FAT32_EOC_MARKER)) return false;
        if (tail >= 2u && tail < FAT32_EOC_MARKER) {
            if (!fat32_free_cluster_chain(tail)) return false;
        }
    }

    file->size = new_size;
    return fat32_update_file_directory_entry(file);
}

bool fat32_write_at(FAT32_FILE *file, uint32_t offset,
                    const uint8_t *buffer, uint32_t size) {
    if (!file) return false;
    if (size == 0u) return fat32_truncate(file, offset);
    if (!buffer) return false;
    if (offset > 0xFFFFFFFFu - size) return false;

    uint32_t final_size = offset + size;
    uint32_t old_size   = file->size;

    if (final_size > file->size) {
        if (!fat32_truncate(file, final_size)) return false;
        if (offset > old_size) {
            if (!fat32_zero_fill_range(file, old_size, offset - old_size)) return false;
        }
    }
    if (offset > file->size || size > file->size - offset) return false;
    return fat32_write_existing_range(file, offset, buffer, size);
}

bool fat32_write_file(FAT32_FILE *file, const uint8_t *buffer) {
    if (!file || !buffer) return false;
    return fat32_write_at(file, 0u, buffer, file->size);
}

bool fat32_read_file(FAT32_FILE *file, uint8_t *buffer) {
    if (!file || !buffer) return false;
    return fat32_read_at(file, 0u, buffer, file->size);
}

static bool fat32_split_parent_and_name(const char *path,
                                         char *parent_out, uint32_t parent_size,
                                         char *name_out,   uint32_t name_size) {
    if (!path || !parent_out || !name_out || parent_size == 0u || name_size == 0u) return false;
    const char *clean = skip_path_separators(path);
    if (!clean || clean[0] == '\0') return false;

    uint32_t len = fat32_strlen(clean);
    if (len + 1u > FAT32_PATH_MAX) return false;

    char tmp[FAT32_PATH_MAX];
    string_copy_limit(tmp, sizeof(tmp), clean);

    int32_t sep = -1;
    for (int32_t i = (int32_t)len - 1; i >= 0; --i) {
        if (tmp[i] == '/' || tmp[i] == '\\') { sep = i; break; }
    }
    if (sep < 0) {
        parent_out[0] = '/'; parent_out[1] = '\0';
        string_copy_limit(name_out, name_size, tmp);
        return name_out[0] != '\0';
    }
    tmp[sep] = '\0';
    string_copy_limit(name_out, name_size, tmp + sep + 1);
    if (name_out[0] == '\0') return false;
    if (tmp[0] == '\0') { parent_out[0] = '/'; parent_out[1] = '\0'; }
    else string_copy_limit(parent_out, parent_size, tmp);
    return true;
}

static bool fat32_resolve_directory_cluster(const char *path, uint32_t *cluster_out) {
    if (!cluster_out || !path) return false;
    const char *clean = skip_path_separators(path);
    if (!clean || clean[0] == '\0') { *cluster_out = bpb.root_cluster; return true; }

    FAT32_FILE dir;
    if (!fat32_lookup_path(path, &dir)) return false;
    if ((dir.attributes & FAT32_ATTR_DIRECTORY) == 0u) return false;
    if (dir.first_cluster < 2u) return false;
    *cluster_out = dir.first_cluster;
    return true;
}

static void fat32_write_cluster_to_entry(uint8_t *entry, uint32_t cluster) {
    if (!entry) return;
    entry[20] = (uint8_t)((cluster >> 16) & 0xFFu);
    entry[21] = (uint8_t)((cluster >> 24) & 0xFFu);
    entry[26] = (uint8_t)( cluster        & 0xFFu);
    entry[27] = (uint8_t)((cluster >>  8) & 0xFFu);
}

bool fat32_init(void) {
    if (!disk_read(0, g_sector_buffer, 1)) {
        serial_write_string("[OS] [FAT32] Boot sector read failed\n"); return false;
    }
    bpb.bytes_per_sector    = read_u16(&g_sector_buffer[11]);
    bpb.sectors_per_cluster = g_sector_buffer[13];
    bpb.reserved_sectors    = read_u16(&g_sector_buffer[14]);
    bpb.num_fats            = g_sector_buffer[16];
    bpb.fat_size_sectors    = read_u32(&g_sector_buffer[36]);
    bpb.root_cluster        = read_u32(&g_sector_buffer[44]);
    bpb.total_sectors       = read_u32(&g_sector_buffer[32]);

    if (!fat32_validate_bpb()) return false;
    memset(g_dir_handles, 0, sizeof(g_dir_handles));
    g_case_sensitive_lookup = false;
    serial_write_string("[OS] [FAT32] Init Success\n");
    return true;
}

bool fat32_find_file(const char *filename, FAT32_FILE *file) {
    if (!filename || !file) return false;
    if (!fat32_lookup_path(filename, file)) return false;
    if (file->attributes & FAT32_ATTR_DIRECTORY) return false;
    return true;
}

bool fat32_creat(const char *path)
{
    if (!path || path[0] == '\0') return false;

    char parent[FAT32_PATH_MAX], name[FAT32_PATH_MAX];
    if (!fat32_split_parent_and_name(path, parent, sizeof(parent),
                                      name,   sizeof(name)))  return false;

    /* Validate name length for LFN */
    uint32_t name_len = fat32_strlen(name);
    if (name_len == 0u || name_len >= FAT32_MAX_NAME_LEN) {
        serial_write_string("[OS] [FAT32] creat: name too long\n"); return false;
    }

    uint32_t parent_cluster = 0u;
    if (!fat32_resolve_directory_cluster(parent, &parent_cluster)) return false;

    FAT32_FILE existing;
    if (fat32_lookup_entry_in_directory(parent_cluster, name, &existing)) {
        if (existing.attributes & FAT32_ATTR_DIRECTORY) return false;
        return fat32_truncate(&existing, 0u);
    }

    uint8_t short_name83[11];
    if (!fat32_gen_short_name(name, parent_cluster, short_name83)) {
        serial_write_string("[OS] [FAT32] creat: cannot generate short name\n"); return false;
    }

    bool    needs_lfn = !fat32_name_is_simple_83(name);
    uint8_t lfn_count = needs_lfn ? fat32_lfn_count_needed(name) : 0u;
    uint32_t total_slots = (uint32_t)lfn_count + 1u;

    uint32_t first_sector = 0u;
    uint16_t first_offset = 0u;
    if (!fat32_find_consecutive_free(parent_cluster, total_slots,
                                      &first_sector, &first_offset)) {
        serial_write_string("[OS] [FAT32] creat: no free directory entries\n"); return false;
    }

    return fat32_write_entry_sequence(first_sector, first_offset,
                                       name, short_name83,
                                       FAT32_ATTR_ARCHIVE, 0u, 0u);
}

bool fat32_mkdir(const char *path)
{
    if (!path || path[0] == '\0') return false;

    char parent[FAT32_PATH_MAX], name[FAT32_PATH_MAX];
    if (!fat32_split_parent_and_name(path, parent, sizeof(parent),
                                      name,   sizeof(name)))  return false;

    uint32_t name_len = fat32_strlen(name);
    if (name_len == 0u || name_len >= FAT32_MAX_NAME_LEN) {
        serial_write_string("[OS] [FAT32] mkdir: name too long\n"); return false;
    }

    uint32_t parent_cluster = 0u;
    if (!fat32_resolve_directory_cluster(parent, &parent_cluster)) return false;

    FAT32_FILE existing;
    if (fat32_lookup_entry_in_directory(parent_cluster, name, &existing)) return false;

    uint8_t short_name83[11];
    if (!fat32_gen_short_name(name, parent_cluster, short_name83)) {
        serial_write_string("[OS] [FAT32] mkdir: cannot generate short name\n"); return false;
    }

    uint32_t new_dir_cluster = fat32_allocate_cluster_zeroed();
    if (new_dir_cluster < 2u) return false;

    uint32_t dot_dot_cluster =
        (parent_cluster == bpb.root_cluster) ? 0u : parent_cluster;

    uint32_t lba = cluster_to_lba(new_dir_cluster);
    if (lba == 0u || !disk_read(lba, g_sector_buffer, 1)) {
        (void)fat32_free_cluster_chain(new_dir_cluster); return false;
    }
    memset(&g_sector_buffer[0],                    0, FAT32_DIR_ENTRY_SIZE * 2u);
    for (uint32_t i = 0u; i < 11u; ++i) {
        g_sector_buffer[i]                    = ' ';
        g_sector_buffer[FAT32_DIR_ENTRY_SIZE + i] = ' ';
    }
    g_sector_buffer[0]  = '.';
    g_sector_buffer[11] = FAT32_ATTR_DIRECTORY;
    fat32_write_cluster_to_entry(&g_sector_buffer[0], new_dir_cluster);

    g_sector_buffer[FAT32_DIR_ENTRY_SIZE + 0] = '.';
    g_sector_buffer[FAT32_DIR_ENTRY_SIZE + 1] = '.';
    g_sector_buffer[FAT32_DIR_ENTRY_SIZE + 11] = FAT32_ATTR_DIRECTORY;
    fat32_write_cluster_to_entry(&g_sector_buffer[FAT32_DIR_ENTRY_SIZE], dot_dot_cluster);

    if (!disk_write(lba, g_sector_buffer, 1)) {
        (void)fat32_free_cluster_chain(new_dir_cluster); return false;
    }

    bool    needs_lfn  = !fat32_name_is_simple_83(name);
    uint8_t lfn_count  = needs_lfn ? fat32_lfn_count_needed(name) : 0u;
    uint32_t total_slots = (uint32_t)lfn_count + 1u;

    uint32_t first_sector = 0u;
    uint16_t first_offset = 0u;
    if (!fat32_find_consecutive_free(parent_cluster, total_slots,
                                      &first_sector, &first_offset)) {
        (void)fat32_free_cluster_chain(new_dir_cluster);
        serial_write_string("[OS] [FAT32] mkdir: no free directory entries\n"); return false;
    }

    if (!fat32_write_entry_sequence(first_sector, first_offset,
                                     name, short_name83,
                                     FAT32_ATTR_DIRECTORY,
                                     new_dir_cluster, 0u)) {
        (void)fat32_free_cluster_chain(new_dir_cluster); return false;
    }
    return true;
}

bool fat32_unlink(const char *path)
{
    FAT32_FILE file;
    if (!path || !fat32_lookup_path(path, &file)) return false;
    if (file.attributes & FAT32_ATTR_DIRECTORY)   return false;

    if (!fat32_free_cluster_chain(file.first_cluster)) return false;

    if (file.dir_entry_sector == 0u ||
        file.dir_entry_offset + FAT32_DIR_ENTRY_SIZE > bpb.bytes_per_sector) return false;

    if (file.lfn_entry_count > 0u) {
        if (!fat32_delete_preceding_lfn(file.dir_cluster,
                                         file.dir_entry_sector,
                                         file.dir_entry_offset,
                                         file.lfn_entry_count)) {
            serial_write_string("[OS] [FAT32] unlink: LFN entry deletion failed\n");
        }
    }

    if (!disk_read(file.dir_entry_sector, g_sector_buffer, 1)) return false;
    uint32_t off = file.dir_entry_offset;
    g_sector_buffer[off]      = 0xE5u;
    g_sector_buffer[off + 20] = 0u;
    g_sector_buffer[off + 21] = 0u;
    g_sector_buffer[off + 26] = 0u;
    g_sector_buffer[off + 27] = 0u;
    g_sector_buffer[off + 28] = 0u;
    g_sector_buffer[off + 29] = 0u;
    g_sector_buffer[off + 30] = 0u;
    g_sector_buffer[off + 31] = 0u;
    return disk_write(file.dir_entry_sector, g_sector_buffer, 1);
}

void fat32_list_root_files(void) {
    uint32_t cluster = bpb.root_cluster;
    char     lfn_name[FAT32_MAX_NAME_LEN];
    int      lfn_valid = 0;
    memset(lfn_name, 0, sizeof(lfn_name));
    uint32_t max_clusters = fat32_total_clusters();

    for (uint32_t c = 0u; c < max_clusters; ++c) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!lba) return;

        for (uint8_t sec = 0u; sec < bpb.sectors_per_cluster; ++sec) {
            if (!disk_read(lba + sec, g_sector_buffer, 1)) return;

            for (uint32_t i = 0u; i < bpb.bytes_per_sector; i += FAT32_DIR_ENTRY_SIZE) {
                uint8_t first = g_sector_buffer[i];
                if (first == 0x00u) return;
                if (first == 0xE5u) { lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name)); continue; }

                uint8_t attr = g_sector_buffer[i + 11];
                if (attr == FAT32_ATTR_LFN) {
                    uint8_t seq   = g_sector_buffer[i];
                    uint8_t order = seq & 0x1Fu;
                    if (order == 0u || order > FAT32_MAX_LFN_ORDER) {
                        lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name)); continue;
                    }
                    if (seq & 0x40u) { memset(lfn_name, 0, sizeof(lfn_name)); lfn_valid = 1; }
                    if (!lfn_valid ||
                        !fat32_decode_lfn_entry(&g_sector_buffer[i], lfn_name, sizeof(lfn_name))) {
                        lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name));
                    }
                    continue;
                }
                if (attr & FAT32_ATTR_VOLUME_ID) { lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name)); continue; }

                char short_name[13];
                fat32_short_name_to_string(&g_sector_buffer[i], short_name);
                const char *display = (lfn_valid && lfn_name[0] != '\0') ? lfn_name : short_name;

                serial_write_string(display);
                if (attr & FAT32_ATTR_DIRECTORY) serial_write_string("/");
                serial_write_string("\n");

                lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name));
            }
        }
        uint32_t next = fat_get_next_cluster(cluster);
        if (next < 2u || next >= 0x0FFFFFF8u) break;
        cluster = next;
    }
}

int32_t fat32_opendir(const char *path)
{
    uint32_t directory_cluster = 0u;
    if (!fat32_resolve_directory_cluster(path ? path : "/", &directory_cluster)) return -1;
    for (int32_t i = 0; i < FAT32_DIR_HANDLE_MAX; ++i) {
        if (!g_dir_handles[i].used) {
            g_dir_handles[i].used              = 1u;
            g_dir_handles[i].directory_cluster = directory_cluster;
            g_dir_handles[i].current_cluster   = directory_cluster;
            g_dir_handles[i].entry_offset      = 0u;
            return i;
        }
    }
    return -1;
}

int32_t fat32_readdir(int32_t dir_handle, FAT32_DIRENT *out_entry)
{
    if (dir_handle < 0 || dir_handle >= FAT32_DIR_HANDLE_MAX || !out_entry) return -1;
    fat32_dir_handle_t *h = &g_dir_handles[dir_handle];
    if (!h->used || h->current_cluster < 2u) return -1;

    uint32_t cluster_size = fat32_cluster_size_bytes();
    if (cluster_size == 0u) return -1;

    char lfn_name[FAT32_MAX_NAME_LEN];
    int  lfn_valid = 0;
    memset(lfn_name, 0, sizeof(lfn_name));

    while (1) {
        if (h->entry_offset >= cluster_size) {
            uint32_t next = fat_get_next_cluster(h->current_cluster);
            if (next < 2u || next >= FAT32_EOC_MARKER) return 0;
            h->current_cluster = next;
            h->entry_offset    = 0u;
            lfn_valid = 0;
            memset(lfn_name, 0, sizeof(lfn_name));
        }

        uint32_t lba = cluster_to_lba(h->current_cluster);
        if (lba == 0u) return -1;

        uint32_t sec = h->entry_offset / bpb.bytes_per_sector;
        uint32_t off = h->entry_offset % bpb.bytes_per_sector;
        h->entry_offset += FAT32_DIR_ENTRY_SIZE;

        if (!disk_read(lba + sec, g_sector_buffer, 1)) return -1;

        const uint8_t *entry = &g_sector_buffer[off];
        uint8_t first = entry[0];

        if (first == 0x00u) return 0;
        if (first == 0xE5u) { lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name)); continue; }

        uint8_t attr = entry[11];
        if (attr == FAT32_ATTR_LFN) {
            if (entry[0] & 0x40u) { lfn_valid = 1; memset(lfn_name, 0, sizeof(lfn_name)); }
            if (!lfn_valid || !fat32_decode_lfn_entry(entry, lfn_name, sizeof(lfn_name))) {
                lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name));
            }
            continue;
        }
        if (attr & FAT32_ATTR_VOLUME_ID) {
            lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name)); continue;
        }

        char short_name[13];
        fat32_short_name_to_string(entry, short_name);
        const char *candidate = (lfn_valid && lfn_name[0] != '\0') ? lfn_name : short_name;

        lfn_valid = 0; memset(lfn_name, 0, sizeof(lfn_name));

        /* Skip . and .. */
        if ((candidate[0] == '.' && candidate[1] == '\0') ||
            (candidate[0] == '.' && candidate[1] == '.' && candidate[2] == '\0')) continue;

        string_copy_limit(out_entry->name, sizeof(out_entry->name), candidate);
        out_entry->attributes   = attr;
        out_entry->size         = read_u32(&entry[28]);
        out_entry->first_cluster = ((uint32_t)read_u16(&entry[20]) << 16) | read_u16(&entry[26]);
        return 1;
    }
}

int32_t fat32_closedir(int32_t dir_handle)
{
    if (dir_handle < 0 || dir_handle >= FAT32_DIR_HANDLE_MAX) return -1;
    memset(&g_dir_handles[dir_handle], 0, sizeof(g_dir_handles[dir_handle]));
    return 0;
}

void fat32_set_case_sensitive_lookup(bool enabled) { g_case_sensitive_lookup = enabled; }
bool fat32_get_case_sensitive_lookup(void)          { return g_case_sensitive_lookup; }

#ifdef IMPLUS_DRIVER_MODULE
static const fat32_driver_t g_fat32_driver = {
    .init                      = fat32_init,
    .find_file                 = fat32_find_file,
    .read_file                 = fat32_read_file,
    .write_file                = fat32_write_file,
    .read_at                   = fat32_read_at,
    .write_at                  = fat32_write_at,
    .get_file_size             = fat32_get_file_size,
    .list_root_files           = fat32_list_root_files,
    .creat                     = fat32_creat,
    .mkdir                     = fat32_mkdir,
    .opendir                   = fat32_opendir,
    .readdir                   = fat32_readdir,
    .closedir                  = fat32_closedir,
    .unlink                    = fat32_unlink,
    .truncate                  = fat32_truncate,
    .set_case_sensitive_lookup = fat32_set_case_sensitive_lookup,
    .get_case_sensitive_lookup = fat32_get_case_sensitive_lookup,
};

#undef disk_read
#undef disk_write
#undef serial_write_string
#undef memset
#undef memcpy

const fat32_driver_t *driver_module_init(const driver_kernel_api_t *api)
{
    if (!api || !api->disk_read || !api->disk_write ||
        !api->serial_write_string || !api->memset || !api->memcpy)
        return NULL;
    g_driver_api = api;
    return &g_fat32_driver;
}
#endif