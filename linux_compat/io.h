#ifndef LINUX_COMPAT_IO_H
#define LINUX_COMPAT_IO_H

#include "types.h"
#include "../kernel/arch/x86_64/paging.h"

static inline void __iomem *ioremap(u64 phys, size_t size) {
    u64 pages = (size + 4095) / 4096;
    for (u64 i = 0; i < pages; i++) {
        paging_map(phys + i * 4096, phys + i * 4096, PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE);
    }
    return (void __iomem *)phys;
}

static inline void iounmap(void __iomem *addr) {
    (void)addr;
}

static inline u8 readb(const volatile void __iomem *addr) { return *(volatile u8*)addr; }
static inline u16 readw(const volatile void __iomem *addr) { return *(volatile u16*)addr; }
static inline u32 readl(const volatile void __iomem *addr) { return *(volatile u32*)addr; }
static inline u64 readq(const volatile void __iomem *addr) { return *(volatile u64*)addr; }

static inline void writeb(u8 val, volatile void __iomem *addr) { *(volatile u8*)addr = val; }
static inline void writew(u16 val, volatile void __iomem *addr) { *(volatile u16*)addr = val; }
static inline void writel(u32 val, volatile void __iomem *addr) { *(volatile u32*)addr = val; }
static inline void writeq(u64 val, volatile void __iomem *addr) { *(volatile u64*)addr = val; }

#define ioread8(addr) readb(addr)
#define ioread16(addr) readw(addr)
#define ioread32(addr) readl(addr)
#define iowrite8(val, addr) writeb(val, addr)
#define iowrite16(val, addr) writew(val, addr)
#define iowrite32(val, addr) writel(val, addr)

#endif
