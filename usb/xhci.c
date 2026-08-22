// usb/xhci.c
#include "xhci.h"
#include "../drivers/drm.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../include/io.h"
#include "../drivers/pci.h"

static xhci_controller_t controllers[4];
static int num_controllers = 0;

static u64 xhci_read_bar(pci_device_t* dev, int bar_num) {
    u32 offset = 0x10 + (bar_num * 4);
    u32 lo = pci_read_config(dev->bus, dev->device, dev->function, offset);

    if ((lo & 0x6) == 0x4) {
        u32 hi = pci_read_config(dev->bus, dev->device, dev->function, offset + 4);
        return ((u64)hi << 32) | (lo & ~0xFFF);
    }

    return lo & ~0xFFF;
}

static int xhci_scan_pci(void) {
    int found = 0;

    for (int bus = 0; bus < 256 && found < 4; bus++) {
        for (int dev = 0; dev < 32 && found < 4; dev++) {
            for (int func = 0; func < 8; func++) {
                u32 vendor = pci_read_config(bus, dev, func, 0x00);
                if ((vendor & 0xFFFF) == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }

                u32 class_code = pci_read_config(bus, dev, func, 0x08);
                u8 class = (class_code >> 24) & 0xFF;
                u8 subclass = (class_code >> 16) & 0xFF;
                u8 prog_if = (class_code >> 8) & 0xFF;

                if (class == 0x0C && subclass == 0x03 && prog_if == 0x30) {
                    controllers[found].pci_dev.bus = bus;
                    controllers[found].pci_dev.device = dev;
                    controllers[found].pci_dev.function = func;
                    controllers[found].pci_dev.vendor_id = vendor & 0xFFFF;
                    controllers[found].pci_dev.device_id = (vendor >> 16) & 0xFFFF;
                    controllers[found].pci_dev.class_code = class;
                    controllers[found].pci_dev.subclass = subclass;
                    controllers[found].pci_dev.prog_if = prog_if;
                    found++;
                }

                if (func == 0) {
                    u32 header = pci_read_config(bus, dev, func, 0x0C);
                    if (!((header >> 16) & 0x80)) break;
                }
            }
        }
    }

    num_controllers = found;
    return found;
}

static int xhci_reset_controller(xhci_controller_t* ctrl) {
    ctrl->op_regs->usbcmd = 2;

    for (int i = 0; i < 10000000; i++) {
        if (!(ctrl->op_regs->usbcmd & 2)) return 0;
        __asm__ volatile ("pause");
    }

    return -1;
}

static int xhci_setup_dcbaa(xhci_controller_t* ctrl) {
    u32 max_slots = (ctrl->cap_regs->hcsparams1 & 0xFF) + 1;
    u32 dcbaa_size = (max_slots + 1) * 8;

    ctrl->dcbaa = (u64*)kmalloc_aligned(dcbaa_size, 64);
    if (!ctrl->dcbaa) return -1;

    memset(ctrl->dcbaa, 0, dcbaa_size);

    ctrl->op_regs->dcbaap_low = (u32)(u64)ctrl->dcbaa;
    ctrl->op_regs->dcbaap_high = (u32)((u64)ctrl->dcbaa >> 32);

    return 0;
}

static int xhci_setup_command_ring(xhci_controller_t* ctrl) {
    u32 ring_size = 4096;
    ctrl->command_ring = (u64*)kmalloc_aligned(ring_size, 64);
    if (!ctrl->command_ring) return -1;

    memset(ctrl->command_ring, 0, ring_size);

    u64 ring_addr = (u64)ctrl->command_ring;
    ctrl->op_regs->crcr_low = (u32)ring_addr | 1;
    ctrl->op_regs->crcr_high = (u32)(ring_addr >> 32);

    return 0;
}

static int xhci_setup_event_ring(xhci_controller_t* ctrl) {
    u32 max_interrupters = (ctrl->cap_regs->hcsparams1 >> 8) & 0x3FF;
    if (max_interrupters == 0) max_interrupters = 1;

    ctrl->rt_regs = (xhci_rt_regs_t*)((u8*)ctrl->op_regs + ctrl->cap_regs->rtsoff);

    u32 erstsz = 1;
    ctrl->rt_regs->erstsz = erstsz;

    u64* erst = (u64*)kmalloc_aligned(erstsz * 16, 64);
    if (!erst) return -1;

    memset(erst, 0, erstsz * 16);

    u32 event_ring_size = 4096;
    u64* event_ring = (u64*)kmalloc_aligned(event_ring_size, 64);
    if (!event_ring) {
        kfree(erst);
        return -1;
    }

    memset(event_ring, 0, event_ring_size);

    erst[0] = (u64)event_ring;
    erst[1] = (event_ring_size / 16);

    ctrl->rt_regs->erstba_low = (u32)(u64)erst;
    ctrl->rt_regs->erstba_high = (u32)((u64)erst >> 32);

    ctrl->event_ring = event_ring;

    return 0;
}

static int xhci_start_controller(xhci_controller_t* ctrl) {
    u32 max_slots = (ctrl->cap_regs->hcsparams1 & 0xFF) + 1;
    ctrl->op_regs->config = max_slots;

    ctrl->op_regs->usbcmd = 1;

    for (int i = 0; i < 10000000; i++) {
        if (ctrl->op_regs->usbsts & 1) return 0;
        __asm__ volatile ("pause");
    }

    return -1;
}

static int xhci_init_controller(xhci_controller_t* ctrl) {
    u64 mmio_base = xhci_read_bar(&ctrl->pci_dev, 0);
    if (mmio_base == 0) return -1;

    ctrl->cap_regs = (xhci_cap_regs_t*)mmio_base;
    ctrl->op_regs = (xhci_op_regs_t*)(mmio_base + ctrl->cap_regs->caplength);

    u32 num_ports = (ctrl->cap_regs->hcsparams1 >> 24) & 0xFF;
    ctrl->num_ports = num_ports;
    ctrl->port_regs = (xhci_port_regs_t*)((u8*)ctrl->op_regs + 0x400);

    ctrl->page_size = ctrl->op_regs->pagesize * 4096;

    print("  BAR0: ");
    print_hex(mmio_base);
    print(", Ports: ");
    print_num(num_ports);
    print(", Page: ");
    print_num(ctrl->page_size);
    print("\n");

    if (xhci_reset_controller(ctrl) != 0) {
        print("  reset FAILED\n");
        return -1;
    }

    if (xhci_setup_dcbaa(ctrl) != 0) {
        print("  DCBAA FAILED\n");
        return -1;
    }

    if (xhci_setup_command_ring(ctrl) != 0) {
        print("  Command Ring FAILED\n");
        return -1;
    }

    if (xhci_setup_event_ring(ctrl) != 0) {
        print("  Event Ring FAILED\n");
        return -1;
    }

    if (xhci_start_controller(ctrl) != 0) {
        print("  Start FAILED\n");
        return -1;
    }

    return 0;
}

int xhci_init(void) {
    print("Initializing XHCI... ");

    int found = xhci_scan_pci();
    if (found == 0) {
        print("no controllers found\n");
        return 0;
    }

    print("found ");
    print_num(found);
    print(" controllers\n");

    for (int i = 0; i < num_controllers; i++) {
        print("Controller ");
        print_num(i);
        print(": ");
        print_hex(controllers[i].pci_dev.vendor_id);
        print(":");
        print_hex(controllers[i].pci_dev.device_id);
        print("\n");

        if (xhci_init_controller(&controllers[i]) != 0) {
            print("  init FAILED\n");
        } else {
            print("  init OK\n");
        }
    }

    return 0;
}

int xhci_handle_device(int port) {
    (void)port;
    return -1;
}
