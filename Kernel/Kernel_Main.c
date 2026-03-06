#include "Kernel_Main.h"
#include "Memory/Memory_Main.h"
#include "Paging/Paging_Main.h"
#include "SMP/SMP_Main.h"
#include "IDT/IDT_Main.h"
#include "GDT/GDT_Main.h"
#include "IO/IO_Main.h"
#include "Drivers/FileSystem/FAT32/FAT32_Main.h"
#include "Drivers/DriverModule.h"
#include "Drivers/DriverSelect.h"
#include "Drivers/Display/Display_Main.h"
#include "Drivers/PS2/PS2_Input.h"
#include "ELF/ELF_Loader.h"
#include "Syscall/Syscall_Main.h"
#include "Syscall/Syscall_File.h"
#include "ProcessManager/ProcessManager.h"
#include "WindowManager/WindowManager.h"
#include "Serial.h"
#include "BMP.h"
#include "Sync/Spinlock.h"

#include <stdbool.h>

#define COM1_PORT 0x3F8

#define USER_ELF_MAX_SIZE (2ULL * 1024ULL * 1024ULL)

static uint64_t user_entry = 0;
static FAT32_FILE log_file;
static bool log_file_ready = false;
static uint32_t log_offset = 0;

#define LOG_RING_BUFFER_SIZE 32768
static char log_ring_buffer[LOG_RING_BUFFER_SIZE];
static uint32_t log_ring_head = 0;
static uint32_t log_ring_tail = 0;
static spinlock_t log_lock;

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_write_char(char c) {
    uint32_t timeout = 0x100000u;
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {
        if (--timeout == 0) {
            return;
        }
    }
    outb(COM1_PORT, c);
}

void log_write(const char* str) {
    const char* p = str;
    while (*p) {
        log_ring_buffer[log_ring_head] = *p;
        log_ring_head = (log_ring_head + 1) % LOG_RING_BUFFER_SIZE;
        if (log_ring_head == log_ring_tail) {
            log_ring_tail = (log_ring_tail + 1) % LOG_RING_BUFFER_SIZE;
        }
        p++;
    }

    if (log_file_ready) {
        while (log_ring_tail != log_ring_head) {
            uint8_t c = (uint8_t)log_ring_buffer[log_ring_tail];
            fat32_write_at(&log_file, log_offset, &c, 1);
            log_offset++;
            log_ring_tail = (log_ring_tail + 1) % LOG_RING_BUFFER_SIZE;
        }
    }
}

void serial_write_string(const char* str) {
    spinlock_lock(&log_lock);
    const char* p = str;

    while (*p) {
        if (*p == '\n')
            serial_write_char('\r');

        serial_write_char(*p);
        p++;
    }

    //log_write(str);
    spinlock_unlock(&log_lock);
}

void serial_write_uint64(uint64_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint32(uint32_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint16(uint16_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 12; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint8(uint8_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    serial_write_char(hex[(value >> 4) & 0xF]);
    serial_write_char(hex[value & 0xF]);
}

void serial_write_dec16(uint16_t v) {
    char buf[7];
    int i = 6;
    buf[--i] = '\0';
    if (v == 0) {
        buf[--i] = '0';
    } else {
        while (v > 0 && i > 0) {
            buf[--i] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    serial_write_string(&buf[i]);
}

void all_fs_initialize() {
    fat32_init();
}

static bool load_userland_elf(uint64_t *entry_out) {
    elf_load_policy_t policy = {
        .max_file_size = USER_ELF_MAX_SIZE,
        .min_vaddr = USER_CODE_BASE,
        .max_vaddr = USER_CODE_LIMIT,
    };
    return elf_loader_load_from_path("Userland/Userland.ELF", &policy, entry_out);
}

__attribute__((noreturn))
void entry_user_mode() {
    uint64_t user_rip = user_entry;
    uint64_t user_rsp = process_get_current_user_rsp() - 128;
    uint64_t user_cr3 = process_get_current_cr3();
    
    paging_switch_cr3(user_cr3);

    uint64_t user_ss = GDT_USER_DATA | 3;
    uint64_t user_cs = GDT_USER_CODE | 3;
    uint64_t rflags  = 0x202;

    __asm__ volatile (
        "cli\n"
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"

        "pushq %1\n"
        "pushq %2\n"
        "pushq %3\n"
        "pushq %4\n"
        "pushq %5\n"
        "iretq\n"
        :
        : "r"((uint16_t)user_ss), "r"(user_ss), "r"(user_rsp), "r"(rflags), "r"(user_cs), "r"(user_rip)
        : "rax", "memory"
    );
    __builtin_unreachable();
}

void log_init(void) {
    spinlock_init(&log_lock);
    fat32_mkdir("BootUpLog");
    fat32_creat("BootUpLog/BUL.txt");

    if (fat32_find_file("BootUpLog/BUL.txt", &log_file)) {
        fat32_truncate(&log_file, 0);
        log_offset = 0;
        log_file_ready = true;
        
        spinlock_lock(&log_lock);
        log_write("");
        spinlock_unlock(&log_lock);
    }
}

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info) {
    __asm__ volatile ("cli");
    serial_init();
    serial_write_string("\n[OS] ===== Kernel Starting =====\n");
    
    serial_write_string("[OS] Initializing physical memory...\n");
    init_physical_memory(
        boot_info->MemoryMap,
        boot_info->MemoryMapSize,
        boot_info->MemoryMapDescriptorSize
    );

    serial_write_string("[OS] Initializing paging...\n");
    init_paging();

    serial_write_string("[OS] Initializing SMP...\n");
    smp_init();

    serial_write_string("[OS] Initializing memory manager...\n");
    memory_init();

    serial_write_string("[OS] Initializing IDT...\n");
    init_idt();
    
    serial_write_string("[OS] Initializing GDT...\n");
    init_gdt();

    driver_module_manager_init(boot_info);
    display_boot_framebuffer_t boot_fb = {
        .addr = (void *)(uintptr_t)boot_info->FrameBufferBase,
        .size_bytes = boot_info->FrameBufferSize,
        .width = boot_info->HorizontalResolution,
        .height = boot_info->VerticalResolution,
        .pixels_per_scan_line = boot_info->PixelsPerScanLine,
        .bytes_per_pixel = 4,
    };
    driver_select_set_boot_framebuffer(&boot_fb);

    __asm__ volatile ("sti");

    serial_write_string("[OS] Initializing file system...\n");
    all_fs_initialize();
    log_init();

    bool display_ready = false;
    bool ps2_ready = false;
    bool window_manager_ready = false;

    serial_write_string("[OS] Initializing display...\n");
    display_ready = display_init();
    if (!display_ready) {
        serial_write_string("[OS] [WARN] Display unavailable. Running in headless mode.\n");
    }

    display_fill_rect((uint32_t)0,(uint32_t)0,(uint32_t)display_width(),(uint32_t)display_height(),0x00000000);

    void* bmp_data;
    uint32_t bmp_size;

    if (load_bmp("os_logo.bmp", &bmp_data, &bmp_size)) {
        draw_bmp_center_ex(bmp_data,0x000000);
        serial_write_string("[OS] BMP displayed\n");
    } else {
        serial_write_string("[OS] Failed to load BMP\n");
    }

    if (display_ready) {
        serial_write_string("[OS] Initializing window manager...\n");
        window_manager_ready = window_manager_init();
        if (!window_manager_ready) {
            serial_write_string("[OS] [WARN] Window manager init failed. GUI syscalls disabled.\n");
        }
    } else {
        serial_write_string("[OS] [WARN] Window manager skipped (display dependency not ready).\n");
    }

    serial_write_string("[OS] Initializing PS/2 input...\n");
    ps2_ready = ps2_input_init();
    if (!ps2_ready) {
        serial_write_string("[OS] [WARN] PS/2 input unavailable. Input syscalls will be idle.\n");
    }
    
    serial_write_string("[OS] Initializing syscall...\n");
    syscall_init(); 

    serial_write_string("[OS] Initializing process manager...\n");
    process_manager_init();

    syscall_file_init();

    serial_write_string("[OS] Loading userland ELF...\n");
    if (!load_userland_elf(&user_entry)) {
        serial_write_string("[OS] [ERROR] Failed to load userland ELF\n");
        serial_write_string("[OS] [ERROR] System halted\n");
        while (1) {
            __asm__("hlt");
        }
    }
    
    serial_write_string("[OS] ===== Kernel Init Complete =====\n");
    serial_write_string("[OS] Transferring control to userland...\n\n");

    if (process_register_boot_process(user_entry, 0) < 0) {
        serial_write_string("[OS] [ERROR] Failed to register boot process\n");
        while (1) {
            __asm__("hlt");
        }
    }
    
    entry_user_mode();
    __builtin_unreachable();
}
