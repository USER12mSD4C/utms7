#ifndef GDT_H
#define GDT_H

#include "../include/types.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 limit_high : 4;
    u8 flags : 4;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

int gdt_init(void);
void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran);

#endif
