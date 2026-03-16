#include "elf.h"
#include "memory.h"
#include "paging.h"
#include "sched.h"
#include "../include/string.h"

int elf_load_user(u8 *data, void *proc) {
    process_t *p = (process_t*)proc;
    elf64_hdr_t *hdr = (elf64_hdr_t*)data;
    
    if (*(u32*)hdr->ident != ELF_MAGIC) return -1;
    if (hdr->ident[4] != 2) return -1;
    if (hdr->type != ET_EXEC && hdr->type != ET_DYN) return -1;
    if (hdr->machine != 0x3E) return -1;
    
    u64 base = 0x400000;
    if (hdr->type == ET_DYN) {
        base = 0x40000000;
    }
    
    for (int i = 0; i < hdr->phnum; i++) {
        elf64_phdr_t *ph = (elf64_phdr_t*)(data + hdr->phoff + i * hdr->phentsize);
        
        if (ph->type == PT_LOAD) {
            u64 vaddr = ph->vaddr;
            if (hdr->type == ET_DYN) vaddr += base;
            
            u64 memsz = ph->memsz;
            u64 filesz = ph->filesz;
            u64 pages = (memsz + 4095) / 4096;
            
            for (u64 j = 0; j < pages; j++) {
                u64 phys = (u64)kmalloc(4096);
                if (!phys) return -1;
                
                u64 virt = vaddr + j * 4096;
                
                u64 flags = PAGE_PRESENT | PAGE_USER;
                if (ph->flags & 2) flags |= PAGE_WRITABLE;
                
                if (paging_map(phys, virt, flags) != 0) {
                    kfree((void*)phys);
                    return -1;
                }
                
                if (j * 4096 < filesz) {
                    u64 off = ph->offset + j * 4096;
                    u64 size = 4096;
                    if (off + size > ph->offset + filesz) 
                        size = ph->offset + filesz - off;
                    memcpy((void*)virt, data + off, size);
                    
                    if (size < 4096) 
                        memset((void*)(virt + size), 0, 4096 - size);
                } else {
                    memset((void*)virt, 0, 4096);
                }
            }
        }
    }
    
    u64 stack_base = 0x70000000;
    u64 stack_pages = 16;
    
    for (u64 i = 0; i < stack_pages; i++) {
        u64 phys = (u64)kmalloc(4096);
        if (!phys) return -1;
        
        u64 virt = stack_base - (i + 1) * 4096;
        
        if (paging_map(phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            kfree((void*)phys);
            return -1;
        }
        
        memset((void*)virt, 0, 4096);
    }
    
    p->heap_start = 0x60000000;
    p->heap_end = 0x60000000;
    
    p->user_rsp = stack_base - 16;
    p->user_rip = (hdr->type == ET_DYN) ? hdr->entry + base : hdr->entry;
    
    return 0;
}
