#include "../drivers/drm.h"
#include "../adders/ski.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../include/multiboot2.h"

static u8 multiboot_info_copy[8192] __attribute__((aligned(16)));
static u64 multiboot_info_ptr = 0;

void kernel_main(void *mb_info) {
    multiboot2_info_header_t* hdr = (multiboot2_info_header_t*)mb_info;
    u32 sz = hdr->total_size;
    if (sz > sizeof(multiboot_info_copy)) sz = sizeof(multiboot_info_copy);
    memcpy(multiboot_info_copy, mb_info, sz);
    multiboot_info_ptr = (u64)multiboot_info_copy;

    drm_parse_multiboot((u64)multiboot_info_copy);
    drm_init();

    print_clear();
    print("hello world, UTMS7 is booting...\n");

    ski((u64)multiboot_info_copy);

    while (1) {
        __asm__ volatile ("hlt");
    }
}

static int module_name_match(const char* cmdline, const char* name) {
    if (!cmdline || !name) return 0;

    if (strcmp(cmdline, name) == 0) return 1;

    u64 name_len = strlen(name);
    const char* p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        const char* start = p;
        while (*p && *p != ' ' && *p != '\t') p++;

        u64 token_len = (u64)(p - start);

        if (token_len == name_len && strncmp(start, name, name_len) == 0) {
            return 1;
        }

        const char* base = start;
        for (const char* q = start; q < start + token_len; q++) {
            if (*q == '/') base = q + 1;
        }

        u64 base_len = (u64)(start + token_len - base);

        if (base_len == name_len && strncmp(base, name, name_len) == 0) {
            return 1;
        }

        if (base_len > name_len && strncmp(base, name, name_len) == 0 && base[name_len] == '.') {
            return 1;
        }
    }

    return 0;
}

int multiboot_get_module(const char* name, u8** start, u32* size) {
    if (multiboot_info_ptr == 0) {
        print("[mb] info_ptr is NULL\n");
        return -1;
    }

    u8* ptr = (u8*)(multiboot_info_ptr + 8);

    while (1) {
        u32 type = *(u32*)ptr;
        u32 tag_size = *(u32*)(ptr + 4);

        if (type == 0 || tag_size == 0) break;

        if (type == 3 && tag_size > 16) {
            u64 mod_start = *(u32*)(ptr + 8);
            u64 mod_end = *(u32*)(ptr + 12);
            const char* cmdline = (const char*)(ptr + 16);

            if (module_name_match(cmdline, name)) {
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
