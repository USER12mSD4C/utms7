// kernel/paging.h
#ifndef PAGING_H
#define PAGING_H

#include "../include/types.h"

// Флаги страниц
#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITABLE   (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_HUGE       (1 << 7)

// Инициализация
int paging_init(void);

// Маппинг в текущем адресном пространстве
int paging_map(u64 phys_addr, u64 virt_addr, u64 flags);
int paging_map_range(u64 phys_start, u64 virt_start, u64 size, u64 flags);
void paging_enable(void);

// Управление адресными пространствами
u64* create_address_space(void);
void free_address_space(u64* pml4);
int paging_map_for_process(u64* pml4, u64 phys_addr, u64 virt_addr, u64 flags);
int paging_map_huge_for_process(u64* pml4, u64 phys_addr, u64 virt_addr, u64 flags);
u64* copy_address_space(u64* src_pml4);

#endif
