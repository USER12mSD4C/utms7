#ifndef MODULE_H
#define MODULE_H

#include "types.h"
#include "../kernel/elf.h"

typedef struct loaded_module {
    char name[64];
    u8* text_base;
    u8* data_base;
    u8* rodata_base;
    u8* bss_base;
    u32 text_size;
    u32 data_size;
    u32 rodata_size;
    u32 bss_size;
    int (*entry)(void);
    u32 symtab_count;
    elf64_sym_t* symtab;
    char* strtab;
    struct loaded_module* next;
} loaded_module_t;

int kmod_load_all(void);
int module_load(const char* path);
void* module_sym(const char* name, const char* module);
int module_unload(const char* name);

#endif
