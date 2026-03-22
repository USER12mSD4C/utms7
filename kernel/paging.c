// kernel/paging.c
#include "paging.h"
#include "memory.h"
#include "../include/string.h"

// Адреса для статических таблиц страниц
#define PML4_ADDR   0x1000
#define PDPT_ADDR   0x2000
#define PD_ADDR     0x3000
#define PDPT2_ADDR  0x4000
#define PD2_ADDR    0x5000

// Пул для динамических таблиц страниц
#define PAGE_TABLE_POOL_SIZE 64
#define PAGE_TABLE_POOL_ADDR 0x6000

static u64* pml4 = (u64*)PML4_ADDR;
static u64 next_free_table = PAGE_TABLE_POOL_ADDR;
static u64 pool_end = PAGE_TABLE_POOL_ADDR + PAGE_TABLE_POOL_SIZE * 4096;

// Выделение страницы для таблицы страниц
static void* alloc_page_table(void) {
    if (next_free_table >= pool_end) return NULL;
    void* addr = (void*)next_free_table;
    next_free_table += 4096;
    memset(addr, 0, 4096);
    return addr;
}

// Инициализация страничной адресации (вызывается из entry.asm)
int paging_init(void) {
    next_free_table = PAGE_TABLE_POOL_ADDR;
    
    // Проверяем, что PML4 уже настроен загрузчиком
    if ((pml4[0] & PAGE_PRESENT) == 0) {
        return -1;
    }
    
    // Настраиваем отображение для видеопамяти (0xFD000000)
    // PML4[510] указывает на PDPT2
    pml4[510] = PDPT2_ADDR | PAGE_PRESENT | PAGE_WRITABLE;
    
    u64* pdpt2 = (u64*)PDPT2_ADDR;
    pdpt2[0] = PD2_ADDR | PAGE_PRESENT | PAGE_WRITABLE;
    
    u64* pd2 = (u64*)PD2_ADDR;
    for (int i = 0; i < 512; i++) pd2[i] = 0;
    
    // 0x7E8 * 2MB = 0xFD000000
    pd2[0x7E8] = 0xFD000000 | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    
    // Обновляем CR3
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
    
    return 0;
}

// Маппинг одной страницы в текущем адресном пространстве
int paging_map(u64 phys_addr, u64 virt_addr, u64 flags) {
    u64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    u64 pd_idx = (virt_addr >> 21) & 0x1FF;
    u64 pt_idx = (virt_addr >> 12) & 0x1FF;
    
    // PML4
    if ((pml4[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)alloc_page_table();
        if (!new_pdpt) return -1;
        pml4[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pdpt = (u64*)(pml4[pml4_idx] & ~0xFFF);
    
    // PDPT
    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)alloc_page_table();
        if (!new_pd) return -1;
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFF);
    
    // PD
    if ((pd[pd_idx] & PAGE_PRESENT) == 0) {
        u64* new_pt = (u64*)alloc_page_table();
        if (!new_pt) return -1;
        pd[pd_idx] = (u64)new_pt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pt = (u64*)(pd[pd_idx] & ~0xFFF);
    
    // PT
    pt[pt_idx] = (phys_addr & ~0xFFF) | flags;
    
    // Инвалидируем TLB
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
    
    return 0;
}

// Маппинг диапазона страниц
int paging_map_range(u64 phys_start, u64 virt_start, u64 size, u64 flags) {
    for (u64 offset = 0; offset < size; offset += 4096) {
        if (paging_map(phys_start + offset, virt_start + offset, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

// Включение страничной адресации
void paging_enable(void) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
}

// ==================== ФУНКЦИИ ДЛЯ УПРАВЛЕНИЯ АДРЕСНЫМИ ПРОСТРАНСТВАМИ ====================

// Создание нового адресного пространства (копия ядерных отображений)
u64* create_address_space(void) {
    u64* new_pml4 = (u64*)kmalloc(4096);
    if (!new_pml4) return NULL;
    memset(new_pml4, 0, 4096);
    
    // Копируем ядерные отображения (верхняя половина, индексы 256-511)
    u64* kernel_pml4 = (u64*)PML4_ADDR;
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }
    
    return new_pml4;
}

// Освобождение адресного пространства
void free_address_space(u64* pml4) {
    if (!pml4 || pml4 == (u64*)PML4_ADDR) return;
    
    // Освобождаем пользовательские таблицы страниц (нижняя половина, индексы 0-255)
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PAGE_PRESENT) {
            u64* pdpt = (u64*)(pml4[i] & ~0xFFF);
            
            for (int j = 0; j < 512; j++) {
                if (pdpt[j] & PAGE_PRESENT) {
                    u64* pd = (u64*)(pdpt[j] & ~0xFFF);
                    
                    for (int k = 0; k < 512; k++) {
                        if (pd[k] & PAGE_PRESENT) {
                            // Проверяем, что это не huge page
                            if ((pd[k] & PAGE_HUGE) == 0) {
                                u64* pt = (u64*)(pd[k] & ~0xFFF);
                                
                                for (int l = 0; l < 512; l++) {
                                    if (pt[l] & PAGE_PRESENT) {
                                        u64 phys = pt[l] & ~0xFFF;
                                        kfree((void*)phys);
                                    }
                                }
                                kfree(pt);
                            } else {
                                // Huge page
                                u64 phys = pd[k] & ~0xFFF;
                                kfree((void*)phys);
                            }
                        }
                    }
                    kfree(pd);
                }
            }
            kfree(pdpt);
        }
    }
    
    kfree(pml4);
}

// Маппинг страницы в указанном адресном пространстве
int paging_map_for_process(u64* pml4, u64 phys_addr, u64 virt_addr, u64 flags) {
    u64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    u64 pd_idx = (virt_addr >> 21) & 0x1FF;
    u64 pt_idx = (virt_addr >> 12) & 0x1FF;
    
    // PML4
    if ((pml4[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)kmalloc(4096);
        if (!new_pdpt) return -1;
        memset(new_pdpt, 0, 4096);
        pml4[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pdpt = (u64*)(pml4[pml4_idx] & ~0xFFF);
    
    // PDPT
    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)kmalloc(4096);
        if (!new_pd) return -1;
        memset(new_pd, 0, 4096);
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFF);
    
    // PD
    if ((pd[pd_idx] & PAGE_PRESENT) == 0) {
        u64* new_pt = (u64*)kmalloc(4096);
        if (!new_pt) return -1;
        memset(new_pt, 0, 4096);
        pd[pd_idx] = (u64)new_pt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pt = (u64*)(pd[pd_idx] & ~0xFFF);
    
    // PT
    pt[pt_idx] = (phys_addr & ~0xFFF) | flags;
    
    return 0;
}

// Маппинг huge page (2MB) в указанном адресном пространстве
int paging_map_huge_for_process(u64* pml4, u64 phys_addr, u64 virt_addr, u64 flags) {
    u64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    u64 pd_idx = (virt_addr >> 21) & 0x1FF;
    
    // PML4
    if ((pml4[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)kmalloc(4096);
        if (!new_pdpt) return -1;
        memset(new_pdpt, 0, 4096);
        pml4[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pdpt = (u64*)(pml4[pml4_idx] & ~0xFFF);
    
    // PDPT
    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)kmalloc(4096);
        if (!new_pd) return -1;
        memset(new_pd, 0, 4096);
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFF);
    
    // PD — huge page
    pd[pd_idx] = (phys_addr & ~0x1FFFFF) | flags | PAGE_HUGE;
    
    return 0;
}

// Копирование адресного пространства (для fork — пока просто выделяем новое)
u64* copy_address_space(u64* src_pml4) {
    (void)src_pml4;
    // В будущем: COW (copy-on-write)
    // Пока просто создаём пустое пространство
    return create_address_space();
}
