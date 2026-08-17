#include "binfmt.h"
#include "elf.h"
#include "paging.h"
#include "memory.h"
#include "../include/string.h"
#define RAW_LOAD_BASE 0x40000000
static binfmt_t* formats = 0;
static int binfmt_ready = 0;
static binfmt_t elf_format;
static binfmt_t raw_format;
static int elf_probe(const u8* data, u32 size) {
    if (size < 64) return 0;
    if (*(const u32*)data != 0x464C457F) return 0;
    if (data[4] != 2) return 0;
    if (data[5] != 1) return 0;
    return 1;
}
static int raw_probe(const u8* data, u32 size) {
    (void)data;
    return size > 0;
}
static u64 raw_load(u8* data, u32 size, u64* pml4, u64* out_max_vaddr) {
    u64 pages = (size + 4095) / 4096;
    if (pages == 0) return 0;
    for (u64 i = 0; i < pages; i++) {
        u64 phys = (u64)pmm_alloc_page();
        if (!phys) return 0;
        if (paging_map_for_process(pml4, phys, RAW_LOAD_BASE + i * 4096,
                                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page((void*)phys);
            return 0;
        }

        // Копируем данные напрямую в физическую страницу
        u64 chunk = size - i * 4096;
        if (chunk > 4096) chunk = 4096;
        memset((void*)phys, 0, 4096);
        memcpy((void*)phys, data + i * 4096, chunk);
    }
    if (out_max_vaddr) {
        *out_max_vaddr = RAW_LOAD_BASE + pages * 4096;
    }
    return RAW_LOAD_BASE;
}
int binfmt_register(binfmt_t* fmt) {
    if (!fmt || !fmt->probe || !fmt->load) return -1;
    fmt->next = formats;
    formats = fmt;
    return 0;
}
static void binfmt_init_builtin(void) {
    if (binfmt_ready) return;
    binfmt_ready = 1;
    elf_format.name = "elf64";
    elf_format.probe = elf_probe;
    elf_format.load = elf_load;
    elf_format.next = 0;
    raw_format.name = "raw";
    raw_format.probe = raw_probe;
    raw_format.load = raw_load;
    raw_format.next = 0;
    binfmt_register(&raw_format);
    binfmt_register(&elf_format);
}
u64 binfmt_load(u8* data, u32 size, u64* pml4, u64* out_max_vaddr) {
    if (!data || size == 0) return 0;
    binfmt_init_builtin();
    binfmt_t* f = formats;
    while (f) {
        if (f->probe(data, size)) {
            return f->load(data, size, pml4, out_max_vaddr);
        }
        f = f->next;
    }
    return 0;
}
const char* binfmt_name(const u8* data, u32 size) {
    if (!data || size == 0) return "none";
    binfmt_init_builtin();
    binfmt_t* f = formats;
    while (f) {
        if (f->probe(data, size)) return f->name;
        f = f->next;
    }
    return "none";
}
