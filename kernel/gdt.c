// kernel/gdt.c
#include "gdt.h"
#include "../include/string.h"
#include "../include/io.h"

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

#define GDT_SIZE 7
static u64 gdt[GDT_SIZE] __attribute__((aligned(16)));
static struct tss kernel_tss;

extern void gdt_flush(u64 gdt_ptr, u64 gdt_size);

void tss_set_rsp0(u64 rsp) {
    kernel_tss.rsp0 = rsp;
}

int gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iomap_base = sizeof(kernel_tss);

    // 0: NULL
    gdt[0] = 0;

    // 1: Kernel Code (0x08)
    gdt[1] = 0x00af9a000000ffffULL;

    // 2: Kernel Data (0x10)
    gdt[2] = 0x00cf92000000ffffULL;

    // 3: User Code (0x18)
    gdt[3] = 0x00affa000000ffffULL;

    // 4: User Data (0x20)
    gdt[4] = 0x00cff2000000ffffULL;

    // 5-6: TSS (0x28)
    u64 base = (u64)&kernel_tss;
    u64 limit = sizeof(kernel_tss) - 1;
    gdt[5] = (limit & 0xFFFF)
           | ((base & 0xFFFF) << 16)
           | (((base >> 16) & 0xFF) << 32)
           | (0x89ULL << 40)
           | (((limit >> 16) & 0x0FULL) << 48)
           | (((base >> 24) & 0xFFULL) << 56);
    gdt[6] = (base >> 32);

    gdt_flush((u64)gdt, sizeof(gdt));

    return 0;
}

void tss_init(void) {
    __asm__ volatile ("mov $0x28, %%ax; ltr %%ax" : : : "ax");
}
