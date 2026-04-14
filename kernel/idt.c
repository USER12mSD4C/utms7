// файл: kernel/idt.c
#include "idt.h"
#include "../include/io.h"
#include "../include/string.h"
#include "panic.h"
#include "gdt.h"
#include "sched.h"

#define IDT_ENTRIES 256
#define IDT_INTERRUPT_GATE 0x8E
#define IDT_TRAP_GATE 0x8F

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  flags;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idtp;
static irq_handler_t irq_handlers[16];
u32 system_ticks = 0;

// Внешние ассемблерные точки входа
extern void isr_wrapper0(void);   extern void isr_wrapper1(void);
extern void isr_wrapper2(void);   extern void isr_wrapper3(void);
extern void isr_wrapper4(void);   extern void isr_wrapper5(void);
extern void isr_wrapper6(void);   extern void isr_wrapper7(void);
extern void isr_wrapper8(void);   extern void isr_wrapper9(void);
extern void isr_wrapper10(void);  extern void isr_wrapper11(void);
extern void isr_wrapper12(void);  extern void isr_wrapper13(void);
extern void isr_wrapper14(void);  extern void isr_wrapper15(void);
extern void isr_wrapper16(void);  extern void isr_wrapper17(void);
extern void isr_wrapper18(void);  extern void isr_wrapper19(void);
extern void isr_wrapper20(void);  extern void isr_wrapper21(void);
extern void isr_wrapper22(void);  extern void isr_wrapper23(void);
extern void isr_wrapper24(void);  extern void isr_wrapper25(void);
extern void isr_wrapper26(void);  extern void isr_wrapper27(void);
extern void isr_wrapper28(void);  extern void isr_wrapper29(void);
extern void isr_wrapper30(void);  extern void isr_wrapper31(void);

extern void irq0(void);   extern void irq1(void);
extern void irq2(void);   extern void irq3(void);
extern void irq4(void);   extern void irq5(void);
extern void irq6(void);   extern void irq7(void);
extern void irq8(void);   extern void irq9(void);
extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);
extern void irq14(void);  extern void irq15(void);

static void send_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags) {
    if (num >= IDT_ENTRIES) return;
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector    = selector;
    idt[num].ist         = 0;
    idt[num].flags       = flags;
    idt[num].reserved    = 0;
}

static int idt_verify(void) {
    struct idt_ptr verify;
    __asm__ volatile ("sidt %0" : "=m"(verify));
    return (verify.limit == idtp.limit && verify.base == idtp.base) ? 0 : -1;
}

void idt_register_irq(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) irq_handlers[irq] = handler;
}

void irq_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
}

void irq_unmask(int irq) {
    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8  value = inb(port) & ~(1 << (irq & 7));
    outb(port, value);
}

void irq_mask(int irq) {
    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8  value = inb(port) | (1 << (irq & 7));
    outb(port, value);
}

void exception_handler_c(int num, int error_code) {
    panic("Unhandled CPU exception");
    (void)num; (void)error_code;
}

void irq_handler_dispatch(int irq) {
    if (irq >= 0 && irq < 16 && irq_handlers[irq])
        irq_handlers[irq]();
    send_eoi(irq);
}

static void irq0_handler_c(void) {
    system_ticks++;
}

// Заглушки для остальных IRQ
static void irq1_handler_c(void) {}
static void irq2_handler_c(void) {}
static void irq3_handler_c(void) {}
static void irq4_handler_c(void) {}
static void irq5_handler_c(void) {}
static void irq6_handler_c(void) {}
static void irq7_handler_c(void) {}
static void irq8_handler_c(void) {}
static void irq9_handler_c(void) {}
static void irq10_handler_c(void) {}
static void irq11_handler_c(void) {}
static void irq12_handler_c(void) {}
static void irq13_handler_c(void) {}
static void irq14_handler_c(void) {}
static void irq15_handler_c(void) {}

int idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u64)idt;
    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));

    // Установка векторов исключений (0-31)
    idt_set_gate(0,  (u64)isr_wrapper0,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(1,  (u64)isr_wrapper1,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(2,  (u64)isr_wrapper2,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(3,  (u64)isr_wrapper3,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(4,  (u64)isr_wrapper4,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(5,  (u64)isr_wrapper5,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(6,  (u64)isr_wrapper6,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(7,  (u64)isr_wrapper7,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(8,  (u64)isr_wrapper8,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(9,  (u64)isr_wrapper9,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(10, (u64)isr_wrapper10, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(11, (u64)isr_wrapper11, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(12, (u64)isr_wrapper12, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(13, (u64)isr_wrapper13, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(14, (u64)isr_wrapper14, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(15, (u64)isr_wrapper15, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(16, (u64)isr_wrapper16, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(17, (u64)isr_wrapper17, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(18, (u64)isr_wrapper18, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(19, (u64)isr_wrapper19, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(20, (u64)isr_wrapper20, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(21, (u64)isr_wrapper21, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(22, (u64)isr_wrapper22, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(23, (u64)isr_wrapper23, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(24, (u64)isr_wrapper24, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(25, (u64)isr_wrapper25, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(26, (u64)isr_wrapper26, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(27, (u64)isr_wrapper27, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(28, (u64)isr_wrapper28, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(29, (u64)isr_wrapper29, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(30, (u64)isr_wrapper30, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(31, (u64)isr_wrapper31, __KERNEL_CS, IDT_INTERRUPT_GATE);

    // Установка векторов IRQ (32-47)
    idt_set_gate(32, (u64)irq0,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(33, (u64)irq1,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(34, (u64)irq2,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(35, (u64)irq3,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(36, (u64)irq4,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(37, (u64)irq5,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(38, (u64)irq6,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(39, (u64)irq7,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(40, (u64)irq8,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(41, (u64)irq9,  __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(42, (u64)irq10, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(43, (u64)irq11, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(44, (u64)irq12, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(45, (u64)irq13, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(46, (u64)irq14, __KERNEL_CS, IDT_INTERRUPT_GATE);
    idt_set_gate(47, (u64)irq15, __KERNEL_CS, IDT_INTERRUPT_GATE);

    __asm__ volatile ("lidt %0" : : "m"(idtp));
    if (idt_verify() != 0) return -1;

    irq_remap();
    for (int i = 0; i < 16; i++) irq_mask(i);

    idt_register_irq(0, irq0_handler_c);
    idt_register_irq(1, irq1_handler_c);
    idt_register_irq(2, irq2_handler_c);
    idt_register_irq(3, irq3_handler_c);
    idt_register_irq(4, irq4_handler_c);
    idt_register_irq(5, irq5_handler_c);
    idt_register_irq(6, irq6_handler_c);
    idt_register_irq(7, irq7_handler_c);
    idt_register_irq(8, irq8_handler_c);
    idt_register_irq(9, irq9_handler_c);
    idt_register_irq(10, irq10_handler_c);
    idt_register_irq(11, irq11_handler_c);
    idt_register_irq(12, irq12_handler_c);
    idt_register_irq(13, irq13_handler_c);
    idt_register_irq(14, irq14_handler_c);
    idt_register_irq(15, irq15_handler_c);

    irq_unmask(0); // таймер
    irq_unmask(1); // клавиатура

    return 0;
}

int timer_init(void) {
    u32 divisor = 1193180 / 100; // 100 Гц
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    return 0;
}

u32 get_ticks(void) { return system_ticks; }
u32 get_seconds(void) { return system_ticks / 100; }

void idt_get_info(u16 *limit, u64 *base) {
    struct idt_ptr ptr;
    __asm__ volatile ("sidt %0" : "=m"(ptr));
    if (limit) *limit = ptr.limit;
    if (base)  *base  = ptr.base;
}
