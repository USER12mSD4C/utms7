#include "pci.h"
#include "../include/io.h"
#include "../drivers/vga.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

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

int pci_init(void) {
    vga_write("PCI: scanning...\n");

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            u32 id = pci_read_config(bus, slot, 0, 0);
            if (id == 0xFFFFFFFF) continue;

            u16 vendor = id & 0xFFFF;
            u16 device = (id >> 16) & 0xFFFF;

            u32 class_reg = pci_read_config(bus, slot, 0, 8);
            u8 class = (class_reg >> 24) & 0xFF;
            u8 subclass = (class_reg >> 16) & 0xFF;

            // Выводим найденные устройства
            vga_write("  ");
            vga_write_num(bus);
            vga_write(":");
            vga_write_num(slot);
            vga_write(" ");
            vga_write_hex(vendor);
            vga_write(":");
            vga_write_hex(device);
            vga_write(" class=");
            vga_write_num(class);
            vga_write(".");
            vga_write_num(subclass);

            if (class == 0x02) { // Network controller
                vga_write(" NET");
            }
            vga_write("\n");
        }
    }
    return 0;
}

pci_dev_t* pci_find_device(u16 vendor, u16 device) {
    static pci_dev_t dev;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                u32 id = pci_read_config(bus, slot, func, 0);

                if (id == 0xFFFFFFFF) continue;

                dev.vendor_id = id & 0xFFFF;
                dev.device_id = (id >> 16) & 0xFFFF;
                dev.bus = bus;
                dev.slot = slot;
                dev.func = func;

                u32 class_reg = pci_read_config(bus, slot, func, 8);
                dev.class_code = (class_reg >> 24) & 0xFF;
                dev.subclass = (class_reg >> 16) & 0xFF;
                dev.prog_if = (class_reg >> 8) & 0xFF;

                u32 header = pci_read_config(bus, slot, func, 12);
                dev.header_type = (header >> 16) & 0xFF;

                for (int i = 0; i < 6; i++) {
                    dev.bar[i] = pci_read_config(bus, slot, func, 0x10 + i * 4);
                }

                u32 irq_reg = pci_read_config(bus, slot, func, 0x3C);
                dev.irq = irq_reg & 0xFF;

                if (dev.vendor_id == vendor && dev.device_id == device) {
                    return &dev;
                }

                if (func == 0 && !(dev.header_type & 0x80)) {
                    break;
                }
            }
        }
    }

    return NULL;
}

void pci_scan_network(void) {
    vga_write("PCI network devices:\n");

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            u32 id = pci_read_config(bus, slot, 0, 0);
            if (id == 0xFFFFFFFF) continue;

            u32 class_reg = pci_read_config(bus, slot, 0, 8);
            u8 class = (class_reg >> 24) & 0xFF;
            u8 subclass = (class_reg >> 16) & 0xFF;

            if (class == 0x02) {
                u16 vendor = id & 0xFFFF;
                u16 device = (id >> 16) & 0xFFFF;

                vga_write("  ");
                vga_write_num(vendor);
                vga_write(":");
                vga_write_num(device);
                vga_write(" - ");

                if (vendor == 0x10EC && device == 0x8139) {
                    vga_write("Realtek RTL8139");
                } else if (vendor == 0x8086 && device == 0x100E) {
                    vga_write("Intel PRO/1000");
                } else {
                    vga_write("Unknown NIC");
                }
                vga_write("\n");
            }
        }
    }
}
