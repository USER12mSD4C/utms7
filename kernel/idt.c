#include "idt.h"
#include "../include/io.h"
#include "../drivers/vga.h"
#include "gdt.h"
#include "panic.h"
#include "../include/string.h"
#include "../drivers/keyboard.h"

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;
u32 system_ticks = 0;

static u32 timer_ticks = 0;
static u32 seconds = 0;

extern void rtl8139_handle_irq(void);
extern int rtl8139_present(void);

static void* irq_handlers[16] = {0};

void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_mid = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags | 0x80;
    idt[num].zero = 0;
}

void irq_register(int irq, void* handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_handler(int irq) {
    switch(irq) {
        case 1:
            keyboard_handler_c();
            break;
        case 11:
            if (rtl8139_present()) {
                rtl8139_handle_irq();
            }
            break;
    }
    
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static void exception_handler(int num) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Exception %d", num);
    panic(buf);
}

extern void irq_wrapper_0(void);
extern void irq_wrapper_1(void);
extern void irq_wrapper_2(void);
extern void irq_wrapper_3(void);
extern void irq_wrapper_4(void);
extern void irq_wrapper_5(void);
extern void irq_wrapper_6(void);
extern void irq_wrapper_7(void);
extern void irq_wrapper_8(void);
extern void irq_wrapper_9(void);
extern void irq_wrapper_10(void);
extern void irq_wrapper_11(void);
extern void irq_wrapper_12(void);
extern void irq_wrapper_13(void);
extern void irq_wrapper_14(void);
extern void irq_wrapper_15(void);

int idt_init(void) {
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base = (u64)&idt[0];
    
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (u64)exception_handler, 0x08, 0x8E);
    }
    
    idt_set_gate(32, (u64)irq_wrapper_0, 0x08, 0x8E);  // PIT
    idt_set_gate(33, (u64)irq_wrapper_1, 0x08, 0x8E);  // Keyboard
    idt_set_gate(44, (u64)irq_wrapper_12, 0x08, 0x8E); // Mouse
    
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
    
    irq_register(1, keyboard_handler_c);
    
    return 0;
}

int timer_init(void) {
    u32 freq = 100;
    u32 divisor = 1193180 / freq;
    
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    return 0;
}

void timer_handler_c(void) {
    timer_ticks++;
    system_ticks = timer_ticks;
    
    if (timer_ticks % 100 == 0) {
        seconds++;
    }
}

u32 get_seconds(void) {
    return seconds;
}

u32 get_ticks(void) {
    return timer_ticks;
}

void double_fault_handler_c(void) {
    panic("DOUBLE FAULT");
}
