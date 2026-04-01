#ifndef GDT_H
#define GDT_H

#include "../include/types.h"

#define GDT_ENTRIES 6
#define GDT_SIZE (GDT_ENTRIES * 8)

#define GDT_NULL_ENTRY   0x00
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20
#define GDT_TSS_ENTRY    0x28

#define __KERNEL_CS GDT_KERNEL_CODE
#define __KERNEL_DS GDT_KERNEL_DATA
#define __USER_CS   GDT_USER_CODE
#define __USER_DS   GDT_USER_DATA

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

struct gdt_page {
    struct gdt_entry entries[GDT_ENTRIES];
} __attribute__((aligned(16)));

int gdt_init(void);
void gdt_reload_segments(void);
void gdt_set_tss(u64 base, u32 limit);
void gdt_get_info(u16 *limit, u64 *base);

#endif
