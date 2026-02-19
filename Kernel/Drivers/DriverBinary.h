#pragma once

#include <stdint.h>
#include <stddef.h>

#include "Display/Display_Driver.h"

typedef struct {
    void (*serial_write_string)(const char *str);
    void (*serial_write_uint32)(uint32_t value);
    void (*serial_write_uint64)(uint64_t value);

    void *(*kmalloc)(uint32_t size);
    void (*kfree)(void *ptr);

    void *(*dma_alloc)(uint32_t size, uint64_t *phys_out);
    void  (*dma_free)(void *ptr, uint32_t size);
    uint64_t (*virt_to_phys)(void *virt);

    void *(*memset)(void *s, int c, size_t n);
    void *(*memcpy)(void *dst, const void *src, size_t n);

    uint32_t (*pci_read_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
    void (*pci_write_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);

    void *(*map_mmio_virt)(uint64_t phys_addr);
} driver_kernel_api_t;

typedef const display_driver_t *(*display_driver_module_init_t)(const driver_kernel_api_t *api);