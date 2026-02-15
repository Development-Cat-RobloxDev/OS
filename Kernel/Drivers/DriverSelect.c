#include <stddef.h>
#include <stdint.h>

#include "DriverSelect.h"
#include "DriverBinary.h"
#include "../ELF/ELF_Loader.h"
#include "../Memory/Memory_Main.h"
#include "../Paging/Paging_Main.h"
#include "PCI/PCI_Main.h"
#include "../Serial.h"

#define MAX_DISPLAY_DRIVERS 8
#define DISPLAY_DRIVER_MODULE_MAX_SIZE (2ULL * 1024ULL * 1024ULL)
#define DISPLAY_DRIVER_VADDR_MIN 0x00800000ULL
#define DISPLAY_DRIVER_VADDR_MAX 0x02000000ULL
#define VIRTIO_DRIVER_MODULE_PATH "Kernel/Driver/VirtIO_Driver.ELF"
#define INTEL_DRIVER_MODULE_PATH  "Kernel/Driver/Intel_UHD_Graphics_9TH_Driver.ELF"

static const display_driver_t *g_display_drivers[MAX_DISPLAY_DRIVERS];
static uint32_t g_display_driver_count = 0;
static int g_display_binary_registered = 0;

static const driver_kernel_api_t g_driver_api = {
    .serial_write_string = serial_write_string,
    .serial_write_uint32 = serial_write_uint32,
    .serial_write_uint64 = serial_write_uint64,
    .kmalloc = kmalloc,
    .kfree = kfree,
    .pci_read_config = pci_read_config,
    .pci_write_config = pci_write_config,
    .map_mmio_virt = map_mmio_virt,
};

bool driver_select_register_display_driver(const display_driver_t *driver) {
    if (!driver || !driver->name || !driver->init || !driver->is_ready ||
        !driver->width || !driver->height || !driver->draw_pixel ||
        !driver->fill_rect || !driver->present) {
        return false;
    }

    for (uint32_t i = 0; i < g_display_driver_count; ++i) {
        if (g_display_drivers[i] == driver) {
            return true;
        }
    }

    if (g_display_driver_count >= MAX_DISPLAY_DRIVERS) {
        serial_write_string("[OS] [DRIVER] Display driver registry is full\n");
        return false;
    }

    g_display_drivers[g_display_driver_count++] = driver;
    return true;
}

static void load_display_driver_module(const char *path) {
    elf_load_policy_t policy = {
        .max_file_size = DISPLAY_DRIVER_MODULE_MAX_SIZE,
        .min_vaddr = DISPLAY_DRIVER_VADDR_MIN,
        .max_vaddr = DISPLAY_DRIVER_VADDR_MAX,
    };
    uint64_t entry = 0;

    if (!elf_loader_load_from_path(path, &policy, &entry)) {
        serial_write_string("[OS] [DRIVER] Failed to load module: ");
        serial_write_string(path);
        serial_write_string("\n");
        return;
    }

    display_driver_module_init_t init_fn =
        (display_driver_module_init_t)(uintptr_t)entry;
    const display_driver_t *driver = init_fn(&g_driver_api);

    if (!driver) {
        serial_write_string("[OS] [DRIVER] Module init failed: ");
        serial_write_string(path);
        serial_write_string("\n");
        return;
    }

    if (!driver_select_register_display_driver(driver)) {
        serial_write_string("[OS] [DRIVER] Module register failed: ");
        serial_write_string(path);
        serial_write_string("\n");
        return;
    }

    serial_write_string("[OS] [DRIVER] Module loaded: ");
    serial_write_string(path);
    serial_write_string("\n");
}

void driver_select_register_binary_display_drivers(void) {
    if (g_display_binary_registered) {
        return;
    }

    g_display_binary_registered = 1;
    load_display_driver_module(VIRTIO_DRIVER_MODULE_PATH);
    load_display_driver_module(INTEL_DRIVER_MODULE_PATH);
}

const display_driver_t *driver_select_pick_display_driver(void) {
    for (uint32_t i = 0; i < g_display_driver_count; ++i) {
        const display_driver_t *driver = g_display_drivers[i];
        if (!driver) {
            continue;
        }

        if (!driver->probe || driver->probe()) {
            serial_write_string("[OS] [DRIVER] Display driver selected: ");
            serial_write_string(driver->name);
            serial_write_string("\n");
            return driver;
        }
    }

    serial_write_string("[OS] [DRIVER] No display driver matched connected devices\n");
    return NULL;
}
