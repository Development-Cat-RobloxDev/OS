#include "USB_Main.h"
#include "../DriverBinary.h"
#include "../../ELF/ELF_Loader.h"
#include "../../Serial.h"
#include "../../Memory/Memory_Main.h"
#include "../../Paging/Paging_Main.h"
#include "../PCI/PCI_Main.h"
#include "../../Memory/DMA_Memory.h"
#include "../../Memory/Other_Utils.h"

#include <stddef.h>
#include <stdint.h>

#define USB_DRIVER_MODULE_PATH    "Kernel/Driver/XHCI_USB.ELF"
#define USB_DRIVER_MODULE_MAX_SIZE (2ULL * 1024ULL * 1024ULL)
#define USB_DRIVER_VADDR_MIN       0x00C00000ULL
#define USB_DRIVER_VADDR_MAX       0x01000000ULL

static const usb_driver_t *g_usb_driver = NULL;

static const driver_kernel_api_t g_driver_api = {
    .serial_write_string = serial_write_string,
    .serial_write_uint32 = serial_write_uint32,
    .serial_write_uint64 = serial_write_uint64,
    .kmalloc             = kmalloc,
    .kfree               = kfree,
    .dma_alloc           = dma_alloc,
    .dma_free            = dma_free,
    .virt_to_phys        = virt_to_phys,
    .memset              = memset,
    .memcpy              = memcpy,
    .pci_read_config     = pci_read_config,
    .pci_write_config    = pci_write_config,
    .map_mmio_virt       = map_mmio_virt,
};

bool usb_main_init(void)
{
    elf_load_policy_t policy = {
        .max_file_size = USB_DRIVER_MODULE_MAX_SIZE,
        .min_vaddr     = USB_DRIVER_VADDR_MIN,
        .max_vaddr     = USB_DRIVER_VADDR_MAX,
    };
    uint64_t entry = 0;

    serial_write_string("[OS] [USB] Loading xHCI module: ");
    serial_write_string(USB_DRIVER_MODULE_PATH);
    serial_write_string("\n");

    if (!elf_loader_load_from_path(USB_DRIVER_MODULE_PATH, &policy, &entry)) {
        serial_write_string("[OS] [USB] Failed to load xHCI ELF module\n");
        return false;
    }

    usb_driver_module_init_t init_fn = (usb_driver_module_init_t)(uintptr_t)entry;
    const usb_driver_t *driver = init_fn(&g_driver_api);

    if (!driver) {
        serial_write_string("[OS] [USB] xHCI module init returned NULL\n");
        return false;
    }

    if (!driver->init || !driver->init()) {
        serial_write_string("[OS] [USB] xHCI hardware init failed\n");
        return false;
    }

    g_usb_driver = driver;

    serial_write_string("[OS] [USB] xHCI driver ready: ");
    if (driver->name) {
        serial_write_string(driver->name);
    }
    serial_write_string("\n");

    return true;
}

const usb_driver_t *usb_get_driver(void)
{
    return g_usb_driver;
}

bool usb_is_ready(void)
{
    return g_usb_driver != NULL &&
           g_usb_driver->is_ready != NULL &&
           g_usb_driver->is_ready();
}

void usb_poll(void)
{
    if (g_usb_driver && g_usb_driver->poll) {
        g_usb_driver->poll();
    }
}

uint8_t usb_device_count(void)
{
    if (g_usb_driver && g_usb_driver->device_count) {
        return g_usb_driver->device_count();
    }
    return 0;
}