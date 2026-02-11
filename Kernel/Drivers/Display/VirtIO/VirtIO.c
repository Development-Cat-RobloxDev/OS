#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "../../../Memory/Other_Utils.h"
#include "../../../Serial.h"
#include "../../../IO/IO_Main.h"
#include "../../PCI/PCI_Main.h"

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_GPU_DEVICE_ID 0x1050

#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0100
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_FLAG_FENCE 0x1

typedef struct {
    uint32_t base_addr;
    uint32_t irq;
} virtio_gpu_pci_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} virtgpu_resource_create_2d;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
} virtgpu_transfer_2d;

extern uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);

/*
 * Minimal virtqueue helpers:
 * keep GPU command flow buildable until full MMIO virtqueue implementation is added.
 */
static void *init_virtqueue_mmio(void *base_addr) {
    if (!base_addr) {
        return NULL;
    }
    return base_addr;
}

static void virtqueue_add_buffer(void *vq, const void *buf, uint32_t len) {
    (void)vq;
    (void)buf;
    (void)len;
}

static uint64_t fence_counter = 1;
static uint32_t resource_counter = 1;

void serial_write_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 28; i >= 0; i -= 4) {
        char c = hex[(val >> i) & 0xF];
        char s[2] = {c, 0};
        serial_write_string(s);
    }
}

void serial_write_dec(uint32_t val) {
    char buf[11];
    int i = 10;
    buf[i] = 0;
    if (val == 0) { serial_write_string("0"); return; }
    while (val > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    serial_write_string(&buf[i]);
}

int find_virtio_gpu(virtio_gpu_pci_t *gpu) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor_id = pci_read_config(bus, device, func, 0x00) & 0xFFFF;
                uint16_t device_id = (pci_read_config(bus, device, func, 0x00) >> 16) & 0xFFFF;

                if (vendor_id == VIRTIO_VENDOR_ID && device_id == VIRTIO_GPU_DEVICE_ID) {
                    uint32_t bar0 = pci_read_config(bus, device, func, 0x10);
                    gpu->base_addr = bar0 & ~0xF;
                    gpu->irq = pci_read_config(bus, device, func, 0x3C) & 0xFF;
                    return 1;
                }

                uint32_t header_type = pci_read_config(bus, device, func, 0x0C);
                if (func == 0 && ((header_type >> 16) & 0x80) == 0) break;
            }
        }
    }
    return 0;
}

uint32_t virtgpu_create_2d(void *vq, uint32_t width, uint32_t height) {
    virtgpu_resource_create_2d cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.flags = VIRTIO_GPU_FLAG_FENCE;
    cmd.fence_id = fence_counter++;
    cmd.resource_id = resource_counter++;
    cmd.format = 1;
    cmd.width = width;
    cmd.height = height;

    virtqueue_add_buffer(vq, &cmd, sizeof(cmd));
    return cmd.resource_id;
}

void virtgpu_fill_screen(void *vq, uint32_t resource_id, uint32_t width, uint32_t height, uint32_t color) {
    uint32_t *buf = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (!buf) return;

    for (uint32_t i = 0; i < width * height; i++) {
        buf[i] = color;
    }

    virtqueue_add_buffer(vq, buf, width * height * sizeof(uint32_t));

    virtgpu_transfer_2d cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.flags = VIRTIO_GPU_FLAG_FENCE;
    cmd.fence_id = fence_counter++;
    cmd.resource_id = resource_id;
    cmd.width = width;
    cmd.height = height;

    virtqueue_add_buffer(vq, &cmd, sizeof(cmd));
    free(buf);
}

void *virtio_init_gpu() {
    virtio_gpu_pci_t gpu;
    if (!find_virtio_gpu(&gpu)) {
        serial_write_string("VIRTIO GPU not found!\n");
        return NULL;
    }

    serial_write_string("VIRTIO GPU found at ");
    serial_write_hex(gpu.base_addr);
    serial_write_string(" IRQ ");
    serial_write_dec(gpu.irq);
    serial_write_string("\n");

    void *cmd_vq = init_virtqueue_mmio((void *)(uintptr_t)gpu.base_addr);
    if (!cmd_vq) {
        serial_write_string("Failed to init virtqueue\n");
        return NULL;
    }

    uint32_t res_id = virtgpu_create_2d(cmd_vq, 1024, 768);
    serial_write_string("Created 2D resource ID ");
    serial_write_dec(res_id);
    serial_write_string("\n");

    virtgpu_fill_screen(cmd_vq, res_id, 1024, 768, 0xFF0000FF);

    return cmd_vq;
}
