#ifndef LINUX_COMPAT_PCI_H
#define LINUX_COMPAT_PCI_H

#include "types.h"
#include "io.h"

#define PCI_ANY_ID (~0U)

#define PCI_BASE_ADDRESS_0 0x10
#define PCI_BASE_ADDRESS_1 0x14
#define PCI_BASE_ADDRESS_2 0x18
#define PCI_BASE_ADDRESS_3 0x1c
#define PCI_BASE_ADDRESS_4 0x20
#define PCI_BASE_ADDRESS_5 0x24

#define PCI_BASE_ADDRESS_MEM_TYPE_64 0x04
#define PCI_BASE_ADDRESS_MEM_PREFETCH 0x08
#define PCI_BASE_ADDRESS_SPACE 0x01
#define PCI_BASE_ADDRESS_SPACE_IO 0x01
#define PCI_BASE_ADDRESS_MEM_MASK ~0x0fULL
#define PCI_BASE_ADDRESS_IO_MASK ~0x03ULL

#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_REVISION_ID 0x08
#define PCI_CLASS_DEVICE 0x0a
#define PCI_HEADER_TYPE 0x0e
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c
#define PCI_SUBSYSTEM_ID 0x2e
#define PCI_INTERRUPT_LINE 0x3c
#define PCI_INTERRUPT_PIN 0x3d

#define PCI_COMMAND_IO 0x01
#define PCI_COMMAND_MEMORY 0x02
#define PCI_COMMAND_MASTER 0x04

#define IORESOURCE_IO       0x00000100
#define IORESOURCE_MEM      0x00000200
#define IORESOURCE_PREFETCH 0x00001000

#define PCI_DEVFN(d, f) ((((u8)(d)) << 3) | ((u8)(f)))
#define PCI_SLOT(df) ((df) >> 3)
#define PCI_FUNC(df) ((df) & 7)

struct pci_device_id {
    u32 vendor, device;
    u32 subvendor, subdevice;
    u32 class, class_mask;
    unsigned long driver_data;
};

struct pci_dev {
    u16 vendor;
    u16 device;
    u8 bus, devfn;
    u64 resource[6];
    u64 resource_len[6];
    u64 resource_flags[6];
    u32 class;
    u8 irq;
    void *driver_data;
};

struct pci_driver {
    const char *name;
    const struct pci_device_id *id_table;
    int (*probe)(struct pci_dev *dev, const struct pci_device_id *id);
    void (*remove)(struct pci_dev *dev);
};

int pci_register_driver(struct pci_driver *drv);
void pci_unregister_driver(struct pci_driver *drv);

u8 pci_read_config_byte(struct pci_dev *dev, int where);
u16 pci_read_config_word(struct pci_dev *dev, int where);
u32 pci_read_config_dword(struct pci_dev *dev, int where);
void pci_write_config_byte(struct pci_dev *dev, int where, u8 val);
void pci_write_config_word(struct pci_dev *dev, int where, u16 val);
void pci_write_config_dword(struct pci_dev *dev, int where, u32 val);

int pci_enable_device(struct pci_dev *dev);
void pci_disable_device(struct pci_dev *dev);
void pci_set_master(struct pci_dev *dev);

const char *pci_name(const struct pci_dev *dev);

#define pci_resource_start(dev, bar) ((dev)->resource[bar])
#define pci_resource_len(dev, bar) ((dev)->resource_len[bar])
#define pci_resource_flags(dev, bar) ((dev)->resource_flags[bar])

static inline void *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen) {
    (void)maxlen;
    return ioremap(pci_resource_start(dev, bar), pci_resource_len(dev, bar));
}

#endif
