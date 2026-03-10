#include "idt.h"
#include "../include/io.h"
#include "../drivers/vga.h"
#include "gdt.h"
#include "panic.h"

extern void keyboard_handler(void);
extern void timer_handler(void);
extern void mouse_handler(void);
extern void double_fault_handler(void);

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;
u32 system_ticks = 0;

void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_mid = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags | 0x80;
    idt[num].zero = 0;
}

void timer_handler_c(void) {
    system_ticks++;
    outb(0x20, 0x20);
}

void double_fault_handler_c(void) {
    panic("DOUBLE FAULT");
}

int idt_init(void) {  // ВОЗВРАЩАЕТ int
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base = (u64)&idt[0];
    
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (u64)double_fault_handler, 0x08, 0x8E);
    }
    
    idt_set_gate(32, (u64)timer_handler, 0x08, 0x8E);
    idt_set_gate(33, (u64)keyboard_handler, 0x08, 0x8E);
    idt_set_gate(44, (u64)mouse_handler, 0x08, 0x8E);
    
    __asm__ volatile ("lidt %0" : : "m"(idtp));
    
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    outb(0x21, 0xFC);
    outb(0xA1, 0xEF);
    
    return 0;
}

int timer_init(void) {  // ВОЗВРАЩАЕТ int
    u32 freq = 100;
    u32 divisor = 1193180 / freq;
    
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    return 0;
}
