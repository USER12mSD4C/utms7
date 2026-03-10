#ifndef PAGING_H
#define PAGING_H

#include "../include/types.h"

#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITABLE   (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_HUGE       (1 << 7)

int paging_init(void);
int paging_map(u64 phys_addr, u64 virt_addr, u64 flags);
int paging_map_range(u64 phys_start, u64 virt_start, u64 size, u64 flags);
void paging_enable(void);

#endif
