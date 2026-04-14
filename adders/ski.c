#include "../drivers/vga.h"
#include "../kernel/memory.h"
#include "../fs/ufs.h"
#include "../drivers/disk.h"
#include "../kernel/gdt.h"
#include "../kernel/idt.h"
#include "syscall.h"
#include "../include/multiboot2.h"
#include "../include/string.h"

char* version = "0.1";

#define ski_check(fnname, init_func, crit, ...) \
    do { \
        vga_write("["); \
        vga_write(fnname); \
        vga_write(":"); \
        int result = init_func(##__VA_ARGS__); \
        if (result != 0) { \
            vga_write("FAIL]\n"); \
            if (crit == 1) { \
                while(1) { __asm__ volatile ("hlt"); } \
            } \
        } else { \
            vga_write("OK]\n"); \
        } \
    } while(0)

static bool multiboot2_get_first_available_memory(u32 mb_info_addr, u64* out_start, u64* out_size) {
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
                    return true;
                }
                entry = (multiboot2_mmap_entry_t*)((u8*)entry + mmap_tag->entry_size);
            }
            break;
        }
        tag = (multiboot2_tag_t*)((u8*)tag + ((tag->size + 7) & ~7));
    }
    return false;
}

int ski() {
    vga_write("ski\n");
    vga_write("version: ");
    vga_write(version);
    vga_write("\n\n");

    ski_check("GDT", gdt_init, 1);
    ski_check("IDT", idt_init, 1);

    u64 mem_start = 0, mem_size = 0;
    get_first_available_memory(mb_info_addr, &mem_start, &mem_size);
    ski_check("memory", memory_init, 1, mem_start, mem_size);

    vga_write("Disks found: ");
    vga_write_num(disk_get_disk_count());
    vga_write("\n");

    int over;

    while(overch > 0) {
        *overch = over--;
        vga_write(overch);
        vga_write("/");
        vga_write(over);
        ski_check(tablefnname, 0, tablefn);
    }

    return 0;
}
