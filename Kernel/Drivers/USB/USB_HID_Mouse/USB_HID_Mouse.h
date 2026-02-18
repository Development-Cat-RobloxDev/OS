#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_BTN_LEFT    (1u << 0)
#define MOUSE_BTN_RIGHT   (1u << 1)
#define MOUSE_BTN_MIDDLE  (1u << 2)

typedef struct {
    int32_t  x;
    int32_t  y;
    int32_t  dx;
    int32_t  dy;
    int32_t  wheel;
    uint8_t  buttons;
    uint8_t  pressed;
    uint8_t  released;
    bool     valid;
} mouse_state_t;

void hid_mouse_init(void);
void hid_mouse_poll(void);
bool hid_mouse_is_ready(void);
void hid_mouse_get_state(mouse_state_t *out);
void hid_mouse_set_bounds(int32_t min_x, int32_t min_y,
                          int32_t max_x, int32_t max_y);
void hid_mouse_set_position(int32_t x, int32_t y);