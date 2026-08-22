// usb/xhci.h
#ifndef XHCI_H
#define XHCI_H

#include "../include/types.h"

typedef struct {
    u8  caplength;
    u8  reserved;
    u16 hciversion;
    u32 hcsparams1;
    u32 hcsparams2;
    u32 hcsparams3;
    u32 hccparams1;
    u32 dboff;
    u32 rtsoff;
    u32 hccparams2;
} __attribute__((packed)) xhci_cap_regs_t;

typedef struct {
    u32 usbcmd;
    u32 usbsts;
    u32 pagesize;
    u32 reserved1[2];
    u32 dnctrl;
    u32 crcr_low;
    u32 crcr_high;
    u32 dcbaap_low;
    u32 dcbaap_high;
    u32 config;
} __attribute__((packed)) xhci_op_regs_t;

typedef struct {
    u32 mfindex;
    u32 reserved[3];
    u32 iman;
    u32 imod;
    u32 erstsz;
    u32 reserved2;
    u32 erstba_low;
    u32 erstba_high;
} __attribute__((packed)) xhci_rt_regs_t;

typedef struct {
    u32 portsc;
    u32 portpmsc;
    u32 portli;
    u32 porthlpmc;
} __attribute__((packed)) xhci_port_regs_t;

typedef struct {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
} pci_device_t;

typedef struct {
    pci_device_t pci_dev;
    volatile xhci_cap_regs_t* cap_regs;
    volatile xhci_op_regs_t* op_regs;
    volatile xhci_rt_regs_t* rt_regs;
    volatile xhci_port_regs_t* port_regs;
    u64* dcbaa;
    u64* command_ring;
    u64* event_ring;
    u32 num_ports;
    u32 page_size;
} xhci_controller_t;

int xhci_init(void);
int xhci_handle_device(int port);

#endif
