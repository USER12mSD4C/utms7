#include "pci.h"
#include "../include/io.h"
#include "../drivers/drm.h"
#include "../drivers/i915.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

extern int e1000_init(pci_dev_t* pci);

u32 pci_read_config(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

static pci_dev_t pci_devices[32];
static int pci_device_count = 0;

int pci_init(void) {
    print("PCI: scanning...\n");
    pci_device_count = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                u32 id = pci_read_config(bus, slot, func, 0);
                if (id == 0xFFFFFFFF) {
                    if (func == 0) break;
                    continue;
                }

                u16 vendor = id & 0xFFFF;
                u16 device = (id >> 16) & 0xFFFF;

                u32 class_reg = pci_read_config(bus, slot, func, 8);
                u8 class = (class_reg >> 24) & 0xFF;
                u8 subclass = (class_reg >> 16) & 0xFF;
                u8 progif = (class_reg >> 8) & 0xFF;

                print("  ");
                printnum(bus);
                print(":");
                printnum(slot);
                print(".");
                printnum(func);
                print(" ");
                printhex(vendor);
                print(":");
                printhex(device);
                print(" class=");
                printnum(class);
                print(".");
                printnum(subclass);

                if (class == 0x02) print(" NET");
                if (class == 0x01 && subclass == 0x06 && progif == 0x01) print(" AHCI");
                if (class == 0x03) print(" VGA");
                print("\n");

                if (pci_device_count < 32) {
                    pci_dev_t* d = &pci_devices[pci_device_count];
                    d->vendor_id = vendor;
                    d->device_id = device;
                    d->bus = bus;
                    d->slot = slot;
                    d->func = func;
                    d->class_code = class;
                    d->subclass = subclass;
                    d->prog_if = progif;
                    u32 header = pci_read_config(bus, slot, func, 12);
                    d->header_type = (header >> 16) & 0xFF;
                    for (int i = 0; i < 6; i++) {
                        d->bar[i] = pci_read_config(bus, slot, func, 0x10 + i * 4);
                    }
                    u32 irq_reg = pci_read_config(bus, slot, func, 0x3C);
                    d->irq = irq_reg & 0xFF;
                    pci_device_count++;
                }

                if (func == 0) {
                    u32 hdr = pci_read_config(bus, slot, 0, 12);
                    if (!((hdr >> 16) & 0x80)) break;
                }
            }
        }
    }

    print("PCI: initializing devices...\n");
    for (int i = 0; i < pci_device_count; i++) {
        pci_dev_t* d = &pci_devices[i];

        if (d->vendor_id == 0x8086 && d->class_code == 0x03) {
            print("  Found Intel GPU: 0x");
            printhex(d->device_id);
            print("\n");
            i915_init(d);
        }

        if (d->vendor_id == 0x8086 &&
            (d->device_id == 0x100E || d->device_id == 0x1502 || d->device_id == 0x153A)) {
            print("  Found Intel NIC: 0x");
            printhex(d->device_id);
            print("\n");
            e1000_init(d);
        }
    }

    return 0;
}

pci_dev_t* pci_find_device(u16 vendor, u16 device) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor && pci_devices[i].device_id == device) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

void pci_scan_network(void) {
    print("PCI network devices:\n");
    for (int i = 0; i < pci_device_count; i++) {
        pci_dev_t* d = &pci_devices[i];
        if (d->class_code == 0x02) {
            print("  ");
            printhex(d->vendor_id);
            print(":");
            printhex(d->device_id);
            print(" - ");
            if (d->vendor_id == 0x10EC && d->device_id == 0x8139) {
                print("Realtek RTL8139");
            } else if (d->vendor_id == 0x8086 && d->device_id == 0x100E) {
                print("Intel PRO/1000");
            } else {
                print("Unknown NIC");
            }
            print("\n");
        }
    }
}
