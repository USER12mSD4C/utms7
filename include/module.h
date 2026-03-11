#ifndef MODULE_H
#define MODULE_H

#include "types.h"

#define MODULE_MAGIC 0x4B4F4D55  // "UMOK" - UTMS Module

typedef enum {
    MODULE_EARLY    = 0,
    MODULE_ARCH     = 1,
    MODULE_DRIVER   = 2,
    MODULE_FILESYSTEM = 3,
    MODULE_GRAPHICS = 4,
    MODULE_APP      = 5,
    MODULE_LATE     = 6,
} module_class_t;

typedef struct module_dependency {
    char name[32];
    u32 version_min;
} module_dependency_t;

typedef struct module_header {
    u32 magic;
    char name[32];
    module_class_t class;
    u32 version;
    u32 text_offset;
    u32 data_offset;
    u32 rodata_offset;
    u32 bss_size;
    u32 text_size;
    u32 data_size;
    u32 rodata_size;
    u32 entry_point;
    u32 dep_count;
    module_dependency_t deps[8];
    u32 crc32;
} module_header_t;

typedef struct loaded_module {
    char name[32];
    module_class_t class;
    u32 version;
    void* text_base;
    void* data_base;
    void* rodata_base;
    void* bss_base;
    u32 text_size;
    u32 data_size;
    u32 rodata_size;
    u32 bss_size;
    int (*entry)(void);
    struct loaded_module* next;
} loaded_module_t;

// Ядро: загрузка модулей
int module_load(const char* path);
int module_unload(const char* name);
void* module_sym(const char* name, const char* module);
void module_init_system(void);

// Модули: экспорт символов
#define EXPORT_SYMBOL(func) \
    static const char __sym_##func[] __attribute__((section(".module_symtab"))) = #func; \
    static void* __symptr_##func __attribute__((section(".module_symptr"))) = func;

#define MODULE_INIT(func) \
    static int (*__module_init)(void) __attribute__((section(".module_entry"))) = func;

#endif
