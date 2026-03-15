#include "paging.h"
#include "memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"

// Адреса из entry.asm
#define PML4_ADDR   0x1000
#define PDPT_ADDR   0x2000
#define PD_ADDR     0x3000
#define PDPT2_ADDR  0x4000
#define PD2_ADDR    0x5000

// Пул для таблиц страниц (16 страниц = 64KB)
#define PAGE_TABLE_POOL_SIZE 16
#define PAGE_TABLE_POOL_ADDR 0x6000

static u64* pml4 = (u64*)PML4_ADDR;
static u64 next_free_table = PAGE_TABLE_POOL_ADDR;
static u64 pool_end = PAGE_TABLE_POOL_ADDR + PAGE_TABLE_POOL_SIZE * 4096;

// Физический аллокатор для таблиц страниц
static void* alloc_page_table(void) {
    if (next_free_table >= pool_end) {
        vga_write("Paging: out of page tables!\n");
        return NULL;
    }
    void* addr = (void*)next_free_table;
    next_free_table += 4096;
    memset(addr, 0, 4096);
    return addr;
}

int paging_init(void) {
    // Инициализируем пул таблиц
    next_free_table = PAGE_TABLE_POOL_ADDR;
    
    // Проверяем что entry.asm создал таблицы
    if ((pml4[0] & 1) == 0) {
        vga_write("Paging: PML4[0] not present\n");
        return -1;
    }
    
    // Получаем адрес PDPT из PML4[0]
    u64 pdpt_addr = pml4[0] & ~0xFFF;
    u64* pdpt = (u64*)pdpt_addr;
    
    if ((pdpt[0] & 1) == 0) {
        vga_write("Paging: PDPT[0] not present\n");
        return -1;
    }
    
    // Получаем адрес PD из PDPT[0]
    u64 pd_addr = pdpt[0] & ~0xFFF;
    u64* pd = (u64*)pd_addr;
    
    // Проверяем первую запись в PD (2MB страница)
    if ((pd[0] & 1) == 0 || (pd[0] & 0x80) == 0) {
        vga_write("Paging: PD[0] not 2MB page\n");
        return -1;
    }
    
    // Настраиваем отображение для framebuffer через верхнюю память
    // PML4[510] для адресов 0xFFFF800000000000
    pml4[510] = PDPT2_ADDR | PAGE_PRESENT | PAGE_WRITABLE;
    
    // Настраиваем PDPT2
    u64* pdpt2 = (u64*)PDPT2_ADDR;
    pdpt2[0] = PD2_ADDR | PAGE_PRESENT | PAGE_WRITABLE;
    
    // Очищаем PD2
    u64* pd2 = (u64*)PD2_ADDR;
    for (int i = 0; i < 512; i++) {
        pd2[i] = 0;
    }
    
    // Отображаем framebuffer
    pd2[0x7E8] = 0xFD000000 | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
    
    vga_write("Paging: pool at 0x6000-0x10000\n");
    
    return 0;
}

int paging_map(u64 phys_addr, u64 virt_addr, u64 flags) {
    u64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    u64 pd_idx = (virt_addr >> 21) & 0x1FF;
    
    // Проверяем/создаем PDPT
    if ((pml4[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)alloc_page_table();
        if (!new_pdpt) return -1;
        pml4[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    u64* pdpt = (u64*)(pml4[pml4_idx] & ~0xFFF);
    
    // Проверяем/создаем PD
    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)alloc_page_table();
        if (!new_pd) return -1;
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFF);
    
    // Устанавливаем запись (2MB страница)
    pd[pd_idx] = (phys_addr & ~0x1FFFFF) | PAGE_PRESENT | flags | PAGE_HUGE;
    
    // Сбрасываем TLB для этого адреса
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
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
}
