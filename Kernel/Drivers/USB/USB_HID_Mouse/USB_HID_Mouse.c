#include "USB_HID_Mouse.h"
#include "../USB_Main.h"
#include "../XHCI_USB.h"
#include "../../../Serial.h"

#include <stddef.h>
#include <stdint.h>

#define HID_MOUSE_MAX_SLOTS   4 
#define HID_REPORT_BUF_SIZE   8
typedef struct {
    bool    active;
    uint8_t slot_id;
} mouse_slot_t;

static mouse_slot_t g_mice[HID_MOUSE_MAX_SLOTS];
static uint8_t      g_mouse_count    = 0;
static bool         g_initialized    = false;

static mouse_state_t g_state = {
    .x        = 0,
    .y        = 0,
    .dx       = 0,
    .dy       = 0,
    .wheel    = 0,
    .buttons  = 0,
    .pressed  = 0,
    .released = 0,
    .valid    = false,
};

static bool    g_bounds_enabled = false;
static int32_t g_min_x = -0x7FFFFFFF;
static int32_t g_max_x =  0x7FFFFFFF;
static int32_t g_min_y = -0x7FFFFFFF;
static int32_t g_max_y =  0x7FFFFFFF;

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void process_report(const uint8_t *buf, int32_t len)
{
    if (len < 3) {
        return;
    }

    uint8_t new_buttons = buf[0] & 0x07u;
    int8_t  raw_dx      = (int8_t)buf[1];
    int8_t  raw_dy      = (int8_t)buf[2];
    int8_t  raw_wheel   = (len >= 4) ? (int8_t)buf[3] : 0;

    uint8_t prev_buttons = g_state.buttons;

    g_state.dx       = (int32_t)raw_dx;
    g_state.dy       = (int32_t)raw_dy;
    g_state.x       += (int32_t)raw_dx;
    g_state.y       += (int32_t)raw_dy;
    g_state.wheel   += (int32_t)raw_wheel;
    g_state.buttons  = new_buttons;
    g_state.pressed  = (uint8_t)(new_buttons & ~prev_buttons);
    g_state.released = (uint8_t)(prev_buttons & ~new_buttons);
    g_state.valid    = true;

    if (g_bounds_enabled) {
        g_state.x = clamp_i32(g_state.x, g_min_x, g_max_x);
        g_state.y = clamp_i32(g_state.y, g_min_y, g_max_y);
    }
}

void hid_mouse_init(void)
{
    const usb_driver_t *usb = usb_get_driver();
    if (!usb || !usb->is_ready || !usb->is_ready()) {
        serial_write_string("[OS] [MOUSE] USB driver not ready, mouse init skipped\n");
        return;
    }

    g_mouse_count = 0;
    for (uint8_t i = 0; i < HID_MOUSE_MAX_SLOTS; ++i) {
        g_mice[i].active  = false;
        g_mice[i].slot_id = 0;
    }

    uint8_t max_slots = usb->get_max_slots ? usb->get_max_slots() : 64;

    for (uint8_t slot = 1; slot <= max_slots && g_mouse_count < HID_MOUSE_MAX_SLOTS; ++slot) {
        uint16_t vid        = 0;
        uint16_t pid        = 0;
        uint8_t  class_code = 0;
        uint8_t  subclass   = 0;

        if (!usb->get_device_info(slot, &vid, &pid, &class_code, &subclass)) {
            continue;
        }

        if (class_code != USB_CLASS_HID) {
            continue;
        }

        g_mice[g_mouse_count].active  = true;
        g_mice[g_mouse_count].slot_id = slot;
        g_mouse_count++;

        serial_write_string("[OS] [MOUSE] HID device bound: slot=");
        serial_write_uint32(slot);
        serial_write_string(" VID=");
        serial_write_uint16(vid);
        serial_write_string(" PID=");
        serial_write_uint16(pid);
        serial_write_string("\n");
    }

    if (g_mouse_count == 0) {
        serial_write_string("[OS] [MOUSE] No HID mouse found\n");
    } else {
        serial_write_string("[OS] [MOUSE] Init complete, mice=");
        serial_write_uint32(g_mouse_count);
        serial_write_string("\n");
    }

    g_initialized = true;
}

void hid_mouse_poll(void)
{
    if (!g_initialized || g_mouse_count == 0) {
        return;
    }

    const usb_driver_t *usb = usb_get_driver();
    if (!usb || !usb->hid_read) {
        return;
    }

    g_state.pressed  = 0;
    g_state.released = 0;
    g_state.dx       = 0;
    g_state.dy       = 0;

    for (uint8_t i = 0; i < g_mouse_count; ++i) {
        if (!g_mice[i].active) {
            continue;
        }

        uint8_t buf[HID_REPORT_BUF_SIZE] = {0};
        int32_t n = usb->hid_read(g_mice[i].slot_id, buf, sizeof(buf));

        if (n >= 3) {
            process_report(buf, n);
        } else if (n < 0) {
            serial_write_string("[OS] [MOUSE] slot=");
            serial_write_uint32(g_mice[i].slot_id);
            serial_write_string(" hid_read error, deactivating\n");
            g_mice[i].active = false;
        }
    }
}

bool hid_mouse_is_ready(void)
{
    return g_initialized && g_state.valid;
}

void hid_mouse_get_state(mouse_state_t *out)
{
    if (!out) {
        return;
    }
    *out = g_state;
}

void hid_mouse_set_bounds(int32_t min_x, int32_t min_y,
                          int32_t max_x, int32_t max_y)
{
    if (min_x >= max_x || min_y >= max_y) {
        g_bounds_enabled = false;
        return;
    }
    g_min_x = min_x;
    g_max_x = max_x;
    g_min_y = min_y;
    g_max_y = max_y;
    g_bounds_enabled = true;

    g_state.x = clamp_i32(g_state.x, g_min_x, g_max_x);
    g_state.y = clamp_i32(g_state.y, g_min_y, g_max_y);
}

void hid_mouse_set_position(int32_t x, int32_t y)
{
    g_state.x = g_bounds_enabled ? clamp_i32(x, g_min_x, g_max_x) : x;
    g_state.y = g_bounds_enabled ? clamp_i32(y, g_min_y, g_max_y) : y;
}