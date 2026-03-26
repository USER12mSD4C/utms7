#include "gdt.h"
#include "../include/io.h"
#include "../drivers/vga.h"

struct gdt_entry gdt[6];
struct gdt_ptr gp;

extern void gdt_flush(u64);

void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].base_upper = (base >> 32) & 0xFFFFFFFF;
    
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].limit_high = (limit >> 16) & 0x0F;
    
    gdt[num].access = access;
    gdt[num].flags = (gran & 0xF0) >> 4;
    gdt[num].limit_high |= (gran & 0x0F);
}

static void gdt_flush(void) {
    __asm__ volatile (
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        : : "m"(gp) : "rax", "memory"
    );
}

int gdt_init(void) {
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gp.base = (u64)&gdt;
    
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xAF);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xAF);
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xAF);
    gdt_set_gate(5, 0, 0, 0, 0);
    
    gdt_flush();
    return 0;
}
