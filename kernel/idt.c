#include "idt.h"
#include "../include/io.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../net/rtl8139.h"
#include "../net/e1000.h"

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;
u32 system_ticks = 0;

static u32 timer_ticks = 0;
static u32 seconds = 0;

static void irq0_handler(void) {
    timer_ticks++;
    system_ticks = timer_ticks;
    if (timer_ticks % 100 == 0) seconds++;
    outb(0x20, 0x20);
}

static void irq1_handler(void) {
    keyboard_handler_c();
    outb(0x20, 0x20);
}

static void irq12_handler(void) {
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

static void irq11_handler(void) {
    if (rtl8139_present()) rtl8139_handle_irq();
    if (e1000_present()) e1000_handle_irq();
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

static void default_handler(void) {
    outb(0x20, 0x20);
}

void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_mid = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags | 0x80;
    idt[num].zero = 0;
}

int idt_init(void) {
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base = (u64)&idt[0];
    
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, (u64)default_handler, 0x08, 0x8E);
    }
    
    idt_set_gate(32, (u64)irq0_handler, 0x08, 0x8E);
    idt_set_gate(33, (u64)irq1_handler, 0x08, 0x8E);
    idt_set_gate(44, (u64)irq12_handler, 0x08, 0x8E);
    idt_set_gate(43, (u64)irq11_handler, 0x08, 0x8E);
    
    __asm__ volatile ("lidt %0" : : "m"(idtp));
    
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    outb(0x21, ~(0x02 | 0x08));
    outb(0xA1, 0xFF);
    
    return 0;
}

int timer_init(void) {
    u32 divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    return 0;
}

u32 get_seconds(void) { return seconds; }
u32 get_ticks(void) { return timer_ticks; }
