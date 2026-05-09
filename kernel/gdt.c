#include "gdt.h"
#include "../include/string.h"
#include "../include/io.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

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
struct gdt_entry gdt[GDT_SIZE] __attribute__((aligned(16)));
struct tss kernel_tss;

void tss_set_rsp0(u64 rsp) {
    kernel_tss.rsp0 = rsp;
}

extern void gdt_flush(u64 gdt_ptr, u64 gdt_size);

static void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran) {
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].base_upper = (base >> 32) & 0xFFFFFFFF;
    gdt[num].access = access;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].reserved = 0;
}

static void gdt_set_tss(u32 num, u64 base, u32 limit) {
    gdt[num].limit_low    = limit & 0xFFFF;
    gdt[num].base_low     = base & 0xFFFF;
    gdt[num].base_middle  = (base >> 16) & 0xFF;
    gdt[num].access       = 0x89;
    gdt[num].granularity  = (limit >> 16) & 0x0F;
    gdt[num].base_high    = (base >> 24) & 0xFF;
    gdt[num].base_upper   = 0;
    gdt[num].reserved     = 0;

    gdt[num+1].limit_low   = 0;
    gdt[num+1].base_low    = 0;
    gdt[num+1].base_middle = 0;
    gdt[num+1].access      = 0;
    gdt[num+1].granularity = 0;
    gdt[num+1].base_high   = 0;
    gdt[num+1].base_upper  = (base >> 32) & 0xFFFFFFFF;
    gdt[num+1].reserved    = 0;
}

int gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iomap_base = sizeof(kernel_tss);

    gdt_set_gate(0, 0, 0,        0,    0);
    gdt_set_gate(1, 0, 0xFFFFF,  0x9A, 0xAF);  // kernel code
    gdt_set_gate(2, 0, 0xFFFFF,  0x92, 0xCF);  // kernel data
    gdt_set_gate(3, 0, 0xFFFFF,  0xFA, 0xAF);  // user code
    gdt_set_gate(4, 0, 0xFFFFF,  0xF2, 0xCF);  // user data
    gdt_set_tss(5, (u64)&kernel_tss, sizeof(kernel_tss) - 1);

    // Вызываем ассемблерную функцию, передавая указатель и размер
    gdt_flush((u64)gdt, sizeof(gdt));

    return 0;
}

void tss_init(void) {
    __asm__ volatile ("mov $0x28, %%ax; ltr %%ax" : : : "ax");
}
