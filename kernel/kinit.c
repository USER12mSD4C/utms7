#include "kinit.h"
#include "memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../include/elf.h"

#define MAX_MODULES 128
#define SCAN_START 0x100000
#define SCAN_END   0x2000000

static kinit_module_t modules[MAX_MODULES];
static int module_count = 0;

// Сигнатуры для поиска функций инициализации
static const u8 init_signature[] = {0x55, 0x48, 0x89, 0xE5}; // push rbp; mov rbp, rsp
static const u8 return_zero[] = {0xB8, 0x00, 0x00, 0x00, 0x00}; // mov eax, 0

// Поиск функций по сигнатуре в памяти
static u64 find_function(u8 *start, u8 *end, const char *name_hint) {
    (void)name_hint;
    
    for (u64 addr = (u64)start; addr < (u64)end; addr += 1) {
        u8 *p = (u8*)addr;
        
        // Ищем начало функции (push rbp)
        if (p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89 && p[3] == 0xE5) {
            // Проверяем, возвращает ли 0 в конце
            for (int i = 0; i < 100; i++) {
                if (p[i] == 0xB8 && p[i+1] == 0x00 && 
                    p[i+2] == 0x00 && p[i+3] == 0x00 && p[i+4] == 0x00) {
                    return addr;
                }
                if (p[i] == 0xC3) break; // ret
            }
        }
    }
    return 0;
}

// Поиск по именам в ELF секциях
static u64 find_by_name(const char *name) {
    extern u8 __text_start;
    extern u8 __text_end;
    extern u8 __rodata_start;
    extern u8 __rodata_end;
    
    char *rodata = (char*)&__rodata_start;
    u64 rodata_size = &__rodata_end - &__rodata_start;
    
    // Ищем имя функции в .rodata
    for (u64 i = 0; i < rodata_size - strlen(name); i++) {
        if (memcmp(rodata + i, name, strlen(name)) == 0) {
            // Нашли имя, ищем адрес рядом
            u64 *addr_ptr = (u64*)(rodata + i - 8);
            if (*addr_ptr > SCAN_START && *addr_ptr < SCAN_END) {
                return *addr_ptr;
            }
        }
    }
    return 0;
}

// Автоматическая регистрация модулей
static void auto_register_modules(void) {
    const char *known_modules[] = {
        "gdt_init", "idt_init", "timer_init",
        "keyboard_init", "mouse_init", "disk_init",
        "vesa_init", "gpt_detect", "ufs_mount",
        NULL
    };
    
    for (int i = 0; known_modules[i] != NULL; i++) {
        u64 addr = find_by_name(known_modules[i]);
        if (!addr) {
            addr = find_function((u8*)SCAN_START, (u8*)SCAN_END, known_modules[i]);
        }
        
        if (addr) {
            int priority = 10;
            if (strstr(known_modules[i], "gdt") || 
                strstr(known_modules[i], "idt")) priority = 0;
            if (strstr(known_modules[i], "timer")) priority = 5;
            
            kinit_register(known_modules[i], (int (*)(void))addr, priority);
        }
    }
}

// Сортировка по приоритету
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

void kinit_register(const char* name, int (*init)(void), int priority) {
    if (module_count >= MAX_MODULES) return;
    
    strcpy(modules[module_count].name, name);
    modules[module_count].init = init;
    modules[module_count].priority = priority;
    modules[module_count].status = -1;
    modules[module_count].addr_start = (u64)init;
    module_count++;
}

void kinit_run_all(void) {
    vga_write("Kinit: Scanning for modules...\n");
    
    // Автоматически находим все модули
    auto_register_modules();
    
    // Сортируем по приоритету
    sort_modules();
    
    // Запускаем
    vga_write("Kinit: Initializing modules\n");
    
    int success = 0;
    for (int i = 0; i < module_count; i++) {
        vga_write("  ");
        vga_write(modules[i].name);
        
        int len = strlen(modules[i].name);
        for (int j = len; j < 20; j++) vga_putchar(' ');
        
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
    
    vga_write("Kinit: ");
    vga_write("Loaded ");
    vga_putchar('0' + (module_count / 10));
    vga_putchar('0' + (module_count % 10));
    vga_write(" modules (");
    vga_putchar('0' + (success / 10));
    vga_putchar('0' + (success % 10));
    vga_write(" OK)\n");
}

// Функции для сканирования секций (добавить в linker.ld)
void kinit_scan_sections(void) {
    extern u8 __modules_start;
    extern u8 __modules_end;
    
    u64 *mod_table = (u64*)&__modules_start;
    u64 mod_count = (&__modules_end - &__modules_start) / sizeof(u64) / 2;
    
    for (u64 i = 0; i < mod_count; i++) {
        u64 name_addr = mod_table[i * 2];
        u64 func_addr = mod_table[i * 2 + 1];
        
        if (name_addr && func_addr) {
            const char *name = (const char*)name_addr;
            kinit_register(name, (int (*)(void))func_addr, 10);
        }
    }
}
