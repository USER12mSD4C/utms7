#include "idt.h"
#include "../include/io.h"
#include "../include/string.h"
#include "panic.h"
#include "gdt.h"

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idtp;
static irq_handler_t irq_handlers[16];
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

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags) {
    if (num >= IDT_ENTRIES) {
        return;
    }
    
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

static int idt_verify(void) {
    struct idt_ptr verify;
    
    __asm__ volatile ("sidt %0" : "=m"(verify));
    
    if (verify.limit != idtp.limit) {
        return -1;
    }
    
    if (verify.base != idtp.base) {
        return -1;
    }
    
    return 0;
}

void idt_register_irq(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
}

void irq_unmask(int irq) {
    u16 port;
    u8 value;
    
    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void irq_mask(int irq) {
    u16 port;
    u8 value;
    
    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void exception_handler_c(int num, int error_code) {
    (void)error_code;
    panic("Exception");
}

void irq_handler_dispatch(int irq) {
    if (irq >= 0 && irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }
}

void irq0_handler_c(void) {
    system_ticks++;
    outb(0x20, 0x20);
}

void irq11_handler_c(void) {
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void irq12_handler_c(void) {
    outb(0x20, 0x20);
}

int idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (u64)idt;
    
    memset(idt, 0, sizeof(idt));
    
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = NULL;
    }
    
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (u64)isr_wrapper0 + i * 16, __KERNEL_CS, IDT_INTERRUPT_GATE);
    }
    
    idt_set_gate(32, (u64)irq0, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(33, (u64)irq1, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(34, (u64)irq2, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(35, (u64)irq3, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(36, (u64)irq4, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(37, (u64)irq5, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(38, (u64)irq6, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(39, (u64)irq7, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(40, (u64)irq8, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(41, (u64)irq9, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(42, (u64)irq10, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(43, (u64)irq11, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(44, (u64)irq12, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(45, (u64)irq13, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(46, (u64)irq14, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(47, (u64)irq15, __KERNEL_CS, IDT_INTERRUPT_GATE);
    
    __asm__ volatile ("lidt %0" : : "m"(idtp));
    
    if (idt_verify() != 0) {
        return -1;
    }
    
    irq_remap();
    irq_unmask(0);
    irq_unmask(1);
    irq_unmask(12);
    
    idt_register_irq(0, irq0_handler_c);
    idt_register_irq(11, irq11_handler_c);
    idt_register_irq(12, irq12_handler_c);
    
    return 0;
}

int timer_init(void) {
    u32 divisor = 1193180 / 100;
    
    if (divisor == 0) {
        return -1;
    }
    
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

void idt_get_info(u16 *limit, u64 *base) {
    struct idt_ptr ptr;
    
    __asm__ volatile ("sidt %0" : "=m"(ptr));
    
    if (limit) {
        *limit = ptr.limit;
    }
    if (base) {
        *base = ptr.base;
    }
}
