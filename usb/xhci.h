#ifndef XHCI_H
#define XHCI_H

#include "../include/types.h"

#define XHCI_BASE 0x40000000 // TODO: из PCI

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
    u32 reserved2[4];
    u32 dcbaap_low;
    u32 dcbaap_high;
    u32 config;
} __attribute__((packed)) xhci_op_regs_t;

void xhci_init(void);
int xhci_handle_device(int port);

#endif
