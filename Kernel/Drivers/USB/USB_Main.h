#pragma once

#include <stdbool.h>
#include "XHCI_USB.h"

bool usb_main_init(void);

const usb_driver_t *usb_get_driver(void);

bool    usb_is_ready(void);
void    usb_poll(void);
uint8_t usb_device_count(void);