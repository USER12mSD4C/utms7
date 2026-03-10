#ifndef ELF_H
#define ELF_H

#include "../include/types.h"

#define ELF_MAGIC 0x464C457F // "\x7FELF"

typedef struct {
    u8  ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} __attribute__((packed)) elf64_hdr_t;

typedef struct {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} __attribute__((packed)) elf64_phdr_t;

#define PT_LOAD 1

int elf_load(u8 *data, u64 *entry, u64 *stack);

#endif
