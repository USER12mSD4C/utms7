#ifndef IDT_H
#define IDT_H

#include "../include/types.h"

struct idt_entry {
    u16 base_low;
    u16 selector;
    u8  ist;
    u8  flags;
    u16 base_mid;
    u32 base_high;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

int idt_init(void);
int timer_init(void);
u32 get_ticks(void);
u32 get_seconds(void);

// Обработчики на C
void irq0_handler_c(void);
void irq1_handler_c(void);
void irq11_handler_c(void);
void irq12_handler_c(void);

#endif
