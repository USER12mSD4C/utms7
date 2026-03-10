#include "paging.h"
#include "memory.h"
#include "../include/string.h"

// Адреса из entry.asm
#define PML4_ADDR   0x1000
#define PDPT_ADDR   0x2000
#define PD_ADDR     0x3000
#define PDPT2_ADDR  0x4000
#define PD2_ADDR    0x5000

static u64* pml4 = (u64*)PML4_ADDR;

int paging_init(void) {
    // Проверяем что entry.asm уже создал таблицы
    if (pml4[0] != (PDPT_ADDR | 3)) return -1;
    
    // Добавляем отображение для framebuffer (0xFD000000)
    // Используем вторую PDPT (индекс 510 в PML4)
    u64* pdpt2 = (u64*)PDPT2_ADDR;
    u64* pd2 = (u64*)PD2_ADDR;
    
    // Очищаем таблицы если нужно
    // Они уже должны быть созданы в entry.asm
    
    // Добавляем запись для framebuffer в PD2
    // 0xFD000000 / 0x200000 = 0x7E8
    pd2[0x7E8] = 0xFD000000 | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    
    return 0;
}

static u64* get_pdpt(u64 virt_addr) {
    u64 index = (virt_addr >> 39) & 0x1FF;
    if (!(pml4[index] & PAGE_PRESENT)) return NULL;
    return (u64*)(pml4[index] & ~0xFFF);
}

static u64* get_pd(u64 virt_addr) {
    u64* pdpt = get_pdpt(virt_addr);
    if (!pdpt) return NULL;
    
    u64 index = (virt_addr >> 30) & 0x1FF;
    if (!(pdpt[index] & PAGE_PRESENT)) return NULL;
    return (u64*)(pdpt[index] & ~0xFFF);
}

int paging_map(u64 phys_addr, u64 virt_addr, u64 flags) {
    u64* pd = get_pd(virt_addr);
    if (!pd) return -1;
    
    u64 index = (virt_addr >> 21) & 0x1FF;
    pd[index] = (phys_addr & ~0x1FFFFF) | PAGE_PRESENT | flags | PAGE_HUGE;
    
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 0;
}

int paging_map_range(u64 phys_start, u64 virt_start, u64 size, u64 flags) {
    for (u64 offset = 0; offset < size; offset += 0x200000) {
        if (paging_map(phys_start + offset, virt_start + offset, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

void paging_enable(void) {
    // Уже включено в entry.asm
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
}
