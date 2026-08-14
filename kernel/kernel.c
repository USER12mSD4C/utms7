// kernel/kernel.c
#include "../drivers/drm.h"
#include "../adders/ski.h"
#include "../kernel/memory.h"
#include "../include/string.h"

static u64 multiboot_info_ptr = 0;

void kernel_main(void *mb_info) {
    multiboot_info_ptr = (u64)mb_info;

    drm_parse_multiboot((u64)mb_info);
    drm_init();

    print_clear();
    print("hello world, UTMS7 is booting...\n");

    ski((u64)mb_info);

    while (1) {
        __asm__ volatile ("hlt");
    }
}

int multiboot_get_module(const char* name, u8** start, u32* size) {
    if (multiboot_info_ptr == 0) {
        print("[mb] info_ptr is NULL\n");
        return -1;
    }

    u8* ptr = (u8*)(multiboot_info_ptr + 8);
    int mod_index = 0;
    while (1) {
        u32 type = *(u32*)ptr;
        u32 tag_size = *(u32*)(ptr + 4);
        if (type == 0 || tag_size == 0) break;

        if (type == 3) {
            u64 mod_start = *(u32*)(ptr + 8);
            u64 mod_end = *(u32*)(ptr + 12);
            const char* cmdline = (const char*)(ptr + 16);

            print("[mb] module ");
            printnum(mod_index++);
            print(": start=");
            printhex(mod_start);
            print(" end=");
            printhex(mod_end);
            print(" cmdline='");
            print(cmdline);
            print("'\n");

            int match = 0;
            int i = 0;
            for (; cmdline[i] != '\0' && name[i] != '\0'; i++) {
                if (cmdline[i] != name[i]) break;
            }
            if (cmdline[i] == '\0' && name[i] == '\0') match = 1;

            if (match) {
                *start = (u8*)mod_start;
                *size = (u32)(mod_end - mod_start);
                return 0;
            }
        }
        ptr += (tag_size + 7) & ~7;
    }
    print("[mb] module '");
    print(name);
    print("' not found\n");
    return -1;
}

int get_module_data(const char* name, u8** buf, u32* size) {
    u8* start = NULL;
    u32 sz = 0;
    if (multiboot_get_module(name, &start, &sz) == 0) {
        u8* kbuf = kmalloc(sz);
        if (kbuf) {
            memcpy(kbuf, start, sz);
            *buf = kbuf;
            *size = sz;
            return 0;
        }
    }
    return -1;
}

void shell_print(const char* s) {
    print(s);
}

void shell_print_num(u64 n) {
    printnum(n);
}

void shell_print_hex(u64 n) {
    printhex(n);
}

int shell_register_command(const char* name, int (*func)(int, char**), const char* desc) {
    (void)name; (void)func; (void)desc;
    return 0;
}

int upac_main(int argc, char** argv) {
    (void)argc; (void)argv;
    return 0;
}
