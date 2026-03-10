#include "gdt.h"
#include "../include/io.h"

struct gdt_entry gdt[6];
struct gdt_ptr gp;

extern void gdt_flush(void);

void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].base_upper = (base >> 32) & 0xFFFFFFFF;
    
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].limit_high = (limit >> 16) & 0x0F;
    
    gdt[num].access = access;
    gdt[num].flags = (gran & 0xF0);
    gdt[num].limit_high |= (gran & 0x0F);
}

int gdt_init(void) {
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gp.base = (u64)&gdt;
    
    gdt_set_gate(0, 0, 0, 0, 0);                // Null
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);    // Code kernel
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xAF);    // Data kernel
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xAF);    // Code user
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xAF);    // Data user
    gdt_set_gate(5, 0, 0, 0, 0);                 // TSS
    
    gdt_flush();
    
    return 0;
}
