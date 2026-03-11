#include "../drivers/vga.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "memory.h"
#include "../include/io.h"

#define MODULE_MAGIC 0x4B4F4D55

typedef struct {
    u32 magic;
    char name[32];
    u32 class;
    u32 version;
    u32 text_offset;
    u32 data_offset;
    u32 rodata_offset;
    u32 bss_size;
    u32 text_size;
    u32 data_size;
    u32 rodata_size;
    u32 entry_point;
    u32 symtab_offset;
    u32 symtab_count;
    u32 strtab_offset;
    u32 strtab_size;
} module_header_t;

typedef struct {
    u32 name_offset;
    u32 value_offset;
    u8 type;
    u8 bind;
    u16 shndx;
} module_sym_t;

typedef struct loaded_module {
    char name[32];
    void* base;
    int (*entry)(void);
    struct loaded_module* next;
    u32 symtab_count;
    module_sym_t* symtab;
    char* strtab;
} loaded_module_t;

static loaded_module_t* module_list = NULL;

// Поиск символа во всех загруженных модулях
static void* find_any_symbol(const char* name) {
    loaded_module_t* mod = module_list;
    
    while (mod) {
        for (u32 i = 0; i < mod->symtab_count; i++) {
            char* sym_name = mod->strtab + mod->symtab[i].name_offset;
            if (strcmp(sym_name, name) == 0) {
                return mod->base + mod->symtab[i].value_offset;
            }
        }
        mod = mod->next;
    }
    return NULL;
}

// Разрешение символов модуля
static void resolve_module_syms(loaded_module_t* mod) {
    for (u32 i = 0; i < mod->symtab_count; i++) {
        if (mod->symtab[i].shndx == 0) { // UNDEF - внешний символ
            char* sym_name = mod->strtab + mod->symtab[i].name_offset;
            void* addr = find_any_symbol(sym_name);
            
            if (addr) {
                u64* target = mod->base + mod->symtab[i].value_offset;
                *target = (u64)addr;
            }
        }
    }
}

// Сканирование памяти в поисках функций
static void scan_memory_for_symbols(void) {
    u8* start = (u8*)0x100000;
    u8* end = (u8*)0x200000;
    
    for (u64 addr = (u64)start; addr < (u64)end - 8; addr++) {
        u8* p = (u8*)addr;
        
        if (p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89 && p[3] == 0xE5) {
            for (int i = 4; i < 100 && addr + i < (u64)end; i++) {
                if (p[i] == 0xC3) {
                    // TODO: добавить найденную функцию в таблицу символов
                    break;
                }
            }
        }
    }
}

static int module_load(const char* path) {
    u8* data;
    u32 size;
    
    if (ufs_read(path, &data, &size) != 0) return -1;
    
    module_header_t* hdr = (module_header_t*)data;
    if (hdr->magic != MODULE_MAGIC) {
        kfree(data);
        return -1;
    }
    
    u32 total = hdr->text_size + hdr->data_size + hdr->rodata_size + hdr->bss_size;
    u8* base = kmalloc(total);
    if (!base) {
        kfree(data);
        return -1;
    }
    
    memcpy(base, data + hdr->text_offset, hdr->text_size);
    memcpy(base + hdr->text_size, data + hdr->data_offset, hdr->data_size);
    memcpy(base + hdr->text_size + hdr->data_size, data + hdr->rodata_offset, hdr->rodata_size);
    memset(base + hdr->text_size + hdr->data_size + hdr->rodata_size, 0, hdr->bss_size);
    
    module_sym_t* symtab = NULL;
    char* strtab = NULL;
    
    if (hdr->symtab_count > 0) {
        symtab = kmalloc(hdr->symtab_count * sizeof(module_sym_t));
        memcpy(symtab, data + hdr->symtab_offset, hdr->symtab_count * sizeof(module_sym_t));
        
        strtab = kmalloc(hdr->strtab_size);
        memcpy(strtab, data + hdr->strtab_offset, hdr->strtab_size);
    }
    
    loaded_module_t* mod = kmalloc(sizeof(loaded_module_t));
    strcpy(mod->name, hdr->name);
    mod->base = base;
    mod->entry = (int (*)(void))(base + hdr->entry_point);
    mod->next = module_list;
    mod->symtab_count = hdr->symtab_count;
    mod->symtab = symtab;
    mod->strtab = strtab;
    module_list = mod;
    
    kfree(data);
    
    resolve_module_syms(mod);
    
    vga_write("  ");
    vga_write(hdr->name);
    int len = strlen(hdr->name);
    for (int j = len; j < 16; j++) vga_putchar(' ');
    
    int result = mod->entry();
    if (result == 0) {
        vga_setcolor(0x0A, 0x00);
        vga_write("OK\n");
    } else {
        vga_setcolor(0x0C, 0x00);
        vga_write("FAILED\n");
    }
    vga_setcolor(0x07, 0x00);
    
    return result;
}

void kinit_run_all(void) {
    vga_write("\nKinit: scanning memory for built-in drivers...\n");
    
    // Сначала сканируем память для встроенных функций
    scan_memory_for_symbols();
    
    vga_write("Kinit: loading modules from /modules/\n");
    
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir("/modules", &entries, &count) != 0) {
        vga_write("No /modules/ directory\n");
        return;
    }
    
    int loaded = 0;
    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        
        int len = strlen(entries[i].name);
        if (len > 3 && strcmp(entries[i].name + len - 3, ".ko") == 0) {
            char path[256];
            snprintf(path, sizeof(path), "/modules/%s", entries[i].name);
            
            if (module_load(path) == 0) loaded++;
        }
    }
    
    kfree(entries);
    
    vga_write("\nKinit: ");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", loaded);
    vga_write(buf);
    vga_write(" modules loaded\n");
}
