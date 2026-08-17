// kernel/elf.c
#include "elf.h"
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "../include/string.h"
#include "../drivers/drm.h"

#define USER_LOW_LIMIT 0x40000000

#define PF_X        (1 << 0)
#define PF_W        (1 << 1)
#define PF_R        (1 << 2)

static int elf_check_header(elf64_hdr_t *hdr) {
    if (*(u32*)hdr->ident != ELF_MAGIC) return -1;
    if (hdr->ident[4] != 2) return -1;
    if (hdr->ident[5] != 1) return -1;
    if (hdr->type != ET_EXEC && hdr->type != ET_DYN) return -1;
    if (hdr->machine != 0x3E) return -1;
    return 0;
}

static u64 push_to_user_stack(u64* pml4, u64 rsp, const void* data, u64 size) {
    rsp -= size;
    rsp &= ~0xF;

    u64 old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");

    memcpy((void*)rsp, data, size);

    __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
    return rsp;
}

static u64 push_string_to_user_stack(u64* pml4, u64 rsp, const char* str) {
    u64 len = strlen(str) + 1;
    return push_to_user_stack(pml4, rsp, str, len);
}

u64 elf_load(u8 *data, u32 size, u64* pml4, u64* out_max_vaddr) {
    elf64_hdr_t *hdr = (elf64_hdr_t*)data;
    if (elf_check_header(hdr) != 0) {
        print("[elf] bad header\n");
        return 0;
    }
    u64 base = 0;
    if (hdr->type == ET_DYN) {
        base = 0x40000000;
    }
    u64 max_vaddr = 0;

    for (int i = 0; i < hdr->phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t*)(data + hdr->phoff + i * hdr->phentsize);
        if (ph->type != PT_LOAD) continue;
        if (ph->memsz == 0) continue;
        u64 vaddr = ph->vaddr + base;
        u64 memsz = ph->memsz;
        u64 filesz = ph->filesz;
        if (vaddr < 0x10000) {
            print("[elf] vaddr too low\n");
            return 0;
        }
        u64 end_vaddr = vaddr + memsz;
        if (end_vaddr > max_vaddr) {
            max_vaddr = end_vaddr;
        }
        u64 start_page = vaddr & ~0xFFFULL;
        u64 end_page = (vaddr + memsz + 4095) & ~0xFFFULL;
        u64 num_pages = (end_page - start_page) / 4096;
        for (u64 j = 0; j < num_pages; j++) {
            u64 virt = start_page + j * 4096;
            u64 phys = (u64)pmm_alloc_page();
            if (!phys) {
                print("[elf] pmm alloc fail\n");
                return 0;
            }
            u64 flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE;
            if (paging_map_for_process(pml4, phys, virt, flags) != 0) {
                pmm_free_page((void*)phys);
                print("[elf] map fail\n");
                return 0;
            }

            // Записываем данные напрямую в физическую страницу (1:1 mapping)
            memset((void*)phys, 0, 4096);

            u64 seg_start = vaddr;
            u64 seg_end = vaddr + filesz;
            u64 pg_start = virt;
            u64 pg_end = virt + 4096;
            if (pg_start < seg_start) pg_start = seg_start;
            if (pg_end > seg_end) pg_end = seg_end;
            if (pg_start < pg_end) {
                u64 copy_size = pg_end - pg_start;
                u64 file_off = ph->offset + (pg_start - vaddr);
                u64 dest_off = pg_start - virt;
                memcpy((void*)(phys + dest_off), data + file_off, copy_size);
            }
        }
    }

    if (out_max_vaddr) {
        *out_max_vaddr = (max_vaddr + 4095) & ~4095ULL;
    }
    return (hdr->type == ET_DYN) ? hdr->entry + base : hdr->entry;
}

int elf_load_current(u8 *data, u32 size, process_t *p) {
    u64* pml4 = create_address_space();
    if (!pml4) return -1;

    u64 max_vaddr = 0;
    u64 entry = elf_load(data, size, pml4, &max_vaddr);
    if (entry == 0) {
        free_address_space(pml4);
        return -1;
    }

    u64 user_stack_top = 0x0000004000000000;
    u64 stack_pages = 64;

    for (u64 i = 0; i < stack_pages; i++) {
        u64 phys = (u64)pmm_alloc_page();
        if (!phys) {
            free_address_space(pml4);
            return -1;
        }
        u64 virt = user_stack_top - (stack_pages - i) * 4096;
        if (paging_map_for_process(pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page((void*)phys);
            free_address_space(pml4);
            return -1;
        }
    }

    u64 old_as = p->cr3;
    p->cr3 = (u64)pml4;
    p->user_rip = entry;
    p->heap_start = max_vaddr;
    p->heap_end = p->heap_start;
    p->user_rsp = user_stack_top;

    if (old_as && old_as != 0x1000) {
        free_address_space((u64*)old_as);
    }

    return 0;
}
