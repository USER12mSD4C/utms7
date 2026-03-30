#include "idt.h"
#include "../include/io.h"
#include "../drivers/vga.h"
#include "panic.h"
#include "../include/string.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../net/e1000.h"
#include "../net/rtl8139.h"
#include "sched.h"

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;
u32 system_ticks = 0;

extern void isr_wrapper0(void);
extern void isr_wrapper1(void);
extern void isr_wrapper2(void);
extern void isr_wrapper3(void);
extern void isr_wrapper4(void);
extern void isr_wrapper5(void);
extern void isr_wrapper6(void);
extern void isr_wrapper7(void);
extern void isr_wrapper8(void);
extern void isr_wrapper9(void);
extern void isr_wrapper10(void);
extern void isr_wrapper11(void);
extern void isr_wrapper12(void);
extern void isr_wrapper13(void);
extern void isr_wrapper14(void);
extern void isr_wrapper15(void);
extern void isr_wrapper16(void);
extern void isr_wrapper17(void);
extern void isr_wrapper18(void);
extern void isr_wrapper19(void);
extern void isr_wrapper20(void);
extern void isr_wrapper21(void);
extern void isr_wrapper22(void);
extern void isr_wrapper23(void);
extern void isr_wrapper24(void);
extern void isr_wrapper25(void);
extern void isr_wrapper26(void);
extern void isr_wrapper27(void);
extern void isr_wrapper28(void);
extern void isr_wrapper29(void);
extern void isr_wrapper30(void);
extern void isr_wrapper31(void);

extern void irq0_handler_asm(void);
extern void irq1_handler_asm(void);
extern void irq2_handler_asm(void);
extern void irq3_handler_asm(void);
extern void irq4_handler_asm(void);
extern void irq5_handler_asm(void);
extern void irq6_handler_asm(void);
extern void irq7_handler_asm(void);
extern void irq8_handler_asm(void);
extern void irq9_handler_asm(void);
extern void irq10_handler_asm(void);
extern void irq11_handler_asm(void);
extern void irq12_handler_asm(void);
extern void irq13_handler_asm(void);
extern void irq14_handler_asm(void);
extern void irq15_handler_asm(void);

void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_mid = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].zero = 0;
}

void exception_handler_c(int num, int err) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Exception %d (Err: %d)", num, err);
    panic(buf);
}

void irq0_handler_c(void) {
    system_ticks++;
    if (sched_current()) {
        sched_tick();
    }
}

void irq1_handler_c(void) {
    keyboard_handler_c();
}

void irq11_handler_c(void) {
    if (e1000_present()) e1000_handle_irq();
    if (rtl8139_present()) rtl8139_handle_irq();
}

void irq12_handler_c(void) {
    mouse_handler_c();
}

int idt_init(void) {
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base = (u64)&idt[0];
    
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    // Исключения 0-31
    idt_set_gate(0, (u64)isr_wrapper0, 0x08, 0x8E);
    idt_set_gate(1, (u64)isr_wrapper1, 0x08, 0x8E);
    idt_set_gate(2, (u64)isr_wrapper2, 0x08, 0x8E);
    idt_set_gate(3, (u64)isr_wrapper3, 0x08, 0x8E);
    idt_set_gate(4, (u64)isr_wrapper4, 0x08, 0x8E);
    idt_set_gate(5, (u64)isr_wrapper5, 0x08, 0x8E);
    idt_set_gate(6, (u64)isr_wrapper6, 0x08, 0x8E);
    idt_set_gate(7, (u64)isr_wrapper7, 0x08, 0x8E);
    idt_set_gate(8, (u64)isr_wrapper8, 0x08, 0x8E);
    idt_set_gate(9, (u64)isr_wrapper9, 0x08, 0x8E);
    idt_set_gate(10, (u64)isr_wrapper10, 0x08, 0x8E);
    idt_set_gate(11, (u64)isr_wrapper11, 0x08, 0x8E);
    idt_set_gate(12, (u64)isr_wrapper12, 0x08, 0x8E);
    idt_set_gate(13, (u64)isr_wrapper13, 0x08, 0x8E);
    idt_set_gate(14, (u64)isr_wrapper14, 0x08, 0x8E);
    idt_set_gate(15, (u64)isr_wrapper15, 0x08, 0x8E);
    idt_set_gate(16, (u64)isr_wrapper16, 0x08, 0x8E);
    idt_set_gate(17, (u64)isr_wrapper17, 0x08, 0x8E);
    idt_set_gate(18, (u64)isr_wrapper18, 0x08, 0x8E);
    idt_set_gate(19, (u64)isr_wrapper19, 0x08, 0x8E);
    idt_set_gate(20, (u64)isr_wrapper20, 0x08, 0x8E);
    idt_set_gate(21, (u64)isr_wrapper21, 0x08, 0x8E);
    idt_set_gate(22, (u64)isr_wrapper22, 0x08, 0x8E);
    idt_set_gate(23, (u64)isr_wrapper23, 0x08, 0x8E);
    idt_set_gate(24, (u64)isr_wrapper24, 0x08, 0x8E);
    idt_set_gate(25, (u64)isr_wrapper25, 0x08, 0x8E);
    idt_set_gate(26, (u64)isr_wrapper26, 0x08, 0x8E);
    idt_set_gate(27, (u64)isr_wrapper27, 0x08, 0x8E);
    idt_set_gate(28, (u64)isr_wrapper28, 0x08, 0x8E);
    idt_set_gate(29, (u64)isr_wrapper29, 0x08, 0x8E);
    idt_set_gate(30, (u64)isr_wrapper30, 0x08, 0x8E);
    idt_set_gate(31, (u64)isr_wrapper31, 0x08, 0x8E);
    
    // IRQ 32-47
    idt_set_gate(32, (u64)irq0_handler_asm, 0x08, 0x8E);
    idt_set_gate(33, (u64)irq1_handler_asm, 0x08, 0x8E);
    idt_set_gate(34, (u64)irq2_handler_asm, 0x08, 0x8E);
    idt_set_gate(35, (u64)irq3_handler_asm, 0x08, 0x8E);
    idt_set_gate(36, (u64)irq4_handler_asm, 0x08, 0x8E);
    idt_set_gate(37, (u64)irq5_handler_asm, 0x08, 0x8E);
    idt_set_gate(38, (u64)irq6_handler_asm, 0x08, 0x8E);
    idt_set_gate(39, (u64)irq7_handler_asm, 0x08, 0x8E);
    idt_set_gate(40, (u64)irq8_handler_asm, 0x08, 0x8E);
    idt_set_gate(41, (u64)irq9_handler_asm, 0x08, 0x8E);
    idt_set_gate(42, (u64)irq10_handler_asm, 0x08, 0x8E);
    idt_set_gate(43, (u64)irq11_handler_asm, 0x08, 0x8E);
    idt_set_gate(44, (u64)irq12_handler_asm, 0x08, 0x8E);
    idt_set_gate(45, (u64)irq13_handler_asm, 0x08, 0x8E);
    idt_set_gate(46, (u64)irq14_handler_asm, 0x08, 0x8E);
    idt_set_gate(47, (u64)irq15_handler_asm, 0x08, 0x8E);
    
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
    outb(0xA1, 0xFF);
    
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

u32 get_ticks(void) {
    return system_ticks;
}

u32 get_seconds(void) {
    return system_ticks / 100;
}
