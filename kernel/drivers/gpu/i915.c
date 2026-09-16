#include "i915.h"
#include "../../core/memory.h"
#include "../../arch/x86_64/paging.h"
#include "../../../include/print.h"
#include "../../../include/io.h"
#include "../../../include/string.h"
#include "drm.h"

#define INTEL_VENDOR 0x8086
#define I915_MMIO_SIZE 0x400000

#define PCH_PP_STATUS    0xC7200
#define PCH_PP_CONTROL   0xC7204
#define PIPEACONF        0x70008
#define PIPEASRC         0x6001C
#define DSPASURF         0x7019C
#define DSPASTRIDE       0x70188
#define DSPACNTR         0x70180
#define GTT_SIZE         (4 * 1024 * 1024)
#define STOLEN_MEM_SIZE  (32 * 1024 * 1024)

typedef struct {
    u32 mmio_phys;
    void* mmio_virt;
    u32 gtt_phys;
    void* gtt_virt;
    u64 stolen_base;
    u64 stolen_size;
    u64 stolen_cursor;
    int initialized;
    pci_dev_t* pci;
} i915_softc_t;

typedef struct {
    u64 handle;
    u64 phys_addr;
    void* virt_addr;
    u32 size;
    int used;
} i915_gem_obj_t;

#define MAX_GEM_OBJECTS 64
static i915_gem_obj_t gem_objects[MAX_GEM_OBJECTS];
static i915_softc_t sc;
static u64 next_handle = 1;

static inline u32 i915_read32(u32 reg) {
    return *(volatile u32*)((u64)sc.mmio_virt + reg);
}

static inline void i915_write32(u32 reg, u32 val) {
    *(volatile u32*)((u64)sc.mmio_virt + reg) = val;
}

static void i915_read_stolen_info(void) {
    u32 bgsm = pci_read_config(sc.pci->bus, sc.pci->slot, sc.pci->func, 0xB0);
    u32 bdsm = pci_read_config(sc.pci->bus, sc.pci->slot, sc.pci->func, 0xB4);
    sc.stolen_base = (u64)(bdsm & 0xFFF00000);
    sc.stolen_size = (u64)((bgsm & 0xFFF00000) - (bdsm & 0xFFF00000));
    if (sc.stolen_size > STOLEN_MEM_SIZE) sc.stolen_size = STOLEN_MEM_SIZE;
    sc.stolen_cursor = 0;
}

static void i915_setup_gtt(void) {
    u32 gtt_base = pci_read_config(sc.pci->bus, sc.pci->slot, sc.pci->func, 0x2C);
    sc.gtt_phys = gtt_base & 0xFFF00000;
    for (u64 off = 0; off < GTT_SIZE; off += 0x200000) {
        if (paging_map(sc.gtt_phys + off, sc.gtt_phys + off,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE) != 0) {
            for (u64 k = 0; k < 0x200000; k += 4096) {
                paging_map(sc.gtt_phys + off + k, sc.gtt_phys + off + k,
                           PAGE_PRESENT | PAGE_WRITABLE);
            }
        }
    }
    sc.gtt_virt = (void*)(u64)sc.gtt_phys;
}

u64 i915_gem_create(u32 size) {
    size = (size + 4095) & ~4095;
    int slot = -1;
    for (int i = 0; i < MAX_GEM_OBJECTS; i++) {
        if (!gem_objects[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;
    u64 phys = 0;
    void* virt = NULL;
    if (sc.stolen_cursor + size <= sc.stolen_size) {
        phys = sc.stolen_base + sc.stolen_cursor;
        sc.stolen_cursor += size;
        virt = (void*)phys;
        for (u64 off = 0; off < size; off += 0x200000) {
            if (paging_map(phys + off, phys + off,
                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE) != 0) {
                for (u64 k = 0; k < 0x200000 && (off + k) < size; k += 4096) {
                    paging_map(phys + off + k, phys + off + k,
                               PAGE_PRESENT | PAGE_WRITABLE);
                }
            }
        }
    } else {
        virt = kmalloc(size);
        if (!virt) return 0;
        phys = (u64)virt;
    }
    gem_objects[slot].handle = next_handle++;
    gem_objects[slot].phys_addr = phys;
    gem_objects[slot].virt_addr = virt;
    gem_objects[slot].size = size;
    gem_objects[slot].used = 1;
    memset(virt, 0, size);
    return gem_objects[slot].handle;
}

void i915_gem_destroy(u64 handle) {
    for (int i = 0; i < MAX_GEM_OBJECTS; i++) {
        if (gem_objects[i].used && gem_objects[i].handle == handle) {
            if (gem_objects[i].phys_addr < sc.stolen_base ||
                gem_objects[i].phys_addr >= sc.stolen_base + sc.stolen_size) {
                kfree(gem_objects[i].virt_addr);
            }
            gem_objects[i].used = 0;
            return;
        }
    }
}

void* i915_gem_map(u64 handle) {
    for (int i = 0; i < MAX_GEM_OBJECTS; i++) {
        if (gem_objects[i].used && gem_objects[i].handle == handle) {
            return gem_objects[i].virt_addr;
        }
    }
    return NULL;
}

int i915_present(void) {
    return sc.initialized;
}

int i915_init(pci_dev_t* dev) {
    if (!dev || dev->vendor_id != INTEL_VENDOR) return -1;
    if (dev->class_code != 0x03) return -1;
    sc.pci = dev;
    sc.mmio_phys = dev->bar[0] & 0xFFFFFFF0;
    if (sc.mmio_phys == 0) return -1;
    for (u64 off = 0; off < I915_MMIO_SIZE; off += 0x200000) {
        if (paging_map(sc.mmio_phys + off, sc.mmio_phys + off,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE) != 0) {
            for (u64 k = 0; k < 0x200000; k += 4096) {
                paging_map(sc.mmio_phys + off + k, sc.mmio_phys + off + k,
                           PAGE_PRESENT | PAGE_WRITABLE);
            }
        }
    }
    sc.mmio_virt = (void*)(u64)sc.mmio_phys;
    u32 cmd = pci_read_config(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x07;
    pci_write_config(dev->bus, dev->slot, dev->func, 0x04, cmd);
    i915_read_stolen_info();
    i915_setup_gtt();
    print("  i915: MMIO 0x");
    printhex(sc.mmio_phys);
    print(", Stolen ");
    printnum(sc.stolen_size / (1024 * 1024));
    print("MB, Device 0x");
    printhex(dev->device_id);
    print("\n");
    for (int i = 0; i < MAX_GEM_OBJECTS; i++) gem_objects[i].used = 0;
    u32 pp_ctl = i915_read32(PCH_PP_CONTROL);
    pp_ctl |= 0x01;
    i915_write32(PCH_PP_CONTROL, pp_ctl);
    sc.initialized = 1;
    return 0;
}
