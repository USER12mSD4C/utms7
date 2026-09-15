#include "types.h"
#include "pci.h"

#define LC_MAX_DEVICES 32

static struct pci_dev lc_devs[LC_MAX_DEVICES];
static struct pci_driver *lc_bound[LC_MAX_DEVICES];
static int lc_dev_count;
static int lc_enumerated;

static inline u32 lc_inl(u16 port) {
    u32 v;
    __asm__ volatile("in %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void lc_outl(u16 port, u32 v) {
    __asm__ volatile("out %0, %1" : : "a"(v), "Nd"(port));
}

static u32 lc_cfg_read(u8 bus, u8 devfn, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)devfn << 8) | (u32)(off & 0xFC);
    lc_outl(0xCF8, addr);
    return lc_inl(0xCFC);
}

static void lc_cfg_write(u8 bus, u8 devfn, u8 off, u32 v) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)devfn << 8) | (u32)(off & 0xFC);
    lc_outl(0xCF8, addr);
    lc_outl(0xCFC, v);
}

u8 pci_read_config_byte(struct pci_dev *dev, int where) {
    u32 v = lc_cfg_read(dev->bus, dev->devfn, (u8)(where & ~3));
    return (u8)(v >> ((where & 3) * 8));
}

u16 pci_read_config_word(struct pci_dev *dev, int where) {
    u32 v = lc_cfg_read(dev->bus, dev->devfn, (u8)(where & ~3));
    return (u16)(v >> ((where & 2) * 8));
}

u32 pci_read_config_dword(struct pci_dev *dev, int where) {
    return lc_cfg_read(dev->bus, dev->devfn, (u8)(where & ~3));
}

void pci_write_config_byte(struct pci_dev *dev, int where, u8 val) {
    u8 off = (u8)(where & ~3);
    u32 shift = (u32)(where & 3) * 8;
    u32 v = lc_cfg_read(dev->bus, dev->devfn, off);
    v &= ~(0xFFu << shift);
    v |= (u32)val << shift;
    lc_cfg_write(dev->bus, dev->devfn, off, v);
}

void pci_write_config_word(struct pci_dev *dev, int where, u16 val) {
    u8 off = (u8)(where & ~3);
    u32 shift = (u32)(where & 2) * 8;
    u32 v = lc_cfg_read(dev->bus, dev->devfn, off);
    v &= ~(0xFFFFu << shift);
    v |= (u32)val << shift;
    lc_cfg_write(dev->bus, dev->devfn, off, v);
}

void pci_write_config_dword(struct pci_dev *dev, int where, u32 val) {
    lc_cfg_write(dev->bus, dev->devfn, (u8)(where & ~3), val);
}

static void lc_parse_bars(struct pci_dev *dev) {
    for (int i = 0; i < 6; i++) {
        dev->resource[i] = 0;
        dev->resource_len[i] = 0;
        dev->resource_flags[i] = 0;

        u32 bar = pci_read_config_dword(dev, PCI_BASE_ADDRESS_0 + i * 4);
        if (bar == 0) continue;

        int is_io = (bar & PCI_BASE_ADDRESS_SPACE_IO) != 0;
        int is_64 = (!is_io) && ((bar & PCI_BASE_ADDRESS_MEM_TYPE_64) == PCI_BASE_ADDRESS_MEM_TYPE_64);

        pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + i * 4, 0xFFFFFFFFu);
        u32 mask = pci_read_config_dword(dev, PCI_BASE_ADDRESS_0 + i * 4);
        pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + i * 4, bar);

        if (is_io) {
            u32 m = mask & (u32)PCI_BASE_ADDRESS_IO_MASK;
            dev->resource[i] = bar & (u32)PCI_BASE_ADDRESS_IO_MASK;
            dev->resource_len[i] = m ? (u64)(~m + 1) : 0;
            dev->resource_flags[i] = IORESOURCE_IO;
            continue;
        }

        int idx = i;
        u64 base = bar & (u32)PCI_BASE_ADDRESS_MEM_MASK;
        u64 m = mask & (u32)PCI_BASE_ADDRESS_MEM_MASK;

        if (is_64 && i < 5) {
            int hi_idx = i + 1;
            u32 hi = pci_read_config_dword(dev, PCI_BASE_ADDRESS_0 + hi_idx * 4);
            pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + hi_idx * 4, 0xFFFFFFFFu);
            u32 mask_hi = pci_read_config_dword(dev, PCI_BASE_ADDRESS_0 + hi_idx * 4);
            pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + hi_idx * 4, hi);
            base |= (u64)hi << 32;
            m |= (u64)mask_hi << 32;
            dev->resource[hi_idx] = 0;
            dev->resource_len[hi_idx] = 0;
            dev->resource_flags[hi_idx] = 0;
            i = hi_idx;
        }

        dev->resource[idx] = base;
        dev->resource_len[idx] = m ? (~m + 1) : 0;
        dev->resource_flags[idx] = IORESOURCE_MEM |
            ((bar & PCI_BASE_ADDRESS_MEM_PREFETCH) ? IORESOURCE_PREFETCH : 0);
    }
}

static void lc_enumerate(void) {
    if (lc_enumerated) return;
    lc_enumerated = 1;

    for (u32 bus = 0; bus < 256 && lc_dev_count < LC_MAX_DEVICES; bus++) {
        for (u32 d = 0; d < 32 && lc_dev_count < LC_MAX_DEVICES; d++) {
            for (u32 f = 0; f < 8 && lc_dev_count < LC_MAX_DEVICES; f++) {
                u8 devfn = (u8)PCI_DEVFN(d, f);
                u32 id = lc_cfg_read((u8)bus, devfn, PCI_VENDOR_ID);
                u16 vendor = (u16)(id & 0xFFFF);

                if (vendor == 0xFFFF) {
                    if (f == 0) break;
                    continue;
                }

                struct pci_dev *dev = &lc_devs[lc_dev_count++];
                dev->vendor = vendor;
                dev->device = (u16)(id >> 16);
                dev->bus = (u8)bus;
                dev->devfn = devfn;
                dev->driver_data = NULL;

                u32 cd = lc_cfg_read((u8)bus, devfn, 0x08);
                dev->class = (cd >> 8) & 0x00FFFFFFu;
                dev->irq = (u8)(lc_cfg_read((u8)bus, devfn, PCI_INTERRUPT_LINE) & 0xFF);

                lc_parse_bars(dev);

                u32 hdr = lc_cfg_read((u8)bus, devfn, PCI_HEADER_TYPE);
                if (f == 0 && !(hdr & 0x80)) break;
            }
        }
    }
}

static int lc_match(struct pci_dev *dev, const struct pci_device_id *id) {
    if (id->vendor != PCI_ANY_ID && id->vendor != dev->vendor) return 0;
    if (id->device != PCI_ANY_ID && id->device != dev->device) return 0;

    if (id->subvendor != PCI_ANY_ID || id->subdevice != PCI_ANY_ID) {
        u32 ss = pci_read_config_dword(dev, PCI_SUBSYSTEM_VENDOR_ID);
        if (id->subvendor != PCI_ANY_ID && id->subvendor != (u16)(ss & 0xFFFF)) return 0;
        if (id->subdevice != PCI_ANY_ID && id->subdevice != (u16)(ss >> 16)) return 0;
    }

    if (id->class != PCI_ANY_ID) {
        u32 mask = id->class_mask ? id->class_mask : 0xFFFFFFu;
        if ((dev->class & mask) != (id->class & mask)) return 0;
    }

    return 1;
}

int pci_register_driver(struct pci_driver *drv) {
    if (!drv || !drv->probe || !drv->id_table) return -1;
    lc_enumerate();

    for (int i = 0; i < lc_dev_count; i++) {
        if (lc_bound[i]) continue;
        for (const struct pci_device_id *id = drv->id_table;
             id->vendor != 0 || id->device != 0; id++) {
            if (lc_match(&lc_devs[i], id)) {
                lc_bound[i] = drv;
                if (drv->probe(&lc_devs[i], id) != 0) lc_bound[i] = NULL;
                break;
            }
        }
    }
    return 0;
}

void pci_unregister_driver(struct pci_driver *drv) {
    for (int i = 0; i < lc_dev_count; i++) {
        if (lc_bound[i] == drv) {
            if (drv->remove) drv->remove(&lc_devs[i]);
            lc_bound[i] = NULL;
        }
    }
}

int pci_enable_device(struct pci_dev *dev) {
    u16 cmd = pci_read_config_word(dev, PCI_COMMAND);
    cmd |= (u16)(PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
    pci_write_config_word(dev, PCI_COMMAND, cmd);
    return 0;
}

void pci_disable_device(struct pci_dev *dev) {
    u16 cmd = pci_read_config_word(dev, PCI_COMMAND);
    cmd &= (u16)~(u16)(PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
    pci_write_config_word(dev, PCI_COMMAND, cmd);
}

void pci_set_master(struct pci_dev *dev) {
    u16 cmd = pci_read_config_word(dev, PCI_COMMAND);
    cmd |= (u16)PCI_COMMAND_MASTER;
    pci_write_config_word(dev, PCI_COMMAND, cmd);
}

const char *pci_name(const struct pci_dev *dev) {
    static const char hex[] = "0123456789abcdef";
    static char buf[9];
    u8 slot = (u8)PCI_SLOT(dev->devfn);
    buf[0] = hex[(dev->bus >> 4) & 0xF];
    buf[1] = hex[dev->bus & 0xF];
    buf[2] = ':';
    buf[3] = hex[(slot >> 4) & 0xF];
    buf[4] = hex[slot & 0xF];
    buf[5] = '.';
    buf[6] = hex[dev->devfn & 7];
    buf[7] = 0;
    return buf;
}
