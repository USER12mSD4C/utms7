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

extern u32 system_ticks;

int idt_init(void);
int timer_init(void);
void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags);
u32 get_seconds(void);
u32 get_ticks(void);

#endif
