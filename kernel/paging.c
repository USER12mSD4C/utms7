#include "paging.h"
#include "memory.h"
#include "../include/string.h"

#define PML4_ADDR   0x1000
#define PDPT_ADDR   0x2000
#define PD_ADDR     0x3000
#define PAGE_TABLE_POOL_SIZE 64
#define PAGE_TABLE_POOL_ADDR 0x6000
#define USER_LOW_LIMIT 0x40000000

static u64* pml4 = (u64*)PML4_ADDR;
static u64 next_free_table = PAGE_TABLE_POOL_ADDR;
static u64 pool_end = PAGE_TABLE_POOL_ADDR + PAGE_TABLE_POOL_SIZE * 4096;

extern u64 drm_fb_phys(void);
extern u64 drm_fb_size(void);

static void* alloc_page_table(void) {
    if (next_free_table >= pool_end) return NULL;
    void* addr = (void*)next_free_table;
    next_free_table += 4096;
    memset(addr, 0, 4096);
    return addr;
}

int paging_init(void) {
    if ((pml4[0] & PAGE_PRESENT) == 0) return -1;
    extern u64 __text_start;
    extern u64 __text_end;
    u64* pd = (u64*)PD_ADDR;
    u64* pt = (u64*)alloc_page_table();
    if (!pt) return -1;
    for (int i = 0; i < 512; i++) {
        u64 phys = (u64)i * 4096;
        u64 flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (phys >= (u64)&__text_start && phys < (u64)&__text_end) {
            flags = PAGE_PRESENT;
        }
        pt[i] = phys | flags;
    }
    pd[0] = (u64)pt | PAGE_PRESENT | PAGE_WRITABLE;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4));
    return 0;
}

int paging_map(u64 phys_addr, u64 virt_addr, u64 flags) {
    u64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    u64 pd_idx    = (virt_addr >> 21) & 0x1FF;
    u64 pt_idx    = (virt_addr >> 12) & 0x1FF;
    if ((pml4[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)alloc_page_table();
        if (!new_pdpt) return -1;
        pml4[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    u64* pdpt = (u64*)(pml4[pml4_idx] & ~0xFFFULL);
    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)alloc_page_table();
        if (!new_pd) return -1;
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE;
    }
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (flags & PAGE_HUGE) {
        pd[pd_idx] = (phys_addr & ~0x1FFFFFULL) | flags;
        __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
        return 0;
    }
    if ((pd[pd_idx] & PAGE_PRESENT) == 0) {
        u64* new_pt = (u64*)alloc_page_table();
        if (!new_pt) return -1;
        pd[pd_idx] = (u64)new_pt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    u64* pt = (u64*)(pd[pd_idx] & ~0xFFFULL);
    pt[pt_idx] = (phys_addr & ~0xFFFULL) | flags;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 0;
}

int paging_map_for_process(u64* pml4_ptr, u64 phys, u64 virt, u64 flags) {
    u64 pml4_idx = (virt >> 39) & 0x1FF;
    u64 pdpt_idx = (virt >> 30) & 0x1FF;
    u64 pd_idx    = (virt >> 21) & 0x1FF;
    u64 pt_idx    = (virt >> 12) & 0x1FF;

    if ((pml4_ptr[pml4_idx] & PAGE_PRESENT) == 0) {
        u64* new_pdpt = (u64*)pmm_alloc_page();
        if (!new_pdpt) return -1;
        memset(new_pdpt, 0, 4096);
        pml4_ptr[pml4_idx] = (u64)new_pdpt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    u64* pdpt = (u64*)(pml4_ptr[pml4_idx] & ~0xFFFULL);

    if ((pdpt[pdpt_idx] & PAGE_PRESENT) == 0) {
        u64* new_pd = (u64*)pmm_alloc_page();
        if (!new_pd) return -1;
        memset(new_pd, 0, 4096);
        pdpt[pdpt_idx] = (u64)new_pd | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    u64* pd = (u64*)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (flags & PAGE_HUGE) {
        if ((pd[pd_idx] & PAGE_PRESENT) && !(pd[pd_idx] & PAGE_HUGE)) return -1;
        pd[pd_idx] = (phys & ~0x1FFFFFULL) | flags;
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
        return 0;
    }

    if ((pd[pd_idx] & PAGE_PRESENT) && (pd[pd_idx] & PAGE_HUGE)) {
        u64 huge_phys = pd[pd_idx] & ~0x1FFFFFULL;
        u64 huge_flags = pd[pd_idx] & 0xFFFULL;
        u64* new_pt = (u64*)pmm_alloc_page();
        if (!new_pt) return -1;
        for (int i = 0; i < 512; i++) {
            new_pt[i] = (huge_phys + (u64)i * 4096) | huge_flags;
        }
        pd[pd_idx] = (u64)new_pt | PAGE_PRESENT | PAGE_WRITABLE | (huge_flags & PAGE_USER);
    }

    if ((pd[pd_idx] & PAGE_PRESENT) == 0) {
        u64* new_pt = (u64*)pmm_alloc_page();
        if (!new_pt) return -1;
        memset(new_pt, 0, 4096);
        pd[pd_idx] = (u64)new_pt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    u64* pt = (u64*)(pd[pd_idx] & ~0xFFFULL);
    pt[pt_idx] = (phys & ~0xFFFULL) | flags;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

u64* create_address_space(void) {
    u64* new_pml4 = (u64*)pmm_alloc_page();
    if (!new_pml4) return NULL;
    u64* kernel_pml4 = (u64*)PML4_ADDR;
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }
    u64* pdpt = (u64*)pmm_alloc_page();
    if (!pdpt) {
        pmm_free_page(new_pml4);
        return NULL;
    }
    new_pml4[0] = (u64)pdpt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    u64* kernel_pdpt = (u64*)(kernel_pml4[0] & ~0xFFFULL);
    pdpt[0] = kernel_pdpt[0];
    u64 fb_phys = drm_fb_phys();
    u64 fb_size = drm_fb_size();

    if (fb_phys >= 0x40000000ULL && fb_size != 0) {
        for (u64 off = 0; off < fb_size; off += 4096) {
            if (paging_map_for_process(new_pml4, fb_phys + off, fb_phys + off,
                PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
                break;
            }
        }
    }

    return new_pml4;
}

u64* copy_address_space(u64* src_pml4) {
    u64* dst_pml4 = create_address_space();
    if (!dst_pml4) return NULL;
    for (int i = 0; i < 256; i++) {
        if (!(src_pml4[i] & PAGE_PRESENT)) continue;
        u64* pdpt = (u64*)(src_pml4[i] & ~0xFFFULL);
        for (int j = 0; j < 512; j++) {
            if (i == 0 && (j == 0 || j == 3)) continue;
            if (!(pdpt[j] & PAGE_PRESENT)) continue;
            u64* pd = (u64*)(pdpt[j] & ~0xFFFULL);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PAGE_PRESENT)) continue;
                if (pd[k] & PAGE_HUGE) continue;
                u64* pt = (u64*)(pd[k] & ~0xFFFULL);
                for (int l = 0; l < 512; l++) {
                    if (!(pt[l] & PAGE_PRESENT)) continue;
                    u64 old_phys = pt[l] & ~0xFFFULL;
                    u64 flags = pt[l] & 0xFFF;
                    u64 virt = ((u64)i << 39) | ((u64)j << 30) |
                               ((u64)k << 21) | ((u64)l << 12);
                    u64 new_phys = (u64)pmm_alloc_page();
                    if (!new_phys) {
                        free_address_space(dst_pml4);
                        return NULL;
                    }
                    memcpy((void*)new_phys, (void*)old_phys, 4096);
                    if (paging_map_for_process(dst_pml4, new_phys, virt, flags) != 0) {
                        pmm_free_page((void*)new_phys);
                        free_address_space(dst_pml4);
                        return NULL;
                    }
                }
            }
        }
    }
    return dst_pml4;
}

void free_address_space(u64* pml4_ptr) {
    if (!pml4_ptr || pml4_ptr == (u64*)PML4_ADDR) return;
    for (int i = 0; i < 256; i++) {
        if (!(pml4_ptr[i] & PAGE_PRESENT)) continue;
        u64* pdpt = (u64*)(pml4_ptr[i] & ~0xFFFULL);
        for (int j = 0; j < 512; j++) {
            if (i == 0 && (j == 0 || j == 3)) continue;
            if (!(pdpt[j] & PAGE_PRESENT)) continue;
            u64* pd = (u64*)(pdpt[j] & ~0xFFFULL);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PAGE_PRESENT)) continue;
                if (!(pd[k] & PAGE_HUGE)) {
                    u64* pt = (u64*)(pd[k] & ~0xFFFULL);
                    for (int l = 0; l < 512; l++) {
                        if (pt[l] & PAGE_PRESENT) {
                            u64 phys = pt[l] & ~0xFFFULL;
                            pmm_free_page((void*)phys);
                        }
                    }
                    pmm_free_page(pt);
                }
            }
            pmm_free_page(pd);
        }
        pmm_free_page(pdpt);
    }
    pmm_free_page(pml4_ptr);
}
