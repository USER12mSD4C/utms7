#include "paging.h"
#include "memory.h"
#include "../include/string.h"

#define PML4_ADDR   0x1000
#define PDPT_ADDR   0x2000
#define PD_ADDR     0x3000
#define PDPT2_ADDR  0x4000
#define PD2_ADDR    0x5000
#define PAGE_TABLE_POOL_SIZE 16
#define PAGE_TABLE_POOL_ADDR 0x6000

static u64* pml4 = (u64*)PML4_ADDR;
static u64 next_free_table = PAGE_TABLE_POOL_ADDR;
static u64 pool_end = PAGE_TABLE_POOL_ADDR + PAGE_TABLE_POOL_SIZE * 4096;

static void* alloc_page_table(void) {
    if (next_free_table >= pool_end) return NULL;
    void* addr = (void*)next_free_table;
    next_free_table += 4096;
    memset(addr, 0, 4096);
    return addr;
}

int paging_init(void) {
    next_free_table = PAGE_TABLE_POOL_ADDR;
    
    if ((pml4[0] & 1) == 0) return -1;
    
    u64 pdpt_addr = pml4[0] & ~0xFFF;
    u64* pdpt = (u64*)pdpt_addr;
    
    if ((pdpt[0] & 1) == 0) return -1;
    
    u64 pd_addr = pdpt[0] & ~0xFFF;
    u64* pd = (u64*)pd_addr;
    
    if ((pd[0] & 1) == 0 || (pd[0] & 0x80) == 0) return -1;
    
    pml4[510] = PDPT2_ADDR | PAGE_PRESENT | PAGE_WRITABLE;
    
    u64* pdpt2 = (u64*)PDPT2_ADDR;
    pdpt2[0] = PD2_ADDR | PAGE_PRESENT | PAGE_WRITABLE;
    
    u64* pd2 = (u64*)PD2_ADDR;
    for (int i = 0; i < 512; i++) pd2[i] = 0;
    
    pd2[0x7E8] = 0xFD000000 | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
    
    return 0;
}

int paging_map(u64 phys_addr, u64 virt_addr, u64 flags) {
    u64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    u64 pd_idx = (virt_addr >> 21) & 0x1FF;
    u64 pt_idx = (virt_addr >> 12) & 0x1FF;
    
    if ((pml4[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)alloc_page_table();
        if (!new_pdpt) return -1;
        pml4[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    u64* pdpt = (u64*)(pml4[pml4_idx] & ~0xFFF);
    
    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)alloc_page_table();
        if (!new_pd) return -1;
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFF);
    
    if ((pd[pd_idx] & PAGE_PRESENT) == 0) {
        u64* new_pt = (u64*)alloc_page_table();
        if (!new_pt) return -1;
        pd[pd_idx] = (u64)new_pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    u64* pt = (u64*)(pd[pd_idx] & ~0xFFF);
    
    pt[pt_idx] = (phys_addr & ~0xFFF) | flags;
    
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
