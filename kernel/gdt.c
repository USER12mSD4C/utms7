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

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr gp;

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

int gdt_init(void) {
    gp.limit = sizeof(gdt) - 1;
    gp.base = (u64)gdt;
    
    memset(gdt, 0, sizeof(gdt));
    
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xAF);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xAF);
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xAF);
    gdt_set_gate(5, 0, 0, 0, 0);
    
    __asm__ volatile ("lgdt %0" : : "m"(gp));
    
    return 0;
}
