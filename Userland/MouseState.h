#pragma once

#include <stdint.h>

typedef struct {
    int32_t  x;
    int32_t  y;
    int32_t  dx;
    int32_t  dy;
    int32_t  wheel;
    uint8_t  buttons;
    uint8_t  pressed;
    uint8_t  released;
    uint8_t  valid;
    uint8_t  _pad[8];
} user_mouse_state_t;

#define MOUSE_BTN_LEFT   (1u << 0)
#define MOUSE_BTN_RIGHT  (1u << 1)
#define MOUSE_BTN_MIDDLE (1u << 2)