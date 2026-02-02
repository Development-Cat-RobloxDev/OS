// FAT32_Main.c (修正版)
#include "FAT32_Main.h"
#include "../../../Kernel_Main.h"
#include <string.h>

static FAT32_BPB bpb;

// --- ヘルパー関数 ---
static uint32_t fat_start_lba() {
    return bpb.reserved_sectors;
}
static uint32_t data_start_lba() {
    return bpb.reserved_sectors + bpb.num_fats * bpb.fat_size_sectors;
}
static uint32_t cluster_to_lba(uint32_t cluster) {
    // クラスタ番号の妥当性チェック
    if(cluster < 2) return 0;
    return data_start_lba() + (cluster - 2) * bpb.sectors_per_cluster;
}

// --- FAT操作 ---
static uint32_t fat_get_next_cluster(uint32_t cluster) {
    // 無効なクラスタ番号のチェック
    if(cluster < 2 || cluster >= 0x0FFFFFF8) return 0x0FFFFFFF;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t sector = fat_start_lba() + (fat_offset / bpb.bytes_per_sector);
    uint32_t offset = fat_offset % bpb.bytes_per_sector;
    
    // オフセットの境界チェック
    if(offset > bpb.bytes_per_sector - 4) return 0x0FFFFFFF;
    
    uint8_t buf[512];
    if(!disk_read(sector, buf, 1)) return 0x0FFFFFFF;
    
    return (*(uint32_t*)&buf[offset]) & 0x0FFFFFFF;
}

uint32_t fat32_get_file_size(FAT32_FILE *file) {
    if(file == NULL) return 0;  // NULLチェック
    return file->size;
}


static bool fat_set_next_cluster(uint32_t cluster, uint32_t next) {
    // 無効なクラスタ番号のチェック
    if(cluster < 2 || cluster >= 0x0FFFFFF8) return false;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t sector = fat_start_lba() + (fat_offset / bpb.bytes_per_sector);
    uint32_t offset = fat_offset % bpb.bytes_per_sector;
    
    // オフセットの境界チェック
    if(offset > bpb.bytes_per_sector - 4) return false;
    
    uint8_t buf[512];
    if(!disk_read(sector, buf, 1)) return false;
    
    buf[offset]     = next & 0xFF;
    buf[offset+1]   = (next >> 8) & 0xFF;
    buf[offset+2]   = (next >> 16) & 0xFF;
    buf[offset+3]   = (buf[offset+3] & 0xF0) | ((next >> 24) & 0x0F);
    
    if(!disk_write(sector, buf, 1)) return false;
    return true;
}
bool fat32_init(void) {
    uint8_t sector[512];
    if(!disk_read(0, sector, 1)) return false;

    bpb.bytes_per_sector    = *(uint16_t*)&sector[11];
    bpb.sectors_per_cluster = sector[13];
    bpb.reserved_sectors    = *(uint16_t*)&sector[14];
    bpb.num_fats            = sector[16];
    bpb.fat_size_sectors    = *(uint32_t*)&sector[36];
    bpb.root_cluster        = *(uint32_t*)&sector[44];

    // BPBパラメータチェック
    if(bpb.bytes_per_sector == 0 || bpb.bytes_per_sector > 4096) return false;
    if(bpb.sectors_per_cluster == 0 || bpb.sectors_per_cluster > 128) return false;
    if(bpb.num_fats == 0 || bpb.num_fats > 2) return false;
    if(bpb.root_cluster < 2) return false;

    serial_write_string("[FAT32] successfully fat32 initialize\n");

    // デバッグ出力
    serial_write_string("Bytes/sector: "); serial_write_uint64(bpb.bytes_per_sector); serial_write_string("\n");
    serial_write_string("Sectors/cluster: "); serial_write_uint64(bpb.sectors_per_cluster); serial_write_string("\n");
    serial_write_string("Reserved sectors: "); serial_write_uint64(bpb.reserved_sectors); serial_write_string("\n");
    serial_write_string("Number of FATs: "); serial_write_uint64(bpb.num_fats); serial_write_string("\n");
    serial_write_string("FAT size sectors: "); serial_write_uint64(bpb.fat_size_sectors); serial_write_string("\n");
    serial_write_string("Root cluster: "); serial_write_uint64(bpb.root_cluster); serial_write_string("\n");

    uint64_t data_lba = bpb.reserved_sectors + bpb.num_fats * bpb.fat_size_sectors;
    serial_write_string("Data start LBA: "); serial_write_uint64(data_lba); serial_write_string("\n");

    uint64_t root_lba = data_lba + (bpb.root_cluster - 2) * bpb.sectors_per_cluster;
    serial_write_string("Root cluster LBA: "); serial_write_uint64(root_lba); serial_write_string("\n");

    // ルートクラスタ先頭セクタの16進ダンプ（先頭64バイトだけ）
    uint8_t buf[512];
    if(disk_read(root_lba, buf, 1)) {
        serial_write_string("Root cluster first 64 bytes:\n");
        for(int i = 0; i < 64; i += 16) {
            char line[33]; // 16バイト * 2 + NULL
            for(int j = 0; j < 16; j++) {
                uint8_t b = buf[i + j];
                uint8_t high = (b >> 4) & 0x0F;
                uint8_t low  = b & 0x0F;
                line[j*2 + 0] = (high < 10) ? ('0' + high) : ('A' + high - 10);
                line[j*2 + 1] = (low  < 10) ? ('0' + low)  : ('A' + low  - 10);
            }
            line[32] = '\0';
            serial_write_string(line);
            serial_write_string("\n");
        }
    }

    return true;
}



// --- ファイル検索(ルートディレクトリのみ、8.3形式) ---
bool fat32_find_file(const char *filename, FAT32_FILE *file) {
    // NULLチェック
    if(filename == NULL || file == NULL) return false;
    
    uint32_t cluster = bpb.root_cluster;
    uint8_t buf[512];
    int loop_count = 0;  // 無限ループ防止
    const int MAX_LOOPS = 10000;

    while(cluster >= 2 && cluster < 0x0FFFFFF8) {
        if(++loop_count > MAX_LOOPS) break;  // 無限ループ防止
        
        for(uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            uint32_t lba = cluster_to_lba(cluster);
            if(lba == 0) break;  // 無効なLBA
            
            if(!disk_read(lba + sec, buf, 1)) break;
            
            for(int i = 0; i < 512; i += 32) {
                if(buf[i] == 0x00) break; // ファイルなし
                if(buf[i] == 0xE5) continue;     // 削除済みエントリ
                if((buf[i+11] & 0x0F) == 0x0F) continue; // ロングファイル名無視
                
                char name[12];
                memcpy(name, &buf[i], 11);
                name[11] = '\0';
                
                // 空白削除
                for(int j = 0; j < 11; j++) {
                    if(name[j] == ' ') name[j] = '\0';
                }
                
                if(strncmp(filename, name, 11) == 0) {
                    uint16_t high = *(uint16_t*)&buf[i+20];
                    uint16_t low = *(uint16_t*)&buf[i+26];
                    file->first_cluster = ((uint32_t)high << 16) | low;
                    file->size = *(uint32_t*)&buf[i+28];
                    memcpy(file->name, &buf[i], 11);
                    file->name[11] = '\0';  // NULL終端を保証

                    // ファイル名出力
                    serial_write_string("[FAT32] found file: ");
                    serial_write_string(name);
                    serial_write_string("\n");

                    return true;
                }
            }
        }
        
        uint32_t next = fat_get_next_cluster(cluster);
        if(next >= 0x0FFFFFF8 || next < 2) break; // 終端または不正クラスタ
        cluster = next;
    }

    return false;
}

// --- ルートディレクトリ内の全ファイル名を出力 ---
void fat32_list_root_files(void) {
    uint32_t cluster = bpb.root_cluster;
    uint8_t buf[512];
    int loop_count = 0;
    const int MAX_LOOPS = 10000;

    serial_write_string("[FAT32] listing root files (full LFN support):\n");

    while(cluster >= 2 && cluster < 0x0FFFFFF8) {
        if(++loop_count > MAX_LOOPS) break;

        for(uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            uint32_t lba = cluster_to_lba(cluster);
            if(lba == 0) break;
            if(!disk_read(lba + sec, buf, 1)) break;

            char lfn_stack[256];  // LFN一時格納
            int lfn_len = 0;
            int lfn_order[MAX_LOOPS];  // エントリ順序
            int lfn_count = 0;

            for(int i = 0; i < 512; i += 32) {
                if(buf[i] == 0x00) break;   // 空エントリ
                if(buf[i] == 0xE5) { lfn_len = 0; lfn_count = 0; continue; }

                uint8_t attr = buf[i+11];

                if(attr == 0x0F) {
                    // LFNエントリ
                    uint8_t order = buf[i] & 0x3F;  // 順序番号
                    if(order > 20) continue;        // 異常チェック

                    // 文字を抽出（UTF-16 → ASCII簡易）
                    char tmp[13]; 
                    int k = 0;
                    // 1-5文字
                    for(int j=0; j<5; j++) {
                        uint16_t c = *(uint16_t*)&buf[i+1 + j*2];
                        if(c == 0x0000 || c == 0xFFFF) break;
                        tmp[k++] = (char)c;
                    }
                    // 6-11文字
                    for(int j=0; j<6; j++) {
                        uint16_t c = *(uint16_t*)&buf[i+14 + j*2];
                        if(c == 0x0000 || c == 0xFFFF) break;
                        tmp[k++] = (char)c;
                    }
                    // 12-13文字
                    for(int j=0; j<2; j++) {
                        uint16_t c = *(uint16_t*)&buf[i+28 + j*2];
                        if(c == 0x0000 || c == 0xFFFF) break;
                        tmp[k++] = (char)c;
                    }

                    // スタックに逆順で保存
                    int pos = (order-1)*13;
                    for(int m=0; m<k; m++) {
                        lfn_stack[pos + m] = tmp[m];
                    }
                    if(order > lfn_count) lfn_count = order;
                    continue;
                }

                // 短い8.3形式
                char short_name[12];
                memcpy(short_name, &buf[i], 11);
                short_name[11] = '\0';
                for(int j=0; j<11; j++) {
                    if(short_name[j] == ' ') { short_name[j] = '\0'; break; }
                }

                uint16_t high = *(uint16_t*)&buf[i+20];
                uint16_t low  = *(uint16_t*)&buf[i+26];
                uint32_t first_cluster = ((uint32_t)high << 16) | low;
                uint32_t size = *(uint32_t*)&buf[i+28];

                // 出力
                if(lfn_count > 0) {
                    // LFNを完成させる
                    lfn_stack[lfn_count*13] = '\0';
                    serial_write_string("  ");
                    serial_write_string(lfn_stack);
                    serial_write_string("\n");
                    lfn_count = 0;
                } else {
                    serial_write_string("  ");
                    serial_write_string(short_name);
                    serial_write_string("\n");
                }
            }
        }

        uint32_t next = fat_get_next_cluster(cluster);
        if(next >= 0x0FFFFFF8 || next < 2) break;
        cluster = next;
    }

    serial_write_string("[FAT32] end of root files\n");
}

bool fat32_read_file(FAT32_FILE *file, uint8_t *buffer) {
    // NULLチェック
    if(file == NULL || buffer == NULL) return false;
    if(file->size == 0) return true;  // サイズ0のファイルは成功
    
    uint32_t cluster = file->first_cluster;
    uint32_t bytes_left = file->size;
    uint32_t bytes_per_cluster = bpb.bytes_per_sector * bpb.sectors_per_cluster;
    uint8_t buf[512];
    int loop_count = 0;  // 無限ループ防止
    const int MAX_LOOPS = 10000;

    while(cluster >= 2 && cluster < 0x0FFFFFF8 && bytes_left > 0) {
        if(++loop_count > MAX_LOOPS) return false;  // 無限ループ防止
        
        for(uint8_t sec = 0; sec < bpb.sectors_per_cluster && bytes_left > 0; sec++) {
            uint32_t lba = cluster_to_lba(cluster);
            if(lba == 0) return false;  // 無効なLBA
            
            if(!disk_read(lba + sec, buf, 1)) return false;
            
            uint32_t to_copy = (bytes_left > bpb.bytes_per_sector) ? 
                               bpb.bytes_per_sector : bytes_left;
            memcpy(buffer, buf, to_copy);
            buffer += to_copy;
            bytes_left -= to_copy;
        }
        
        uint32_t next = fat_get_next_cluster(cluster);
        if(next >= 0x0FFFFFF8 || next < 2) break;
        cluster = next;
    }
    return bytes_left == 0;
}


// --- ファイル書き込み(既存クラスタ上書き) ---
bool fat32_write_file(FAT32_FILE *file, const uint8_t *buffer) {
    // NULLチェック
    if(file == NULL || buffer == NULL) return false;
    if(file->size == 0) return true;  // サイズ0のファイルは成功
    
    uint32_t cluster = file->first_cluster;
    uint32_t bytes_left = file->size;
    uint32_t bytes_per_cluster = bpb.bytes_per_sector * bpb.sectors_per_cluster;
    int loop_count = 0;  // 無限ループ防止
    const int MAX_LOOPS = 10000;

    while(cluster >= 2 && cluster < 0x0FFFFFF8 && bytes_left > 0) {
        if(++loop_count > MAX_LOOPS) return false;  // 無限ループ防止
        
        uint32_t to_write = (bytes_left > bytes_per_cluster) ? 
                            bytes_per_cluster : bytes_left;
        
        for(uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            if(bytes_left == 0) return true;  // 書き込み完了
            
            uint32_t lba = cluster_to_lba(cluster);
            if(lba == 0) return false;  // 無効なLBA
            
            uint32_t to_write_sector = (bytes_left > bpb.bytes_per_sector) ? 
                                       bpb.bytes_per_sector : bytes_left;
            
            // セクタ全体を書き込む必要がある場合は直接書き込み
            // 部分書き込みの場合は読み込んでから書き込み
            if(to_write_sector == bpb.bytes_per_sector) {
                if(!disk_write(lba + sec, buffer, 1)) return false;
            } else {
                uint8_t temp_buf[512];
                if(!disk_read(lba + sec, temp_buf, 1)) return false;
                memcpy(temp_buf, buffer, to_write_sector);
                if(!disk_write(lba + sec, temp_buf, 1)) return false;
            }
            
            buffer += to_write_sector;
            bytes_left -= to_write_sector;
        }
        
        uint32_t next = fat_get_next_cluster(cluster);
        if(next >= 0x0FFFFFF8 || next < 2) break;
        cluster = next;
    }
    return bytes_left == 0;
}