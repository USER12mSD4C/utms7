#ifndef GDT_H
#define GDT_H

#include "../include/types.h"

#define GDT_ENTRIES 6

#define GDT_NULL_ENTRY   0x00
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20
#define GDT_TSS_ENTRY    0x28

#define __KERNEL_CS GDT_KERNEL_CODE
#define __KERNEL_DS GDT_KERNEL_DATA

int gdt_init(void);

#endif
