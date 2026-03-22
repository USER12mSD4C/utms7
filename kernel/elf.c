// kernel/elf.c
#include "elf.h"
#include "sched.h"      // ДЛЯ process_t
#include "memory.h"
#include "paging.h"
#include "../include/string.h"

// Флаги сегментов ELF
#define PF_X        (1 << 0)
#define PF_W        (1 << 1)
#define PF_R        (1 << 2)

// Проверка ELF заголовка
static int elf_check_header(elf64_hdr_t *hdr) {
    if (*(u32*)hdr->ident != ELF_MAGIC) return -1;
    if (hdr->ident[4] != 2) return -1;           // 64-bit
    if (hdr->ident[5] != 1) return -1;           // little-endian
    if (hdr->type != ET_EXEC && hdr->type != ET_DYN) return -1;
    if (hdr->machine != 0x3E) return -1;         // x86_64
    return 0;
}

// Загрузка ELF в указанное адресное пространство
u64 elf_load(u8 *data, u32 size, u64* pml4) {
    (void)size;
    elf64_hdr_t *hdr = (elf64_hdr_t*)data;
    
    if (elf_check_header(hdr) != 0) return 0;
    
    // База для PIE (Position Independent Executable)
    u64 base = (hdr->type == ET_DYN) ? 0x40000000 : 0x400000;
    
    // Загружаем все LOAD сегменты
    for (int i = 0; i < hdr->phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t*)(data + hdr->phoff + i * hdr->phentsize);
        
        if (ph->type != PT_LOAD) continue;
        
        u64 vaddr = (hdr->type == ET_DYN) ? ph->vaddr + base : ph->vaddr;
        u64 memsz = ph->memsz;
        u64 filesz = ph->filesz;
        
        if (memsz == 0) continue;
        
        u64 pages = (memsz + 4095) / 4096;
        u64 offset_in_page = vaddr & 0xFFF;
        u64 start_page = vaddr & ~0xFFF;
        
        for (u64 j = 0; j < pages; j++) {
            u64 virt = start_page + j * 4096;
            u64 phys = (u64)kmalloc(4096);
            
            if (!phys) {
                return 0;
            }
            
            // Флаги страницы
            u64 flags = PAGE_PRESENT | PAGE_USER;
            if (ph->flags & PF_W) flags |= PAGE_WRITABLE;
            
            // Маппим страницу
            if (paging_map_for_process(pml4, phys, virt, flags) != 0) {
                kfree((void*)phys);
                return 0;
            }
            
            // Очищаем страницу
            memset((void*)virt, 0, 4096);
            
            // Копируем данные из ELF
            u64 file_offset = ph->offset + j * 4096;
            u64 copy_start = (j == 0) ? offset_in_page : 0;
            u64 copy_size = 4096 - copy_start;
            
            if (file_offset < ph->offset + filesz) {
                u64 remaining = ph->offset + filesz - file_offset;
                if (copy_size > remaining) copy_size = remaining;
                
                if (copy_size > 0 && copy_size <= 4096) {
                    memcpy((void*)(virt + copy_start), data + file_offset, copy_size);
                }
            }
        }
    }
    
    // Возвращаем точку входа
    return (hdr->type == ET_DYN) ? hdr->entry + base : hdr->entry;
}

// Загрузка ELF в текущий процесс (для exec)
// Используем struct process_t для соответствия с elf.h
int elf_load_current(u8 *data, u32 size, struct process_t *p) {
    // Создаём новое адресное пространство
    u64* pml4 = create_address_space();
    if (!pml4) return -1;
    
    // Загружаем ELF
    u64 entry = elf_load(data, size, pml4);
    if (entry == 0) {
        free_address_space(pml4);
        return -1;
    }
    
    // Освобождаем старое адресное пространство
    if (p->cr3 && p->cr3 != (u64)0x1000) {
        free_address_space((u64*)p->cr3);
    }
    
    // Устанавливаем новое
    p->cr3 = (u64)pml4;
    p->user_rip = entry;
    
    return 0;
}
