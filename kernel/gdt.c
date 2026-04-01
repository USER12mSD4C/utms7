#include "gdt.h"
#include "../include/string.h"
#include "../include/io.h"
#include "../drivers/vga.h"

static struct gdt_page gp;
static struct gdt_ptr gp_ptr;

static void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran) {
    if (num >= GDT_ENTRIES) {
        return;
    }
    
    gp.entries[num].limit_low = (limit & 0xFFFF);
    gp.entries[num].base_low = (base & 0xFFFF);
    gp.entries[num].base_middle = (base >> 16) & 0xFF;
    gp.entries[num].base_high = (base >> 24) & 0xFF;
    gp.entries[num].base_upper = (base >> 32) & 0xFFFFFFFF;
    gp.entries[num].access = access;
    gp.entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gp.entries[num].reserved = 0;
}

static int gdt_verify(void) {
    struct gdt_ptr verify;
    
    __asm__ volatile ("sgdt %0" : "=m"(verify));
    
    if (verify.limit != gp_ptr.limit) {
        return -1;
    }
    
    if (verify.base != gp_ptr.base) {
        return -1;
    }
    
    return 0;
}

int gdt_init(void) {
    memset(&gp, 0, sizeof(gp));
    
    gp_ptr.limit = sizeof(gp.entries) - 1;
    gp_ptr.base = (u64)&gp.entries;
    
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xAF);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xAF);
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xAF);
    gdt_set_gate(5, 0, 0, 0, 0);
    
    __asm__ volatile ("lgdt %0" : : "m"(gp_ptr));
    
    if (gdt_verify() != 0) {
        return -1;
    }
    
    __asm__ volatile (
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "r"((u16)__KERNEL_DS) : "ax", "memory"
    );
    
    __asm__ volatile (
        "pushq %0\n"
        "pushq $1f\n"
        "lretq\n"
        "1:\n"
        : : "i"((u16)__KERNEL_CS) : "memory"
    );
    
    if (gdt_verify() != 0) {
        return -1;
    }
    
    return 0;
}

void gdt_reload_segments(void) {
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "ax", "memory"
    );
    
    __asm__ volatile (
        "pushq $0x08\n"
        "pushq $1f\n"
        "lretq\n"
        "1:\n"
        : : : "memory"
    );
}

void gdt_set_tss(u64 base, u32 limit) {
    if (limit > 0xFFFFF) {
        return;
    }
    
    gdt_set_gate(5, base, limit, 0x89, 0x00);
    
    __asm__ volatile ("ltr %w0" : : "r"((u16)GDT_TSS_ENTRY));
}

void gdt_get_info(u16 *limit, u64 *base) {
    struct gdt_ptr ptr;
    
    __asm__ volatile ("sgdt %0" : "=m"(ptr));
    
    if (limit) {
        *limit = ptr.limit;
    }
    if (base) {
        *base = ptr.base;
    }
}
