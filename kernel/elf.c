// kernel/elf.c
#include "elf.h"
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "../include/string.h"

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
    (void)size;
    elf64_hdr_t *hdr = (elf64_hdr_t*)data;

    if (elf_check_header(hdr) != 0) return 0;

    u64 base = (hdr->type == ET_DYN) ? 0x40000000 : 0x400000;
    u64 max_vaddr = base;

    u64 old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));

    for (int i = 0; i < hdr->phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t*)(data + hdr->phoff + i * hdr->phentsize);

        if (ph->type != PT_LOAD) continue;

        u64 vaddr = (hdr->type == ET_DYN) ? ph->vaddr + base : ph->vaddr;
        u64 memsz = ph->memsz;
        u64 filesz = ph->filesz;

        if (memsz == 0) continue;

        if (vaddr + memsz > max_vaddr) {
            max_vaddr = vaddr + memsz;
        }

        u64 pages = (memsz + (vaddr & 4095) + 4095) / 4096;
        u64 offset_in_page = vaddr & 0xFFF;
        u64 start_page = vaddr & ~0xFFF;

        for (u64 j = 0; j < pages; j++) {
            u64 virt = start_page + j * 4096;
            u64 phys = (u64)kmalloc(4096);

            if (!phys) {
                if (old_cr3 != (u64)pml4) {
                    __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
                }
                return 0;
            }

            u64 flags = PAGE_PRESENT | PAGE_USER;
            if (ph->flags & PF_W) flags |= PAGE_WRITABLE;

            if (paging_map_for_process(pml4, phys, virt, flags) != 0) {
                kfree((void*)phys);
                if (old_cr3 != (u64)pml4) {
                    __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
                }
                return 0;
            }

            __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");

            memset((void*)virt, 0, 4096);

            u64 file_offset = ph->offset + j * 4096 - offset_in_page;
            u64 copy_start = (j == 0) ? offset_in_page : 0;
            u64 copy_size = 4096 - copy_start;

            if (file_offset < ph->offset + filesz) {
                u64 remaining = (ph->offset + filesz) - file_offset;
                if (copy_size > remaining) copy_size = remaining;

                if (copy_size > 0 && copy_size <= 4096) {
                    memcpy((void*)(virt + copy_start), data + file_offset, copy_size);
                }
            }

            __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
        }
    }

    if (old_cr3 != (u64)pml4) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
    }

    if (out_max_vaddr) {
        *out_max_vaddr = (max_vaddr + 4095) & ~4095;
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

    u64 user_stack_top = 0x7FFFFFFFF000;
    u64 stack_pages = 16;
    for (u64 i = 0; i < stack_pages; i++) {
        u64 phys = (u64)kmalloc(4096);
        if (!phys) {
            free_address_space(pml4);
            return -1;
        }
        u64 virt = user_stack_top - (stack_pages - i) * 4096;
        if (paging_map_for_process(pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            kfree((void*)phys);
            free_address_space(pml4);
            return -1;
        }
    }

    if (p->cr3 && p->cr3 != (u64)0x1000) {
        free_address_space((u64*)p->cr3);
    }

    p->cr3 = (u64)pml4;
    p->user_rip = entry;

    p->heap_start = max_vaddr;
    p->heap_end = p->heap_start;

    u64 rsp = user_stack_top;

    u8 random_bytes[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
                           0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    u64 at_random_ptr = push_to_user_stack(pml4, rsp, random_bytes, 16);

    u64 prog_name_ptr = push_string_to_user_stack(pml4, rsp, p->name[0] ? p->name : "app");

    u64 envp_null = 0;
    rsp = push_to_user_stack(pml4, rsp, &envp_null, sizeof(u64));

    u64 argv_null = 0;
    rsp = push_to_user_stack(pml4, rsp, &argv_null, sizeof(u64));

    rsp = push_to_user_stack(pml4, rsp, &prog_name_ptr, sizeof(u64));

    u64 auxv[] = {
        6, 4096,
        25, at_random_ptr,
        9, entry,
        0, 0
    };
    rsp = push_to_user_stack(pml4, rsp, auxv, sizeof(auxv));

    u64 argc = 1;
    rsp = push_to_user_stack(pml4, rsp, &argc, sizeof(u64));

    p->user_rsp = rsp;

    return 0;
}
