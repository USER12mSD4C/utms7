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

#define GDT_SIZE 8
static u64 gdt[GDT_SIZE] __attribute__((aligned(16)));
static struct tss kernel_tss;
static u8 double_fault_stack[8192] __attribute__((aligned(16)));

u64 kernel_stack_temp = 0;
u64 user_stack_temp = 0;

extern void gdt_flush(u64 gdt_ptr, u64 gdt_size);

void tss_set_rsp0(u64 rsp) {
    kernel_tss.rsp0 = rsp;
    kernel_stack_temp = rsp;
}

int gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iomap_base = sizeof(kernel_tss);
    kernel_tss.ist[0] = (u64)double_fault_stack + sizeof(double_fault_stack);

    // 0: NULL
    gdt[0] = 0;

    // 1: Kernel Code (0x08)
    gdt[1] = 0x00af9a000000ffffULL;

    // 2: Kernel Data (0x10)
    gdt[2] = 0x00cf92000000ffffULL;

    // 3: Dummy User 32-bit Code (0x18)
    gdt[3] = 0;

    // 4: User Data (0x20)
    gdt[4] = 0x00cff2000000ffffULL;

    // 5: User Code (0x28)
    gdt[5] = 0x00affa000000ffffULL;

    // 6-7: TSS (0x30)
    u64 base = (u64)&kernel_tss;
    u64 limit = sizeof(kernel_tss) - 1;
    gdt[6] = (limit & 0xFFFF)
           | ((base & 0xFFFF) << 16)
           | (((base >> 16) & 0xFF) << 32)
           | (0x89ULL << 40)
           | (((limit >> 16) & 0x0FULL) << 48)
           | (((base >> 24) & 0xFFULL) << 56);
    gdt[7] = (base >> 32);

    gdt_flush((u64)gdt, sizeof(gdt));

    u64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);
    cr4 |= (1ULL << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    return 0;
}

void tss_init(void) {
    __asm__ volatile ("mov $0x30, %%ax; ltr %%ax" : : : "ax");
}
