#ifndef IDT_H
#define IDT_H

#include "../include/types.h"

#define IDT_ENTRIES 256
#define IDT_SIZE (IDT_ENTRIES * 16)

#define IDT_INTERRUPT_GATE 0x8E
#define IDT_TRAP_GATE 0x8F
#define IDT_TASK_GATE 0x85

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 flags;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

typedef void (*irq_handler_t)(void);

int idt_init(void);
void idt_set_gate(u8 num, u64 base, u16 selector, u8 flags);
void idt_register_irq(int irq, irq_handler_t handler);
void irq_remap(void);
void irq_unmask(int irq);
void irq_mask(int irq);
int timer_init(void);
u32 get_ticks(void);
u32 get_seconds(void);
void idt_get_info(u16 *limit, u64 *base);

#endif
