#ifndef ELF_H
#define ELF_H

#include "../include/types.h"
#include "sched.h"

#define ELF_MAGIC 0x464C457F
#define ET_EXEC 2
#define ET_DYN 3
#define PT_LOAD 1

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8
#define SHT_REL      9

#define SHN_UNDEF 0

#define ELF64_R_SYM(info)  ((info) >> 32)
#define ELF64_R_TYPE(info) ((info) & 0xFFFFFFFF)
#define ELF64_ST_BIND(info) ((info) >> 4)
#define ELF64_ST_TYPE(info) ((info) & 0xf)

#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_PLT32    4
#define R_X86_64_32       10
#define R_X86_64_32S      11

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

typedef struct {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
} __attribute__((packed)) elf64_shdr_t;

typedef struct {
    u32 st_name;
    u8  st_info;
    u8  st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
} __attribute__((packed)) elf64_sym_t;

typedef struct {
    u64 r_offset;
    u64 r_info;
    i64 r_addend;
} __attribute__((packed)) elf64_rela_t;

u64 elf_load(u8 *data, u32 size, u64* pml4, u64* out_max_vaddr);
int elf_load_current(u8 *data, u32 size, process_t *p);

#endif
