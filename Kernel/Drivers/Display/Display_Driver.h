#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;
    bool (*probe)(void);
    bool (*init)(void);
    bool (*is_ready)(void);
    uint32_t (*width)(void);
    uint32_t (*height)(void);
    void (*draw_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*present)(void);
} display_driver_t;
