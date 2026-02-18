#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *name;
    bool     (*probe)(void);
    bool     (*init)(void);
    bool     (*is_ready)(void);
    void     (*poll)(void);
    uint8_t  (*device_count)(void);
    bool     (*get_device_info)(uint8_t slot_id, uint16_t *vid, uint16_t *pid,
                                uint8_t *class_code, uint8_t *subclass);
    bool     (*msc_read)(uint8_t slot_id, uint32_t lba, uint8_t sectors,
                         void *buf, uint32_t buf_size);
    int32_t  (*hid_read)(uint8_t slot_id, void *buf, uint32_t len);
    uint8_t  (*get_max_ports)(void);
    uint8_t  (*get_max_slots)(void);
} usb_driver_t;

bool usb_xhci_init(void);
bool usb_xhci_is_ready(void);
void usb_poll(void);
void usb_enumerate_devices(void);
uint8_t usb_device_count(void);
uint8_t usb_get_max_ports(void);
uint8_t usb_get_max_slots(void);
bool usb_get_device_info(uint8_t slot_id,
                          uint16_t *vid, uint16_t *pid,
                          uint8_t  *class_code, uint8_t *subclass);
int32_t usb_hid_read(uint8_t slot_id, void *buf, uint32_t len);
bool usb_msc_read(uint8_t slot_id, uint32_t lba, uint8_t sectors,
                  void *buf, uint32_t buf_size);

#define USB_CLASS_HID           0x03
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09

#define USB_SPEED_FULL          1
#define USB_SPEED_LOW           2
#define USB_SPEED_HIGH          3
#define USB_SPEED_SUPER         4
#define USB_SPEED_SUPER_PLUS    5