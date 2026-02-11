#include <stdint.h>
#include "../../IO/IO_Main.h"
#include "../../Serial.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (device << 11) |
                       (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (device << 11) |
                       (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar[6];
} pci_device_t;

void pci_read_bars(pci_device_t *dev) {
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_config(dev->bus, dev->device, dev->func, 0x10 + i * 4);
    }
}

void pci_scan_bus_serial(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config(bus, device, func, 0x00);
                uint16_t vendor_id = vendor_device & 0xFFFF;
                uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

                if (vendor_id == 0xFFFF) {
                    if (func == 0) break;
                    else continue;
                }

                uint32_t class_reg = pci_read_config(bus, device, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass   = (class_reg >> 16) & 0xFF;
                uint8_t prog_if    = (class_reg >> 8)  & 0xFF;

                pci_device_t dev;
                dev.bus = bus;
                dev.device = device;
                dev.func = func;
                dev.vendor_id = vendor_id;
                dev.device_id = device_id;
                dev.class_code = class_code;
                dev.subclass = subclass;
                dev.prog_if = prog_if;

                pci_read_bars(&dev);
                
                serial_write_string("Bus ");
                serial_write_uint32(bus);
                serial_write_string(" Device ");
                serial_write_uint32(device);
                serial_write_string(" Func ");
                serial_write_uint32(func);
                serial_write_char('\n');

                serial_write_string("  VendorID: 0x");
                serial_write_uint16(vendor_id);
                serial_write_string(" DeviceID: 0x");
                serial_write_uint16(device_id);
                serial_write_char('\n');

                serial_write_string("  Class: 0x");
                serial_write_uint32(class_code);
                serial_write_string(" Subclass: 0x");
                serial_write_uint32(subclass);
                serial_write_string(" ProgIF: 0x");
                serial_write_uint32(prog_if);
                serial_write_char('\n');

                for (int i = 0; i < 6; i++) {
                    serial_write_string("  BAR");
                    serial_write_uint32(i);
                    serial_write_string(": 0x");
                    serial_write_uint32(dev.bar[i]);
                    serial_write_char('\n');
                }

                if (func == 0) {
                    uint32_t header_type = pci_read_config(bus, device, func, 0x0C);
                    if (((header_type >> 16) & 0x80) == 0) break;
                }
            }
        }
    }
}