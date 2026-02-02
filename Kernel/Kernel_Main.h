#ifndef KERNEL_MAIN_H
#define KERNEL_MAIN_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t UINTN;
typedef uint32_t UINT32;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    EFI_MEMORY_DESCRIPTOR *MemoryMap;
    UINTN MemoryMapSize;
    UINTN MemoryMapDescriptorSize;
    UINT32 MemoryMapDescriptorVersion;

    uint64_t FrameBufferBase;
    uint32_t FrameBufferSize;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelsPerScanLine;
} BOOT_INFO;

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info);
void serial_write_string(const char* str);
void serial_write_uint64(uint64_t value);
void serial_write_uint32(uint32_t value);
void serial_write_uint16(uint16_t value);

#endif
