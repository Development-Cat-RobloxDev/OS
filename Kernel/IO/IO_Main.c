#include <stdint.h>
#include <stdbool.h>

#define ATA_DATA     0x1F0
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA0     0x1F3
#define ATA_LBA1     0x1F4
#define ATA_LBA2     0x1F5
#define ATA_HDDEVSEL 0x1F6
#define ATA_COMMAND  0x1F7
#define ATA_STATUS   0x1F7
#define ATA_CONTROL  0x3F6
#define FAT32_START_LBA 2048      // Example start LBA for FAT32 partition
#define ATA_CMD_READ    0x20      // Read sectors command
#define ATA_SR_BSY      0x80      // Busy
#define ATA_SR_DRQ      0x08      // Data request ready
#define ATA_SR_ERR      0x01      // Error

void outb(uint16_t port, uint8_t val){
    __asm__ volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port){
    uint8_t val;
    __asm__ volatile("inb %1,%0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outsw(uint16_t port, const void* addr, int count){
    __asm__ volatile(
        "rep outsw"
        : "+S"(addr), "+c"(count)   // addr (SI/RSI) と count (CX/RCX) は読み書き
        : "d"(port)                 // port (DX) は入力
        : "memory"
    );
}

static inline void insw(uint16_t port, void* addr, int count){
    __asm__ volatile(
        "rep insw"
        : "+D"(addr), "+c"(count)
        : "d"(port)
        : "memory"
    );
}


bool disk_read(uint32_t lba, uint8_t *buffer, uint32_t sectors){
    uint32_t real_lba = FAT32_START_LBA + lba;

    outb(ATA_HDDEVSEL, 0xE0 | ((real_lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, real_lba & 0xFF);
    outb(ATA_LBA1, (real_lba >> 8) & 0xFF);
    outb(ATA_LBA2, (real_lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ);

    // BSY解除待ち
    while (inb(ATA_STATUS) & ATA_SR_BSY);

    // DRQ待ち
    uint8_t status;
    do {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return false;
    } while (!(status & ATA_SR_DRQ));

    // 512 bytes 読み込み
    insw(ATA_DATA, buffer, 256);
    return true;
}

bool disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors){
    outb(ATA_HDDEVSEL, 0xE0 | ((lba>>24)&0x0F));
    outb(ATA_SECCOUNT, sectors);
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba>>8) & 0xFF);
    outb(ATA_LBA2, (lba>>16) & 0xFF);
    outb(ATA_COMMAND, 0x30); // WRITE SECTORS

    for(uint32_t s=0; s<sectors; s++){
        while(inb(ATA_STATUS) & 0x80);
        while(!(inb(ATA_STATUS) & 0x08));
        outsw(ATA_DATA, buffer, 256);
        buffer += 512;
    }
    return true;
}
