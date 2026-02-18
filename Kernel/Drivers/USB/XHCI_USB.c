#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef IMPLUS_DRIVER_MODULE
#include <string.h>
#include "../../../Serial.h"
#include "../../../Memory/DMA_Memory.h"
#include "../../../Paging/Paging_Main.h"
#include "../../DriverSelect.h"
#include "../../PCI/PCI_Main.h"
#else
#include "../DriverBinary.h"
#endif

#include "XHCI_USB.h"

#define PCI_CLASS_SERIAL    0x0C
#define PCI_SUBCLASS_USB    0x03
#define PCI_PROGIF_XHCI     0x30

#define XHCI_CAP_CAPLENGTH  0x00
#define XHCI_CAP_HCIVERSION 0x02
#define XHCI_CAP_HCSPARAMS1 0x04
#define XHCI_CAP_HCSPARAMS2 0x08
#define XHCI_CAP_HCCPARAMS1 0x10
#define XHCI_CAP_DBOFF      0x14
#define XHCI_CAP_RTSOFF     0x18

#define XHCI_OP_USBCMD      0x00
#define XHCI_OP_USBSTS      0x04
#define XHCI_OP_PAGESIZE    0x08
#define XHCI_OP_DNCTRL      0x14
#define XHCI_OP_CRCR        0x18
#define XHCI_OP_DCBAAP      0x30
#define XHCI_OP_CONFIG      0x38

#define USBCMD_RUN          (1u << 0)
#define USBCMD_HCRST        (1u << 1)
#define USBCMD_INTE         (1u << 2)
#define USBCMD_HSEE         (1u << 3)
#define USBSTS_HCH          (1u << 0)
#define USBSTS_CNR          (1u << 11)
#define CRCR_RCS            (1u << 0)

#define XHCI_RT_MFINDEX     0x00
#define XHCI_IR_IMAN        0x00
#define XHCI_IR_IMOD        0x04
#define XHCI_IR_ERSTSZ      0x08
#define XHCI_IR_ERSTBA      0x10
#define XHCI_IR_ERDP        0x18
#define IMAN_IP             (1u << 0)
#define IMAN_IE             (1u << 1)

#define XHCI_PORT_SC        0x00
#define PORTSC_CCS          (1u << 0)
#define PORTSC_PED          (1u << 1)
#define PORTSC_PR           (1u << 4)
#define PORTSC_PP           (1u << 9)
#define PORTSC_SPD_SHIFT    10
#define PORTSC_SPD_MASK     (0xFu << PORTSC_SPD_SHIFT)
#define PORTSC_CSC          (1u << 17)
#define PORTSC_PRC          (1u << 21)
#define PORTSC_CHANGE_BITS  (PORTSC_CSC | (1u<<18) | (1u<<19) | (1u<<20) | \
                             PORTSC_PRC | (1u<<22) | (1u<<23))

#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP_STAGE    2
#define TRB_TYPE_DATA_STAGE     3
#define TRB_TYPE_STATUS_STAGE   4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_DISABLE_SLOT   10
#define TRB_TYPE_ADDRESS_DEV    11
#define TRB_TYPE_CONFIG_EP      12
#define TRB_TYPE_EVAL_CTX       13
#define TRB_TYPE_NOOP_CMD       23
#define TRB_TYPE_EVT_TRANSFER   32
#define TRB_TYPE_EVT_CMD_COMPL  33
#define TRB_TYPE_EVT_PORT_SC    34

#define TRB_CYCLE               (1u << 0)
#define TRB_ENT                 (1u << 1)
#define TRB_ISP                 (1u << 2)
#define TRB_CHAIN               (1u << 4)
#define TRB_IOC                 (1u << 5)
#define TRB_IDT                 (1u << 6)
#define TRB_TYPE(t)             ((uint32_t)(t) << 10)
#define TRB_TYPE_GET(c)         (((c) >> 10) & 0x3F)
#define TRB_SLOT(s)             ((uint32_t)(s) << 24)
#define TRB_EP(e)               ((uint32_t)(e) << 16)
#define TRB_TRT(t)              ((uint32_t)(t) << 16)

#define TRT_NO_DATA             0
#define TRT_OUT                 2
#define TRT_IN                  3

#define EVT_CC(status)          (((status) >> 24) & 0xFF)
#define EVT_SLOT(ctrl)          (((ctrl) >> 24) & 0xFF)
#define EVT_EP(ctrl)            (((ctrl) >> 16) & 0x1F)

#define CC_SUCCESS              1
#define CC_SHORT_PACKET         13
#define CC_STALL                6

#define XHCI_RING_SIZE          256
#define XHCI_EVENT_RING_SIZE    256
#define XHCI_MAX_SLOTS          64
#define XHCI_MAX_PORTS          32

#define EP_TYPE_CTRL            4
#define EP_TYPE_BULK_OUT        2
#define EP_TYPE_BULK_IN         6
#define EP_TYPE_INTR_OUT        3
#define EP_TYPE_INTR_IN         7
#define EP_TYPE_ISOCH_OUT       1
#define EP_TYPE_ISOCH_IN        5

#define USB_REQ_GET_STATUS      0
#define USB_REQ_CLEAR_FEATURE   1
#define USB_REQ_SET_FEATURE     3
#define USB_REQ_SET_ADDRESS     5
#define USB_REQ_GET_DESCRIPTOR  6
#define USB_REQ_SET_DESCRIPTOR  7
#define USB_REQ_GET_CONFIG      8
#define USB_REQ_SET_CONFIG      9
#define USB_REQ_GET_INTERFACE   10
#define USB_REQ_SET_INTERFACE   11
#define USB_REQ_HID_GET_REPORT  1
#define USB_REQ_HID_SET_IDLE    10
#define USB_REQ_HID_SET_PROTO   11

#define USB_RT_DEV_TO_HOST      0x80
#define USB_RT_HOST_TO_DEV      0x00
#define USB_RT_TYPE_STANDARD    (0 << 5)
#define USB_RT_TYPE_CLASS       (1 << 5)
#define USB_RT_RECIP_DEVICE     0
#define USB_RT_RECIP_IFACE      1
#define USB_RT_RECIP_EP         2

#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIG         2
#define USB_DESC_STRING         3
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5
#define USB_DESC_HID_REPORT     0x22

#define USB_CLASS_HID           3
#define USB_CLASS_MASS_STORAGE  8
#define USB_CLASS_HUB           9

#ifdef IMPLUS_DRIVER_MODULE
static const driver_kernel_api_t *g_driver_api = NULL;

#define serial_write_string     g_driver_api->serial_write_string
#define dma_alloc(sz, phys)     g_driver_api->dma_alloc((sz), (phys))
#define dma_free(ptr, sz)       g_driver_api->dma_free((ptr), (sz))
#define virt_to_phys(v)         g_driver_api->virt_to_phys(v)
#define pci_read_config         g_driver_api->pci_read_config
#define pci_write_config        g_driver_api->pci_write_config
#define map_mmio_virt           g_driver_api->map_mmio_virt
#define memset(s,c,n)           g_driver_api->memset((s),(c),(n))
#define memcpy(d,s,n)           g_driver_api->memcpy((d),(s),(n))
#endif

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct __attribute__((packed, aligned(64))) {
    uint64_t base_addr;
    uint16_t seg_size;
    uint16_t rsvd0;
    uint32_t rsvd1;
} xhci_erst_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
    uint32_t rsvd[4];
} xhci_slot_ctx_t;

typedef struct __attribute__((packed)) {
    uint32_t dw0;
    uint32_t dw1;
    uint64_t deq;
    uint32_t dw4;
    uint32_t dw5;
    uint32_t rsvd[2];
} xhci_ep_ctx_t;

typedef struct __attribute__((packed)) {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t rsvd[5];
    uint32_t cfg_val;
} xhci_input_ctrl_ctx_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_pkt_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_desc_t;

typedef struct {
    xhci_trb_t *trbs;
    uint64_t    phys;
    uint32_t    enqueue;
    uint8_t     cycle;
    uint32_t    size;
} xhci_ring_t;

typedef struct {
    bool     valid;
    uint8_t  addr;
    uint8_t  type;
    bool     dir_in;
    uint16_t max_packet;
    uint8_t  interval;
    xhci_ring_t ring;
} xhci_ep_info_t;

typedef struct {
    bool     valid;
    uint8_t  slot_id;
    uint8_t  port;
    uint8_t  speed;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  protocol;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t mps_ep0;
    uint8_t  num_configs;
    uint8_t  active_config;
    uint8_t  num_ep;
    xhci_ep_info_t eps[16];
    uint8_t *dev_ctx;
    uint64_t dev_ctx_phys;
    uint8_t *input_ctx;
    uint64_t input_ctx_phys;
    xhci_ring_t ep0_ring;
} xhci_device_t;

typedef struct {
    volatile uint8_t  *mmio;
    volatile uint8_t  *op;
    volatile uint8_t  *rt;
    volatile uint32_t *db;
    uint8_t  cap_len;
    uint8_t  max_slots;
    uint8_t  max_ports;
    bool     ac64;
    uint64_t *dcbaa;
    uint64_t  dcbaa_phys;
    xhci_ring_t cmd_ring;
    xhci_trb_t       *evt_ring;
    uint64_t          evt_ring_phys;
    uint32_t          evt_deq;
    uint8_t           evt_cycle;
    xhci_erst_entry_t *erst;
    uint64_t           erst_phys;
    volatile bool    cmd_pending;
    volatile uint8_t cmd_cc;
    volatile uint8_t cmd_slot;
    xhci_device_t devices[XHCI_MAX_SLOTS + 1];
    bool ready;
} xhci_t;

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} usb_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} usb_csw_t;

static xhci_t g_xhci;
static uint32_t g_cbw_tag = 1;

static inline uint8_t  cap_read8(uint32_t off)  { return *(volatile uint8_t*)(g_xhci.mmio + off); }
static inline uint32_t cap_read32(uint32_t off) { return *(volatile uint32_t*)(g_xhci.mmio + off); }
static inline uint32_t op_read32(uint32_t off)  { return *(volatile uint32_t*)(g_xhci.op + off); }
static inline void     op_write32(uint32_t off, uint32_t v) { *(volatile uint32_t*)(g_xhci.op + off) = v; }

static inline void op_write64(uint32_t off, uint64_t v) {
    *(volatile uint32_t*)(g_xhci.op + off)     = (uint32_t)(v & 0xFFFFFFFF);
    *(volatile uint32_t*)(g_xhci.op + off + 4) = (uint32_t)(v >> 32);
}

static inline void rt_write32(uint32_t off, uint32_t v) { *(volatile uint32_t*)(g_xhci.rt + off) = v; }

static inline void rt_write64(uint32_t off, uint64_t v) {
    *(volatile uint32_t*)(g_xhci.rt + off)     = (uint32_t)(v & 0xFFFFFFFF);
    *(volatile uint32_t*)(g_xhci.rt + off + 4) = (uint32_t)(v >> 32);
}

static inline uint32_t portsc_read(uint8_t port)             { return *(volatile uint32_t*)(g_xhci.op + 0x400 + ((port - 1) * 16)); }
static inline void     portsc_write(uint8_t port, uint32_t v){ *(volatile uint32_t*)(g_xhci.op + 0x400 + ((port - 1) * 16)) = v; }
static inline void     db_ring(uint8_t slot, uint8_t target) { g_xhci.db[slot] = (uint32_t)target; }
static inline void     udelay(uint32_t us) { for (volatile uint32_t i = 0; i < us * 100; i++) __asm__ volatile("pause"); }

static bool ring_init(xhci_ring_t *r, uint32_t size) {
    size_t bytes = size * sizeof(xhci_trb_t);
    r->trbs = dma_alloc(bytes, &r->phys);
    if (!r->trbs) return false;
    memset(r->trbs, 0, bytes);
    r->size    = size;
    r->enqueue = 0;
    r->cycle   = 1;
    xhci_trb_t *link = &r->trbs[size - 1];
    link->parameter = r->phys;
    link->status    = 0;
    link->control   = TRB_TYPE(TRB_TYPE_LINK) | (1u << 1) | r->cycle;
    return true;
}

static xhci_trb_t* ring_enqueue(xhci_ring_t *r) {
    xhci_trb_t *trb = &r->trbs[r->enqueue];
    r->enqueue++;
    if (r->enqueue >= r->size - 1) {
        xhci_trb_t *link = &r->trbs[r->size - 1];
        link->control = (link->control & ~(uint32_t)1) | r->cycle;
        r->enqueue = 0;
        r->cycle  ^= 1;
    }
    return trb;
}

static void cmd_post(xhci_trb_t *trb) {
    xhci_trb_t *slot = ring_enqueue(&g_xhci.cmd_ring);
    slot->parameter = trb->parameter;
    slot->status    = trb->status;
    slot->control   = (trb->control & ~(uint32_t)1) | g_xhci.cmd_ring.cycle;
    db_ring(0, 0);
}

static void process_events(void) {
    while (1) {
        xhci_trb_t *evt = &g_xhci.evt_ring[g_xhci.evt_deq];
        if ((evt->control & 1) != g_xhci.evt_cycle) break;
        uint32_t type = TRB_TYPE_GET(evt->control);
        if (type == TRB_TYPE_EVT_CMD_COMPL) {
            if (g_xhci.cmd_pending) {
                g_xhci.cmd_cc     = EVT_CC(evt->status);
                g_xhci.cmd_slot   = EVT_SLOT(evt->control);
                g_xhci.cmd_pending = false;
            }
        }
        g_xhci.evt_deq++;
        if (g_xhci.evt_deq >= XHCI_EVENT_RING_SIZE) {
            g_xhci.evt_deq   = 0;
            g_xhci.evt_cycle ^= 1;
        }
        uint64_t erdp = g_xhci.evt_ring_phys + g_xhci.evt_deq * sizeof(xhci_trb_t);
        erdp |= (1u << 3);
        rt_write64(0x20 + XHCI_IR_ERDP, erdp);
    }
}

static bool cmd_wait(uint32_t timeout_ms) {
    g_xhci.cmd_pending = true;
    for (uint32_t i = 0; i < timeout_ms; i++) {
        udelay(1000);
        process_events();
        if (!g_xhci.cmd_pending) return (g_xhci.cmd_cc == CC_SUCCESS);
    }
    g_xhci.cmd_pending = false;
    serial_write_string("[USB] Command timeout\n");
    return false;
}

static int32_t wait_for_transfer(uint8_t slot, uint8_t ep_dci, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms; i++) {
        uint32_t deq = g_xhci.evt_deq;
        uint8_t  cyc = g_xhci.evt_cycle;
        for (uint32_t j = 0; j < XHCI_EVENT_RING_SIZE; j++) {
            uint32_t idx = (deq + j) % XHCI_EVENT_RING_SIZE;
            xhci_trb_t *evt = &g_xhci.evt_ring[idx];
            if ((evt->control & 1) != cyc) break;
            if (TRB_TYPE_GET(evt->control) == TRB_TYPE_EVT_TRANSFER) {
                if (EVT_SLOT(evt->control) == slot && EVT_EP(evt->control) == ep_dci) {
                    uint8_t  cc     = EVT_CC(evt->status);
                    uint32_t remain = evt->status & 0xFFFFFF;
                    process_events();
                    if (cc == CC_SUCCESS || cc == CC_SHORT_PACKET) return (int32_t)remain;
                    return -1;
                }
            }
        }
        udelay(1000);
        process_events();
    }
    return -1;
}

static bool find_xhci_pci(uint16_t *out_bus, uint8_t *out_dev, uint8_t *out_func, uint64_t *out_bar) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t cc = pci_read_config((uint8_t)bus, dev, func, 0x08);
                if (((cc >> 24) & 0xFF) == PCI_CLASS_SERIAL &&
                    ((cc >> 16) & 0xFF) == PCI_SUBCLASS_USB  &&
                    ((cc >>  8) & 0xFF) == PCI_PROGIF_XHCI)
                {
                    uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
                    uint64_t phys = bar0 & ~0xFu;
                    if ((bar0 & 0x06) == 0x04) {
                        uint64_t bar1 = pci_read_config((uint8_t)bus, dev, func, 0x14);
                        phys |= bar1 << 32;
                    }
                    *out_bus  = bus;
                    *out_dev  = dev;
                    *out_func = func;
                    *out_bar  = phys;
                    return true;
                }
            }
        }
    }
    return false;
}

static void bios_handoff(void) {
    uint32_t hccp1 = cap_read32(XHCI_CAP_HCCPARAMS1);
    uint32_t xecp  = ((hccp1 >> 16) & 0xFFFF) << 2;
    if (!xecp) return;
    while (xecp) {
        uint32_t val = *(volatile uint32_t*)(g_xhci.mmio + xecp);
        if ((val & 0xFF) == 1) {
            *(volatile uint32_t*)(g_xhci.mmio + xecp) = val | (1u << 24);
            for (int i = 0; i < 1000; i++) {
                udelay(1000);
                val = *(volatile uint32_t*)(g_xhci.mmio + xecp);
                if (!(val & (1u << 16))) break;
            }
            uint32_t ctl = *(volatile uint32_t*)(g_xhci.mmio + xecp + 4);
            ctl &= ~0x1F; ctl &= ~(7u << 13);
            *(volatile uint32_t*)(g_xhci.mmio + xecp + 4) = ctl;
            return;
        }
        uint8_t next = (val >> 8) & 0xFF;
        if (!next) break;
        xecp += next << 2;
    }
}

static bool hc_reset(void) {
    uint32_t cmd = op_read32(XHCI_OP_USBCMD);
    if (cmd & USBCMD_RUN) {
        op_write32(XHCI_OP_USBCMD, cmd & ~USBCMD_RUN);
        for (int i = 0; i < 1000; i++) {
            udelay(1000);
            if (op_read32(XHCI_OP_USBSTS) & USBSTS_HCH) break;
        }
    }
    op_write32(XHCI_OP_USBCMD, op_read32(XHCI_OP_USBCMD) | USBCMD_HCRST);
    for (int i = 0; i < 1000; i++) {
        udelay(1000);
        if (!(op_read32(XHCI_OP_USBCMD) & USBCMD_HCRST) &&
            !(op_read32(XHCI_OP_USBSTS) & USBSTS_CNR)) return true;
    }
    serial_write_string("[USB] HC reset timeout\n");
    return false;
}

static xhci_slot_ctx_t*       dev_slot_ctx(xhci_device_t *d)           { return (xhci_slot_ctx_t*)(d->dev_ctx + 0); }
static xhci_ep_ctx_t*         dev_ep_ctx(xhci_device_t *d, uint8_t dci){ return (xhci_ep_ctx_t*)(d->dev_ctx + 32 * dci); }
static xhci_input_ctrl_ctx_t* in_ctrl(xhci_device_t *d)                { return (xhci_input_ctrl_ctx_t*)(d->input_ctx + 0); }
static xhci_slot_ctx_t*       in_slot(xhci_device_t *d)                { return (xhci_slot_ctx_t*)(d->input_ctx + 32); }
static xhci_ep_ctx_t*         in_ep(xhci_device_t *d, uint8_t dci)     { return (xhci_ep_ctx_t*)(d->input_ctx + 32 + 32 * dci); }

static uint8_t ep_addr_to_dci(uint8_t ep_addr) {
    uint8_t num = ep_addr & 0x0F;
    if (num == 0) return 1;
    return (uint8_t)(num * 2 + ((ep_addr & 0x80) ? 1 : 0));
}

static bool control_transfer(xhci_device_t *d, usb_setup_pkt_t *setup,
                              void *data_buf, uint16_t data_len, bool dir_in) {
    {
        xhci_trb_t *trb = ring_enqueue(&d->ep0_ring);
        memset(trb, 0, sizeof(*trb));
        memcpy(&trb->parameter, setup, 8);
        trb->status  = 8;
        uint32_t trt = (data_len == 0) ? TRT_NO_DATA : (dir_in ? TRT_IN : TRT_OUT);
        trb->control = TRB_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_IDT | TRB_TRT(trt) | d->ep0_ring.cycle;
    }
    if (data_buf && data_len > 0) {
        xhci_trb_t *trb = ring_enqueue(&d->ep0_ring);
        memset(trb, 0, sizeof(*trb));
        trb->parameter = virt_to_phys(data_buf);
        trb->status    = data_len;
        trb->control   = TRB_TYPE(TRB_TYPE_DATA_STAGE) | TRB_IOC |
                         (dir_in ? (1u << 16) : 0) | d->ep0_ring.cycle;
    }
    {
        xhci_trb_t *trb = ring_enqueue(&d->ep0_ring);
        memset(trb, 0, sizeof(*trb));
        trb->control = TRB_TYPE(TRB_TYPE_STATUS_STAGE) |
                       (!dir_in ? (1u << 16) : 0) | d->ep0_ring.cycle;
        if (data_len == 0) trb->control |= TRB_IOC;
    }
    db_ring(d->slot_id, 1);
    return (wait_for_transfer(d->slot_id, 1, 500) >= 0);
}

static bool usb_get_descriptor(xhci_device_t *d, uint8_t type, uint8_t idx,
                                void *buf, uint16_t len) {
    usb_setup_pkt_t s = {
        .bRequestType = USB_RT_DEV_TO_HOST | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        .bRequest     = USB_REQ_GET_DESCRIPTOR,
        .wValue       = (uint16_t)((type << 8) | idx),
        .wIndex       = 0,
        .wLength      = len,
    };
    return control_transfer(d, &s, buf, len, true);
}

static bool usb_set_configuration(xhci_device_t *d, uint8_t config_val) {
    usb_setup_pkt_t s = {
        .bRequestType = USB_RT_HOST_TO_DEV | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        .bRequest     = USB_REQ_SET_CONFIG,
        .wValue       = config_val,
        .wIndex       = 0,
        .wLength      = 0,
    };
    return control_transfer(d, &s, NULL, 0, false);
}

static int32_t bulk_transfer(xhci_device_t *d, uint8_t ep_dci,
                              void *buf, uint32_t len, bool dir_in) {
    xhci_ring_t *ring = NULL;
    for (uint8_t i = 0; i < d->num_ep; i++) {
        if (ep_addr_to_dci(d->eps[i].addr) == ep_dci) { ring = &d->eps[i].ring; break; }
    }
    if (!ring) return -1;
    xhci_trb_t *trb = ring_enqueue(ring);
    memset(trb, 0, sizeof(*trb));
    trb->parameter = virt_to_phys(buf);
    trb->status    = len;
    trb->control   = TRB_TYPE(TRB_TYPE_NORMAL) | TRB_IOC | TRB_ISP | ring->cycle;
    db_ring(d->slot_id, ep_dci);
    return wait_for_transfer(d->slot_id, ep_dci, 5000);
}

static int32_t intr_transfer(xhci_device_t *d, uint8_t ep_dci,
                              void *buf, uint32_t len, uint32_t timeout_ms) {
    xhci_ring_t *ring = NULL;
    for (uint8_t i = 0; i < d->num_ep; i++) {
        if (ep_addr_to_dci(d->eps[i].addr) == ep_dci) { ring = &d->eps[i].ring; break; }
    }
    if (!ring) return -1;
    xhci_trb_t *trb = ring_enqueue(ring);
    memset(trb, 0, sizeof(*trb));
    trb->parameter = virt_to_phys(buf);
    trb->status    = len;
    trb->control   = TRB_TYPE(TRB_TYPE_NORMAL) | TRB_IOC | TRB_ISP | ring->cycle;
    db_ring(d->slot_id, ep_dci);
    return wait_for_transfer(d->slot_id, ep_dci, timeout_ms);
}

static uint8_t cmd_enable_slot(void) {
    xhci_trb_t trb = {0};
    trb.control = TRB_TYPE(TRB_TYPE_ENABLE_SLOT);
    cmd_post(&trb);
    if (!cmd_wait(500)) return 0;
    return g_xhci.cmd_slot;
}

static bool cmd_address_device(xhci_device_t *d, bool bsr) {
    xhci_trb_t trb = {0};
    trb.parameter = d->input_ctx_phys;
    trb.control   = TRB_TYPE(TRB_TYPE_ADDRESS_DEV) | TRB_SLOT(d->slot_id) | (bsr ? (1u << 9) : 0);
    cmd_post(&trb);
    return cmd_wait(500);
}

static bool cmd_configure_ep(xhci_device_t *d) {
    xhci_trb_t trb = {0};
    trb.parameter = d->input_ctx_phys;
    trb.control   = TRB_TYPE(TRB_TYPE_CONFIG_EP) | TRB_SLOT(d->slot_id);
    cmd_post(&trb);
    return cmd_wait(500);
}

static bool cmd_evaluate_ctx(xhci_device_t *d) {
    xhci_trb_t trb = {0};
    trb.parameter = d->input_ctx_phys;
    trb.control   = TRB_TYPE(TRB_TYPE_EVAL_CTX) | TRB_SLOT(d->slot_id);
    cmd_post(&trb);
    return cmd_wait(500);
}

static bool port_reset(uint8_t port) {
    uint32_t sc = portsc_read(port);
    if (!(sc & PORTSC_PP)) {
        portsc_write(port, sc | PORTSC_PP);
        udelay(20000);
        sc = portsc_read(port);
    }
    portsc_write(port, (sc & ~PORTSC_CHANGE_BITS) | PORTSC_PR);
    for (int i = 0; i < 1000; i++) {
        udelay(1000);
        sc = portsc_read(port);
        if (!(sc & PORTSC_PR)) break;
    }
    if (sc & PORTSC_PR) {
        serial_write_string("[USB] Port reset timeout\n");
        return false;
    }
    portsc_write(port, (sc & ~PORTSC_CHANGE_BITS) | PORTSC_CHANGE_BITS);
    udelay(10000);
    return true;
}

static bool device_alloc_contexts(xhci_device_t *d) {
    d->input_ctx = dma_alloc(1056, &d->input_ctx_phys);
    if (!d->input_ctx) return false;
    memset(d->input_ctx, 0, 1056);
    d->dev_ctx = dma_alloc(1024, &d->dev_ctx_phys);
    if (!d->dev_ctx) return false;
    memset(d->dev_ctx, 0, 1024);
    g_xhci.dcbaa[d->slot_id] = d->dev_ctx_phys;
    return true;
}

static bool device_init(uint8_t port, uint8_t speed) {
    uint8_t slot = cmd_enable_slot();
    if (!slot) { serial_write_string("[USB] Enable Slot failed\n"); return false; }

    xhci_device_t *d = &g_xhci.devices[slot];
    memset(d, 0, sizeof(*d));
    d->valid   = true;
    d->slot_id = slot;
    d->port    = port;
    d->speed   = speed;
    d->mps_ep0 = (speed == 4) ? 512 : 8;

    if (!device_alloc_contexts(d)) return false;
    if (!ring_init(&d->ep0_ring, XHCI_RING_SIZE)) return false;

    in_ctrl(d)->add_flags  = 0x03;
    in_slot(d)->dw0        = (1u << 27) | ((uint32_t)speed << 20);
    in_slot(d)->dw1        = (uint32_t)port << 16;

    xhci_ep_ctx_t *iep0 = in_ep(d, 1);
    iep0->dw1 = (3u << 1) | (EP_TYPE_CTRL << 3) | ((uint32_t)d->mps_ep0 << 16);
    iep0->deq = d->ep0_ring.phys | 1;
    iep0->dw4 = (uint32_t)d->mps_ep0 | (8u << 16);

    if (!cmd_address_device(d, false)) {
        serial_write_string("[USB] Address Device failed\n");
        return false;
    }

    uint8_t *desc_buf = dma_alloc(512, NULL);
    if (!desc_buf) return false;

    if (!usb_get_descriptor(d, USB_DESC_DEVICE, 0, desc_buf, 8)) {
        serial_write_string("[USB] Get Device Desc failed\n");
        dma_free(desc_buf, 512);
        return false;
    }

    usb_device_desc_t *dd = (usb_device_desc_t*)desc_buf;
    d->mps_ep0 = dd->bMaxPacketSize0;

    uint16_t old_mps = (iep0->dw1 >> 16) & 0xFFFF;
    if (d->mps_ep0 != old_mps) {
        memset(in_ctrl(d), 0, 32);
        in_ctrl(d)->add_flags = 0x02;
        in_ep(d, 1)->dw1 = (in_ep(d, 1)->dw1 & 0x0000FFFF) | ((uint32_t)d->mps_ep0 << 16);
        cmd_evaluate_ctx(d);
    }

    if (!usb_get_descriptor(d, USB_DESC_DEVICE, 0, desc_buf, sizeof(usb_device_desc_t))) {
        dma_free(desc_buf, 512);
        return false;
    }
    dd = (usb_device_desc_t*)desc_buf;
    d->class_code  = dd->bDeviceClass;
    d->subclass    = dd->bDeviceSubClass;
    d->protocol    = dd->bDeviceProtocol;
    d->vendor_id   = dd->idVendor;
    d->product_id  = dd->idProduct;
    d->num_configs = dd->bNumConfigurations;

    dma_free(desc_buf, 512);
    serial_write_string("[USB] Device init OK\n");
    return true;
}

static bool configure_endpoints(xhci_device_t *d, usb_interface_desc_t *iface,
                                 usb_endpoint_desc_t **eps, uint8_t num_ep) {
    (void)iface;
    memset(in_ctrl(d), 0, 32);
    in_ctrl(d)->add_flags = 0x01;
    memcpy(in_slot(d), dev_slot_ctx(d), 32);

    uint8_t max_dci = 1;
    d->num_ep = 0;

    for (uint8_t i = 0; i < num_ep && i < 15; i++) {
        usb_endpoint_desc_t *ep = eps[i];
        uint8_t  dci       = ep_addr_to_dci(ep->bEndpointAddress);
        bool     dir_in    = (ep->bEndpointAddress & 0x80) != 0;
        uint8_t  ep_type   = ep->bmAttributes & 0x03;
        uint16_t mps       = ep->wMaxPacketSize & 0x7FF;
        uint8_t  xhci_type;

        switch (ep_type) {
            case 1: xhci_type = dir_in ? EP_TYPE_ISOCH_IN : EP_TYPE_ISOCH_OUT; break;
            case 2: xhci_type = dir_in ? EP_TYPE_BULK_IN  : EP_TYPE_BULK_OUT;  break;
            case 3: xhci_type = dir_in ? EP_TYPE_INTR_IN  : EP_TYPE_INTR_OUT;  break;
            default: xhci_type = EP_TYPE_CTRL; break;
        }

        xhci_ep_info_t *einfo = &d->eps[d->num_ep++];
        einfo->valid      = true;
        einfo->addr       = ep->bEndpointAddress;
        einfo->type       = ep_type;
        einfo->dir_in     = dir_in;
        einfo->max_packet = mps;
        einfo->interval   = ep->bInterval;
        if (!ring_init(&einfo->ring, XHCI_RING_SIZE)) return false;

        xhci_ep_ctx_t *iep = in_ep(d, dci);
        memset(iep, 0, 32);
        iep->dw0 = ep->bInterval ? ((uint32_t)(ep->bInterval - 1) << 16) : 0;
        iep->dw1 = (3u << 1) | ((uint32_t)xhci_type << 3) | ((uint32_t)mps << 16);
        iep->deq = einfo->ring.phys | 1;
        iep->dw4 = mps | (mps << 16);

        in_ctrl(d)->add_flags |= (1u << dci);
        if (dci > max_dci) max_dci = dci;
    }

    in_slot(d)->dw0 = (in_slot(d)->dw0 & ~(0x1Fu << 27)) | ((uint32_t)max_dci << 27);
    return cmd_configure_ep(d);
}

static bool parse_and_configure(xhci_device_t *d) {
    uint8_t *cbuf = dma_alloc(512, NULL);
    if (!cbuf) return false;

    if (!usb_get_descriptor(d, USB_DESC_CONFIG, 0, cbuf, 9)) {
        dma_free(cbuf, 512); return false;
    }
    uint16_t tlen = ((usb_config_desc_t*)cbuf)->wTotalLength;
    if (tlen > 512) tlen = 512;

    if (!usb_get_descriptor(d, USB_DESC_CONFIG, 0, cbuf, tlen)) {
        dma_free(cbuf, 512); return false;
    }

    usb_config_desc_t    *cd         = (usb_config_desc_t*)cbuf;
    usb_interface_desc_t *best_iface = NULL;
    usb_endpoint_desc_t  *ep_list[15];
    uint8_t               ep_cnt     = 0;
    uint8_t *ptr = cbuf, *end = cbuf + tlen;

    while (ptr < end) {
        uint8_t len = ptr[0], type = ptr[1];
        if (len < 2) break;
        if (type == USB_DESC_INTERFACE) {
            best_iface = (usb_interface_desc_t*)ptr;
            ep_cnt = 0;
            if (d->class_code == 0) {
                d->class_code = best_iface->bInterfaceClass;
                d->subclass   = best_iface->bInterfaceSubClass;
                d->protocol   = best_iface->bInterfaceProtocol;
            }
        } else if (type == USB_DESC_ENDPOINT && ep_cnt < 15) {
            ep_list[ep_cnt++] = (usb_endpoint_desc_t*)ptr;
        }
        ptr += len;
    }

    if (!usb_set_configuration(d, cd->bConfigurationValue)) {
        dma_free(cbuf, 512); return false;
    }
    d->active_config = cd->bConfigurationValue;

    bool ok = true;
    if (best_iface && ep_cnt > 0)
        ok = configure_endpoints(d, best_iface, ep_list, ep_cnt);

    dma_free(cbuf, 512);
    return ok;
}

static void hid_init(xhci_device_t *d, uint8_t iface_num) {
    serial_write_string("[USB] HID device detected\n");
    usb_setup_pkt_t s_idle = {
        .bRequestType = USB_RT_HOST_TO_DEV | USB_RT_TYPE_CLASS | USB_RT_RECIP_IFACE,
        .bRequest = USB_REQ_HID_SET_IDLE, .wValue = 0, .wIndex = iface_num, .wLength = 0,
    };
    control_transfer(d, &s_idle, NULL, 0, false);

    usb_setup_pkt_t s_proto = {
        .bRequestType = USB_RT_HOST_TO_DEV | USB_RT_TYPE_CLASS | USB_RT_RECIP_IFACE,
        .bRequest = USB_REQ_HID_SET_PROTO, .wValue = 0, .wIndex = iface_num, .wLength = 0,
    };
    control_transfer(d, &s_proto, NULL, 0, false);
}

static bool msc_send_command(xhci_device_t *d, uint8_t bulk_out, uint8_t bulk_in,
                              const uint8_t *cdb, uint8_t cdb_len,
                              void *data, uint32_t data_len, bool data_in) {
    usb_cbw_t *cbw = dma_alloc(sizeof(usb_cbw_t), NULL);
    if (!cbw) return false;
    cbw->dCBWSignature          = 0x43425355;
    cbw->dCBWTag                = g_cbw_tag++;
    cbw->dCBWDataTransferLength = data_len;
    cbw->bmCBWFlags             = data_in ? 0x80 : 0x00;
    cbw->bCBWLUN                = 0;
    cbw->bCBWCBLength           = cdb_len;
    memset(cbw->CBWCB, 0, 16);
    memcpy(cbw->CBWCB, cdb, cdb_len);

    bool ok = (bulk_transfer(d, bulk_out, cbw, sizeof(usb_cbw_t), false) >= 0);
    dma_free(cbw, sizeof(usb_cbw_t));
    if (!ok) return false;

    if (data && data_len > 0)
        ok = (bulk_transfer(d, data_in ? bulk_in : bulk_out, data, data_len, data_in) >= 0);

    usb_csw_t *csw = dma_alloc(sizeof(usb_csw_t), NULL);
    if (!csw) return false;
    bool csw_ok = (bulk_transfer(d, bulk_in, csw, sizeof(usb_csw_t), true) >= 0);
    bool status_ok = (csw->dCSWSignature == 0x53425355 && csw->bCSWStatus == 0);
    dma_free(csw, sizeof(usb_csw_t));
    return ok && csw_ok && status_ok;
}

static void msc_init(xhci_device_t *d) {
    serial_write_string("[USB] Mass Storage device detected\n");
    uint8_t bulk_out = 0, bulk_in = 0;
    for (uint8_t i = 0; i < d->num_ep; i++) {
        if (d->eps[i].type == 2) {
            uint8_t dci = ep_addr_to_dci(d->eps[i].addr);
            if (d->eps[i].dir_in) bulk_in  = dci;
            else                  bulk_out = dci;
        }
    }
    if (!bulk_out || !bulk_in) { serial_write_string("[USB] MSC: bulk EPs not found\n"); return; }

    uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0};
    uint8_t *buf = dma_alloc(36, NULL);
    if (buf) {
        msc_send_command(d, bulk_out, bulk_in, cdb, 6, buf, 36, true);
        dma_free(buf, 36);
    }
}

static void device_class_init(xhci_device_t *d) {
    if (d->class_code == USB_CLASS_HID)
        hid_init(d, 0);
    else if (d->class_code == USB_CLASS_MASS_STORAGE)
        msc_init(d);
    else
        serial_write_string("[USB] Unknown class device\n");
}

static void enumerate_ports(void) {
    for (uint8_t port = 1; port <= g_xhci.max_ports; port++) {
        if (!(portsc_read(port) & PORTSC_CCS)) continue;
        if (!port_reset(port)) continue;
        uint32_t sc = portsc_read(port);
        if (!(sc & PORTSC_CCS)) continue;
        uint8_t speed = (uint8_t)((sc & PORTSC_SPD_MASK) >> PORTSC_SPD_SHIFT);
        udelay(10000);
        if (!device_init(port, speed)) { serial_write_string("[USB] Device init failed\n"); continue; }
        for (uint8_t s = 1; s <= g_xhci.max_slots; s++) {
            xhci_device_t *d = &g_xhci.devices[s];
            if (!d->valid || d->port != port) continue;
            if (!parse_and_configure(d)) { serial_write_string("[USB] Configure failed\n"); break; }
            device_class_init(d);
            break;
        }
    }
}

static bool xhci_init(void) {
    memset(&g_xhci, 0, sizeof(g_xhci));

    uint16_t bus; uint8_t dev, func; uint64_t bar;
    if (!find_xhci_pci(&bus, &dev, &func, &bar)) {
        serial_write_string("[USB] xHCI not found\n"); return false;
    }
    serial_write_string("[USB] xHCI found\n");

    uint32_t cmd = pci_read_config((uint8_t)bus, dev, func, 0x04);
    pci_write_config((uint8_t)bus, dev, func, 0x04, cmd | 0x06);

    g_xhci.mmio = (volatile uint8_t*)map_mmio_virt(bar);
    if (!g_xhci.mmio) { serial_write_string("[USB] MMIO map failed\n"); return false; }

    g_xhci.cap_len  = cap_read8(XHCI_CAP_CAPLENGTH);
    g_xhci.op       = g_xhci.mmio + g_xhci.cap_len;
    g_xhci.db       = (volatile uint32_t*)(g_xhci.mmio + cap_read32(XHCI_CAP_DBOFF));
    g_xhci.rt       = g_xhci.mmio + cap_read32(XHCI_CAP_RTSOFF);

    uint32_t hcsp1      = cap_read32(XHCI_CAP_HCSPARAMS1);
    g_xhci.max_slots    = (uint8_t)(hcsp1 & 0xFF);
    g_xhci.max_ports    = (uint8_t)((hcsp1 >> 24) & 0xFF);
    if (g_xhci.max_slots > XHCI_MAX_SLOTS) g_xhci.max_slots = XHCI_MAX_SLOTS;
    if (g_xhci.max_ports > XHCI_MAX_PORTS) g_xhci.max_ports = XHCI_MAX_PORTS;
    g_xhci.ac64 = (cap_read32(XHCI_CAP_HCCPARAMS1) & 1) != 0;

    bios_handoff();
    if (!hc_reset()) return false;
    udelay(1000);

    op_write32(XHCI_OP_CONFIG, g_xhci.max_slots);

    {
        size_t sz = (g_xhci.max_slots + 1) * sizeof(uint64_t);
        uint64_t phys;
        g_xhci.dcbaa = dma_alloc(sz, &phys);
        if (!g_xhci.dcbaa) return false;
        memset(g_xhci.dcbaa, 0, sz);
        g_xhci.dcbaa_phys = phys;
        op_write64(XHCI_OP_DCBAAP, phys);
    }

    {
        uint32_t hcsp2 = cap_read32(XHCI_CAP_HCSPARAMS2);
        uint32_t num_sp = ((hcsp2 >> 27) & 0x1F) | (((hcsp2 >> 21) & 0x1F) << 5);
        if (num_sp > 0) {
            uint64_t arr_phys;
            uint64_t *arr = dma_alloc(num_sp * sizeof(uint64_t), &arr_phys);
            if (arr) {
                for (uint32_t i = 0; i < num_sp; i++) {
                    uint64_t pg_phys;
                    void *pg = dma_alloc(4096, &pg_phys);
                    if (pg) { memset(pg, 0, 4096); arr[i] = pg_phys; }
                }
                g_xhci.dcbaa[0] = arr_phys;
            }
        }
    }

    if (!ring_init(&g_xhci.cmd_ring, XHCI_RING_SIZE)) return false;
    op_write64(XHCI_OP_CRCR, g_xhci.cmd_ring.phys | CRCR_RCS);

    {
        uint64_t er_phys;
        g_xhci.evt_ring = dma_alloc(XHCI_EVENT_RING_SIZE * sizeof(xhci_trb_t), &er_phys);
        if (!g_xhci.evt_ring) return false;
        memset(g_xhci.evt_ring, 0, XHCI_EVENT_RING_SIZE * sizeof(xhci_trb_t));
        g_xhci.evt_ring_phys = er_phys;
        g_xhci.evt_deq       = 0;
        g_xhci.evt_cycle     = 1;

        uint64_t erst_phys;
        g_xhci.erst = dma_alloc(sizeof(xhci_erst_entry_t), &erst_phys);
        if (!g_xhci.erst) return false;
        g_xhci.erst->base_addr = er_phys;
        g_xhci.erst->seg_size  = XHCI_EVENT_RING_SIZE;
        g_xhci.erst_phys       = erst_phys;

        rt_write32(0x20 + XHCI_IR_ERSTSZ, 1);
        rt_write64(0x20 + XHCI_IR_ERSTBA, erst_phys);
        rt_write64(0x20 + XHCI_IR_ERDP,   er_phys);
        rt_write32(0x20 + XHCI_IR_IMAN,   IMAN_IE | IMAN_IP);
    }

    op_write32(XHCI_OP_USBCMD, op_read32(XHCI_OP_USBCMD) | USBCMD_RUN | USBCMD_INTE | USBCMD_HSEE);
    for (int i = 0; i < 1000; i++) {
        udelay(1000);
        if (!(op_read32(XHCI_OP_USBSTS) & USBSTS_HCH)) break;
    }
    if (op_read32(XHCI_OP_USBSTS) & USBSTS_HCH) {
        serial_write_string("[USB] HC won't start\n"); return false;
    }

    g_xhci.ready = true;
    serial_write_string("[USB] xHCI init complete\n");
    udelay(100000);
    enumerate_ports();
    serial_write_string("[XHCI] controller found\n");
    return true;
}

static bool xhci_probe(void) {
    uint16_t bus; uint8_t dev, func; uint64_t bar;
    return find_xhci_pci(&bus, &dev, &func, &bar);
}

static bool xhci_is_ready(void) { return g_xhci.ready; }

static void xhci_poll(void) {
    if (!g_xhci.ready) return;
    process_events();
    for (uint8_t port = 1; port <= g_xhci.max_ports; port++) {
        uint32_t sc = portsc_read(port);
        if (!(sc & PORTSC_CSC)) continue;
        portsc_write(port, (sc & ~PORTSC_CHANGE_BITS) | PORTSC_CSC);
        sc = portsc_read(port);
        if (sc & PORTSC_CCS) {
            serial_write_string("[USB] Hotplug: connected\n");
            if (port_reset(port)) {
                sc = portsc_read(port);
                uint8_t speed = (uint8_t)((sc & PORTSC_SPD_MASK) >> PORTSC_SPD_SHIFT);
                udelay(10000);
                if (device_init(port, speed)) {
                    for (uint8_t s = 1; s <= g_xhci.max_slots; s++) {
                        xhci_device_t *d = &g_xhci.devices[s];
                        if (!d->valid || d->port != port) continue;
                        if (parse_and_configure(d)) device_class_init(d);
                        break;
                    }
                }
            }
        } else {
            serial_write_string("[USB] Hotplug: disconnected\n");
            for (uint8_t s = 1; s <= g_xhci.max_slots; s++) {
                if (g_xhci.devices[s].valid && g_xhci.devices[s].port == port) {
                    g_xhci.dcbaa[s]              = 0;
                    g_xhci.devices[s].valid      = false;
                }
            }
        }
    }
}

static uint8_t xhci_device_count(void) {
    uint8_t cnt = 0;
    for (uint8_t s = 1; s <= g_xhci.max_slots; s++)
        if (g_xhci.devices[s].valid) cnt++;
    return cnt;
}

static bool xhci_get_device_info(uint8_t slot_id, uint16_t *vid, uint16_t *pid,
                                  uint8_t *class_code, uint8_t *subclass) {
    if (slot_id < 1 || slot_id > XHCI_MAX_SLOTS) return false;
    xhci_device_t *d = &g_xhci.devices[slot_id];
    if (!d->valid) return false;
    if (vid)        *vid        = d->vendor_id;
    if (pid)        *pid        = d->product_id;
    if (class_code) *class_code = d->class_code;
    if (subclass)   *subclass   = d->subclass;
    return true;
}

static bool xhci_msc_read(uint8_t slot_id, uint32_t lba, uint8_t sectors,
                           void *buf, uint32_t buf_size) {
    xhci_device_t *d = &g_xhci.devices[slot_id];
    if (!d->valid || d->class_code != USB_CLASS_MASS_STORAGE) return false;
    uint8_t bulk_out = 0, bulk_in = 0;
    for (uint8_t i = 0; i < d->num_ep; i++) {
        if (d->eps[i].type == 2) {
            uint8_t dci = ep_addr_to_dci(d->eps[i].addr);
            if (d->eps[i].dir_in) bulk_in  = dci;
            else                  bulk_out = dci;
        }
    }
    if (!bulk_out || !bulk_in) return false;
    uint8_t cdb[10] = {
        0x28, 0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >>  8), (uint8_t)(lba),
        0, 0, sectors, 0
    };
    return msc_send_command(d, bulk_out, bulk_in, cdb, 10, buf, buf_size, true);
}

static int32_t xhci_hid_read(uint8_t slot_id, void *buf, uint32_t len) {
    xhci_device_t *d = &g_xhci.devices[slot_id];
    if (!d->valid || d->class_code != USB_CLASS_HID) return -1;
    for (uint8_t i = 0; i < d->num_ep; i++) {
        if (d->eps[i].type == 3 && d->eps[i].dir_in)
            return intr_transfer(d, ep_addr_to_dci(d->eps[i].addr), buf, len, 100);
    }
    return -1;
}

static uint8_t xhci_get_max_ports(void)  { return g_xhci.max_ports; }
static uint8_t xhci_get_max_slots(void)  { return g_xhci.max_slots; }

static const usb_driver_t g_xhci_usb_driver = {
    .name            = "xHCI USB Controller",
    .probe           = xhci_probe,
    .init            = xhci_init,
    .is_ready        = xhci_is_ready,
    .poll            = xhci_poll,
    .device_count    = xhci_device_count,
    .get_device_info = xhci_get_device_info,
    .msc_read        = xhci_msc_read,
    .hid_read        = xhci_hid_read,
    .get_max_ports   = xhci_get_max_ports,
    .get_max_slots   = xhci_get_max_slots,
};

#ifdef IMPLUS_DRIVER_MODULE
#undef serial_write_string
#undef dma_alloc
#undef dma_free
#undef virt_to_phys
#undef pci_read_config
#undef pci_write_config
#undef map_mmio_virt
#undef memset
#undef memcpy

const usb_driver_t *driver_module_init(const driver_kernel_api_t *api) {
    if (!api || !api->serial_write_string || !api->dma_alloc || !api->dma_free ||
        !api->virt_to_phys || !api->pci_read_config || !api->pci_write_config ||
        !api->map_mmio_virt || !api->memset || !api->memcpy) {
        return NULL;
    }
    g_driver_api = api;
    return &g_xhci_usb_driver;
}

#else
void xhci_usb_register_driver(void) {
    driver_select_register_usb_driver(&g_xhci_usb_driver);
}
#endif