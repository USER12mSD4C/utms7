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
#include "../drivers/keyboard.h"

extern u64 __bss_end;

extern int disk_commands_init(void);
extern int commands_init(void);
extern int fs_commands_init(void);
extern int shell_init(void);
extern int shell_run(void);
extern int kinit_run_all(void);
extern int sched_start(void);
extern int sched_create_kthread(const char*, void(*)(void*), void*);

static const char* version = "0.2";

static void print_num(u32 n) {
    char buf[16];
    int i = 0;
    if (n == 0) { vga_putchar('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i-- > 0) vga_putchar(buf[i]);
}

static void init_memory_from_multiboot(u32 mb_info_addr) {
    multiboot2_info_header_t* header = (multiboot2_info_header_t*)(u64)mb_info_addr;
    multiboot2_tag_t* tag = (multiboot2_tag_t*)(header + 1);

    int found = 0;
    u64 first_start = 0, first_size = 0;

    while (tag->type != MULTIBOOT2_TAG_END) {
        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            multiboot2_tag_mmap_t* mmap_tag = (multiboot2_tag_mmap_t*)tag;
            multiboot2_mmap_entry_t* entry = (multiboot2_mmap_entry_t*)(mmap_tag + 1);
            u32 entry_count = (mmap_tag->size - sizeof(multiboot2_tag_mmap_t)) / mmap_tag->entry_size;

            for (u32 i = 0; i < entry_count; i++) {
                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    if (!found) {
                        first_start = entry->base_addr;
                        first_size = entry->length;
                        found = 1;
                        if (first_start < (u64)&__bss_end) {
                            u64 adjust = (u64)&__bss_end - first_start;
                            first_start = (u64)&__bss_end;
                            if (first_size > adjust) first_size -= adjust;
                            else first_size = 0;
                        }
                        memory_init(first_start, first_size);
                    } else {
                        memory_add_region(entry->base_addr, entry->length);
                    }
                }
                entry = (multiboot2_mmap_entry_t*)((u8*)entry + mmap_tag->entry_size);
            }
        }
        tag = (multiboot2_tag_t*)((u8*)tag + ((tag->size + 7) & ~7));
    }

    if (!found) {
        vga_write("[memory:FAIL] no available memory\n");
        while(1) __asm__ volatile("hlt");
    }
}

void ski(u64 mb_info_addr) {
    vga_write("ski version ");
    vga_write(version);
    vga_write("\n\n");

    // === ЭТАП 0: GDT, IDT, TSS ===
    __asm__ volatile ("cli");

    vga_write("[GDT]... ");
    if (gdt_init() != 0) {
        vga_write("FAIL\n");
        while(1) __asm__ volatile("hlt");
    }
    vga_write("OK\n");

    vga_write("[IDT]... ");
    if (idt_init() != 0) {
        vga_write("FAIL\n");
        while(1) __asm__ volatile("hlt");
    }
    vga_write("OK\n");

    tss_init();
    vga_write("[TSS]... OK\n");

    __asm__ volatile ("sti");

    // === ЭТАП 1: Память ===
    vga_write("[memory]... ");
    init_memory_from_multiboot((u32)mb_info_addr);
    vga_write("OK\n\n");

    // === ЭТАП 2-7: Всё остальное из init_table.h ===
    int total = 0;
    #define X(name, func, crit, ...) total++;
    #include "../kernel/init_table.h"
    #undef X

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
            for (int _i = 0; _i < 20 - sizeof(name); _i++) vga_putchar(' '); \
            int res = func(__VA_ARGS__); \
            if (res != 0) { \
                vga_write("FAIL (code="); \
                print_num(res); \
                vga_write(")\n"); \
                if (crit) { \
                    vga_write("CRITICAL FAILURE, HALTING\n"); \
                    while(1) __asm__ volatile("hlt"); \
                } \
            } else { \
                vga_write("OK\n"); \
            } \
        } while(0);
    #include "../kernel/init_table.h"
    #undef X

    vga_write("\nDisks found: ");
    vga_write_num(disk_get_disk_count());
    vga_write("\n");

    vga_write("shi done nga\n\n");
}
