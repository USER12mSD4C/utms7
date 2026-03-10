#include "kinit.h"
#include "memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"

#define MAX_MODULES 128
#define SCAN_START 0x100000
#define SCAN_END   0x200000

static kinit_module_t modules[MAX_MODULES];
static int module_count = 0;

// Внешние символы из linker.ld
extern u8 __text_start;
extern u8 __text_end;
extern u8 __rodata_start;
extern u8 __rodata_end;
extern u8 __modules_start;
extern u8 __modules_end;

// Поиск функции по сигнатуре (push rbp; mov rbp, rsp)
static u64 find_function_by_signature(u8 *start, u8 *end) {
    for (u64 addr = (u64)start; addr < (u64)end - 8; addr++) {
        u8 *p = (u8*)addr;
        
        // Пролог функции: push rbp; mov rbp, rsp
        if (p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89 && p[3] == 0xE5) {
            // Проверяем что это не мусор (ищем ret в конце)
            for (int i = 4; i < 100 && addr + i < (u64)end; i++) {
                if (p[i] == 0xC3) { // ret
                    return addr;
                }
                if (p[i] == 0x5D) { // pop rbp
                    return addr;
                }
            }
        }
    }
    return 0;
}

// Поиск по именам в .rodata
static u64 find_function_by_name(const char *name) {
    char *rodata = (char*)&__rodata_start;
    u64 rodata_size = &__rodata_end - &__rodata_start;
    
    for (u64 i = 0; i < rodata_size - strlen(name); i++) {
        if (memcmp(rodata + i, name, strlen(name)) == 0) {
            // Ищем указатель на функцию рядом (до 32 байт в обе стороны)
            for (int off = -32; off <= 32; off += 8) {
                if (i + off >= 0 && i + off + 8 < rodata_size) {
                    u64 *ptr = (u64*)(rodata + i + off);
                    if (*ptr > SCAN_START && *ptr < SCAN_END) {
                        return *ptr;
                    }
                }
            }
        }
    }
    return 0;
}

// Сканирование специальной секции .kinit.modules
void kinit_scan_sections(void) {
    u64 *mod_table = (u64*)&__modules_start;
    u64 mod_entries = (&__modules_end - &__modules_start) / (sizeof(u64) * 2);
    
    for (u64 i = 0; i < mod_entries; i++) {
        u64 name_addr = mod_table[i * 2];
        u64 func_addr = mod_table[i * 2 + 1];
        
        if (name_addr && func_addr && func_addr > SCAN_START && func_addr < SCAN_END) {
            const char *name = (const char*)name_addr;
            
            // Определяем приоритет по имени
            int priority = 10;
            if (strstr(name, "gdt") || strstr(name, "idt")) priority = 0;
            else if (strstr(name, "timer")) priority = 5;
            else if (strstr(name, "keyboard")) priority = 10;
            else if (strstr(name, "mouse")) priority = 10;
            else if (strstr(name, "disk")) priority = 15;
            else if (strstr(name, "vesa")) priority = 15;
            else if (strstr(name, "gpt")) priority = 20;
            else if (strstr(name, "ufs")) priority = 20;
            
            kinit_register(name, (int (*)(void))func_addr, priority);
        }
    }
}

// Сканирование памяти в поисках функций инициализации
void kinit_scan_memory(void) {
    const char *known_names[] = {
        "keyboard_init", "mouse_init", "disk_init",
        "vesa_init", "gpt_detect", "ufs_mount",
        NULL
    };
    
    // Сначала ищем по именам
    for (int i = 0; known_names[i] != NULL; i++) {
        u64 addr = find_function_by_name(known_names[i]);
        if (addr) {
            int priority = 10;
            if (strstr(known_names[i], "keyboard")) priority = 10;
            else if (strstr(known_names[i], "mouse")) priority = 10;
            else if (strstr(known_names[i], "disk")) priority = 15;
            else if (strstr(known_names[i], "vesa")) priority = 15;
            else if (strstr(known_names[i], "gpt")) priority = 20;
            else if (strstr(known_names[i], "ufs")) priority = 20;
            
            // Проверяем, не зарегистрировали ли уже
            int found = 0;
            for (int j = 0; j < module_count; j++) {
                if (modules[j].addr == addr) {
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                kinit_register(known_names[i], (int (*)(void))addr, priority);
            }
        }
    }
}

void kinit_register(const char* name, int (*init)(void), int priority) {
    if (module_count >= MAX_MODULES) return;
    if (!init) return;
    
    strcpy(modules[module_count].name, name);
    modules[module_count].init = init;
    modules[module_count].priority = priority;
    modules[module_count].status = -1;
    modules[module_count].addr = (u64)init;
    module_count++;
}

static void sort_modules(void) {
    for (int i = 0; i < module_count - 1; i++) {
        for (int j = 0; j < module_count - i - 1; j++) {
            if (modules[j].priority > modules[j + 1].priority) {
                kinit_module_t temp = modules[j];
                modules[j] = modules[j + 1];
                modules[j + 1] = temp;
            }
        }
    }
}

void kinit_run_all(void) {
    vga_write("Kinit: Scanning for modules...\n");
    
    // Сканируем специальную секцию
    kinit_scan_sections();
    
    // Сканируем память
    kinit_scan_memory();
    
    // Сортируем по приоритету
    sort_modules();
    
    // Запускаем
    vga_write("Kinit: Initializing modules\n");
    
    int success = 0;
    for (int i = 0; i < module_count; i++) {
        vga_write("  [");
        vga_write(modules[i].name);
        
        int len = strlen(modules[i].name);
        for (int j = len; j < 20; j++) vga_putchar(' ');
        
        vga_write("] ");
        
        if (modules[i].init) {
            int result = modules[i].init();
            modules[i].status = result;
            
            if (result == 0) {
                vga_setcolor(0x0A, 0x00);
                vga_write("OK");
                success++;
            } else {
                vga_setcolor(0x0C, 0x00);
                vga_write("FAILED");
            }
            vga_setcolor(0x07, 0x00);
        } else {
            vga_write("NO INIT");
        }
        vga_putchar('\n');
    }
    
    vga_write("Kinit: Loaded ");
    vga_putchar('0' + (module_count / 10));
    vga_putchar('0' + (module_count % 10));
    vga_write(" modules (");
    vga_putchar('0' + (success / 10));
    vga_putchar('0' + (success % 10));
    vga_write(" OK)\n");
}
