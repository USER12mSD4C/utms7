#include "gdt.h"
#include "../include/io.h"

struct gdt_entry gdt[6];
struct gdt_ptr gp;

void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].base_upper = (base >> 32) & 0xFFFFFFFF;
    
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].limit_high = (limit >> 16) & 0x0F;
    
    gdt[num].access = access;
    gdt[num].flags = (gran >> 4) & 0x0F; 
    gdt[num].limit_high |= (gran & 0x0F);
    gdt[num].reserved = 0;
}

int gdt_init(void) {
    __asm__ volatile ("mov $'1', %%al; out %%al, $0xE9" : : : "al");
    
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gp.base = (u64)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xAF);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xAF);
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xAF);
    gdt_set_gate(5, 0, 0, 0, 0);

    __asm__ volatile ("mov $'2', %%al; out %%al, $0xE9" : : : "al");

    __asm__ volatile ("lgdt %0" : : "m"(gp));
    
    __asm__ volatile ("mov $'3', %%al; out %%al, $0xE9" : : : "al");
    
    // ВРЕМЕННО УБИРАЕМ ВСЁ
    // __asm__ volatile (
    //     "mov $0x10, %%ax\n"
    //     "mov %%ax, %%ds\n"
    //     "mov %%ax, %%es\n"
    //     "mov %%ax, %%fs\n"
    //     "mov %%ax, %%gs\n"
    //     "mov %%ax, %%ss\n"
    //     : : : "ax", "memory"
    // );
    
    __asm__ volatile ("mov $'4', %%al; out %%al, $0xE9" : : : "al");
    
    // __asm__ volatile (
    //     "pushq $0x08\n"
    //     "leaq 1f(%%rip), %%rax\n"
    //     "pushq %%rax\n"
    //     "lretq\n"
    //     "1:\n"
    //     : : : "rax", "memory"
    // );
    
    __asm__ volatile ("mov $'5', %%al; out %%al, $0xE9" : : : "al");
    
    return 0;
}
