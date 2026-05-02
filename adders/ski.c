// adders/ski.c
#include "../drivers/vga.h"
#include "../kernel/memory.h"
#include "../fs/ufs.h"
#include "../drivers/disk.h"
#include "../kernel/gdt.h"
#include "../kernel/idt.h"
#include "../kernel/syscall.h"
#include "../include/multiboot2.h"
#include "../include/string.h"
#include "../kernel/paging.h"
#include "../kernel/sched.h"
#include "../kernel/syscall.h"
#include "../kernel/kinit.h"
#include "../drivers/pci.h"
#include "../net/net.h"
#include "../include/shell_api.h"
#include "../commands/builtin.h"
#include "../commands/fs.h"

extern int disk_commands_init(void);
extern int commands_init(void);
extern int fs_commands_init(void);
extern int shell_init(void);
extern int shell_run(void);
extern int kinit_run_all(void);
extern int sched_start(void);
extern int sched_create_kthread(const char*, void(*)(void*), void*);

static const char* version = "0.1";

// Вспомогательная печать числа
static void print_num(u32 n) {
    char buf[16];
    int i = 0;
    if (n == 0) { vga_putchar('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i-- > 0) vga_putchar(buf[i]);
}

// Получение памяти (оставим как отдельную функцию)
static int get_first_available_memory(u32 mb_info_addr, u64* out_start, u64* out_size) {
    multiboot2_info_header_t* header = (multiboot2_info_header_t*)(u64)mb_info_addr;
    multiboot2_tag_t* tag = (multiboot2_tag_t*)(header + 1);
    while (tag->type != MULTIBOOT2_TAG_END) {
        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            multiboot2_tag_mmap_t* mmap_tag = (multiboot2_tag_mmap_t*)tag;
            multiboot2_mmap_entry_t* entry = (multiboot2_mmap_entry_t*)(mmap_tag + 1);
            u32 entry_count = (mmap_tag->size - sizeof(multiboot2_tag_mmap_t)) / mmap_tag->entry_size;
            for (u32 i = 0; i < entry_count; i++) {
                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    *out_start = entry->base_addr;
                    *out_size = entry->length;
                    return 0;
                }
                entry = (multiboot2_mmap_entry_t*)((u8*)entry + mmap_tag->entry_size);
            }
            break;
        }
        tag = (multiboot2_tag_t*)((u8*)tag + ((tag->size + 7) & ~7));
    }
    return -1;
}

void ski(u64 mb_info_addr) {
    vga_write("ski version ");
    vga_write(version);
    vga_write("\n\n");

    // 1. Критичные ранние инициализации
    if (gdt_init() != 0) { vga_write("[GDT:FAIL]\n"); while(1) __asm__ volatile("hlt"); }
    vga_write("[GDT:OK]\n");
    if (idt_init() != 0) { vga_write("[IDT:FAIL]\n"); while(1) __asm__ volatile("hlt"); }
    vga_write("[IDT:OK]\n");

    // 2. Память
    u64 mem_start, mem_size;
    if (get_first_available_memory((u32)mb_info_addr, &mem_start, &mem_size) != 0) {
        vga_write("[memory:FAIL] no memory map\n");
        while(1) __asm__ volatile("hlt");
    }
    memory_init(mem_start, mem_size);
    vga_write("[memory:OK]\n");

    // 3. Основная таблица (генерируется X‑макросом)
    int total = 0;
    #define X(name, func, crit, ...) total++;
    #include "../kernel/init_table.h"
    #undef X

    __asm__ volatile ("cli");
    int current = 0;
    #define X(name, func, crit, ...) \
        do { \
            current++; \
            vga_write("["); \
            print_num(current); \
            vga_write("/"); \
            print_num(total); \
            vga_write("] "); \
            vga_write(name); \
            vga_write("... "); \
            int res = func(__VA_ARGS__); \
            if (res != 0) { \
                vga_write("FAIL\n"); \
                if (crit) while(1) __asm__ volatile("hlt"); \
            } else { \
                vga_write("OK\n"); \
            } \
        } while(0);
    #include "../kernel/init_table.h"
    #undef X

    vga_write("\nDisks found: ");
    vga_write_num(disk_get_disk_count());
    vga_write("\n");
}
