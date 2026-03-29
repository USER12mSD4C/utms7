// kernel/gdt.h
#ifndef GDT_H
#define GDT_H

#include "../include/types.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

int gdt_init(void);

#endif
