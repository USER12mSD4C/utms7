// drivers/pci.h
#ifndef PCI_H
#define PCI_H

#include "../include/types.h"

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 header_type;
    u32 bar[6];
    u8 irq;
    u8 bus;
    u8 slot;
    u8 func;
} pci_dev_t;

int pci_init(void);
pci_dev_t* pci_find_device(u16 vendor, u16 device);
void pci_scan_network(void);
u32 pci_read_config(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write_config(u8 bus, u8 slot, u8 func, u8 offset, u32 value);

#endif
