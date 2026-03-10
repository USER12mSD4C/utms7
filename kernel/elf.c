#include "elf.h"
#include "memory.h"
#include "sched.h"
#include "../include/string.h"
#include "../drivers/vga.h"

int elf_load(u8 *data, u64 *entry, u64 *stack) {
    elf64_hdr_t *hdr = (elf64_hdr_t*)data;
    
    // Проверяем магию
    if (*(u32*)hdr->ident != ELF_MAGIC) {
        return -1;
    }
    
    // Проверяем 64-битность
    if (hdr->ident[4] != 2) { // 2 = ELFCLASS64
        return -1;
    }
    
    *entry = hdr->entry;
    
    // Выделяем стек для программы
    *stack = (u64)kmalloc(STACK_SIZE) + STACK_SIZE - 8;
    
    // Загружаем сегменты
    for (int i = 0; i < hdr->phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t*)(data + hdr->phoff + i * hdr->phentsize);
        
        if (ph->type == PT_LOAD) {
            // Выделяем память для сегмента
            u64 addr = ph->vaddr;
            u64 size = ph->memsz;
            
            // TODO: выделить страницы по этому адресу
            // Пока просто копируем
            memcpy((void*)addr, data + ph->offset, ph->filesz);
            
            // Обнуляем остаток (BSS)
            if (ph->filesz < ph->memsz) {
                memset((void*)(addr + ph->filesz), 0, ph->memsz - ph->filesz);
            }
        }
    }
    
    return 0;
}
