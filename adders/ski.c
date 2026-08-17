#include "../drivers/drm.h"
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
#include "../kernel/kinit.h"
#include "../drivers/pci.h"
#include "../net/net.h"
#include "../include/shell_api.h"
#include "../drivers/keyboard.h"
#include "../include/udisk.h"

extern u64 __bss_end;

extern int shell_init(void);
extern int shell_run(void);
extern int kinit_run_all(void);
extern int sched_start(void);
extern int sched_create_kthread(const char*, void(*)(void*), void*);

static const char* version = "0.2";

static void print_num(u32 n) {
    char buf[16];
    int i = 0;
    if (n == 0) { print_char('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i-- > 0) print_char(buf[i]);
}

static void print_int(int n) {
    if (n < 0) {
        print_char('-');
        print_num((u32)(-n));
    } else {
        print_num((u32)n);
    }
}

static int mem_found = 0;
static int pmm_done = 0;

static void add_avail_region(u64 s, u64 e) {
    if (e <= s) return;

    if (!pmm_done && (e - s) >= 16 * 1024 * 1024) {
        u64 psz = (e - s) / 4;
        if (psz > 256 * 1024 * 1024) psz = 256 * 1024 * 1024;
        pmm_init_region(s, psz);
        pmm_done = 1;
    }

    if (!mem_found) {
        memory_init(s, e - s);
        mem_found = 1;
    } else {
        memory_add_region(s, e - s);
    }
}

static void init_memory_from_multiboot(u64 mb_info_addr) {
    multiboot2_info_header_t* header = (multiboot2_info_header_t*)mb_info_addr;
    multiboot2_tag_t* tag = (multiboot2_tag_t*)(header + 1);
    u64 kend = (u64)&__bss_end;
    u64 mod_lo = 0;
    u64 mod_hi = 0;
    while (tag->type != MULTIBOOT2_TAG_END) {
        if (tag->type == 3) {
            u64 ms = *(u32*)((u8*)tag + 8);
            u64 me = *(u32*)((u8*)tag + 12);
            if (mod_lo == 0 || ms < mod_lo) mod_lo = ms;
            if (me > mod_hi) mod_hi = me;
        }
        tag = (multiboot2_tag_t*)((u8*)tag + ((tag->size + 7) & ~7));
    }

    const u64 low_limit = 0x40000000;
    mem_found = 0;
    pmm_done = 0;

    tag = (multiboot2_tag_t*)(header + 1);
    while (tag->type != MULTIBOOT2_TAG_END) {
        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            multiboot2_tag_mmap_t* mmap_tag = (multiboot2_tag_mmap_t*)tag;
            multiboot2_mmap_entry_t* entry = (multiboot2_mmap_entry_t*)(mmap_tag + 1);
            u32 entry_count = (mmap_tag->size - sizeof(multiboot2_tag_mmap_t)) / mmap_tag->entry_size;
            for (u32 i = 0; i < entry_count; i++) {
                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    u64 s = entry->base_addr;
                    u64 e = entry->base_addr + entry->length;
                    if (e > low_limit) e = low_limit;
                    if (s < kend) s = kend;
                    if (e > s && mod_hi > mod_lo) {
                        u64 p1e = (e < mod_lo) ? e : mod_lo;
                        if (p1e > s) add_avail_region(s, p1e);
                        u64 p2s = (s > mod_hi) ? s : mod_hi;
                        if (e > p2s) add_avail_region(p2s, e);
                    } else if (e > s) {
                        add_avail_region(s, e);
                    }
                }
                entry = (multiboot2_mmap_entry_t*)((u8*)entry + mmap_tag->entry_size);
            }
        }
        tag = (multiboot2_tag_t*)((u8*)tag + ((tag->size + 7) & ~7));
    }
    if (!mem_found) {
        print("[memory:FAIL] no available memory\n");
        while(1) __asm__ volatile("hlt");
    }
}

static void automount_first_ufs(void) {
    extern int udisk_scan(void);
    extern disk_info_t* udisk_get_info(int disk_num);
    extern int ufs_mount_with_point(u32 start_lba, int disk, const char* point);
    extern void fs_set_current_dir(const char*);

    udisk_scan();

    for (int i = 0; i < 4; i++) {
        disk_info_t* d = udisk_get_info(i);
        if (!d || !d->present) continue;

        for (int j = 0; j < d->partition_count; j++) {
            partition_t* p = &d->partitions[j];
            if (!p->present || p->type != PARTITION_UFS) continue;

            if (ufs_mount_with_point(p->start_lba, p->disk_num, "/") == 0) {
                print_setcolor(0x0A, 0);
                print("[UFS] mounted /dev/sd");
                print_char('a' + i);
                if (p->partition_num > 0) printnum(p->partition_num);
                print(" on /\n");
                print_setcolor(0x07, 0);
                fs_set_current_dir("/");
                return;
            }
        }
    }

    print_setcolor(0x0E, 0);
    print("[UFS] no UFS partition found, using RAM only\n");
    print_setcolor(0x07, 0);
}

void ski(u64 mb_info_addr) {
    print("ski version ");
    print(version);
    print("\n\n");
    __asm__ volatile ("cli");
    print_setcolor(0x0E, 0x00);
    print("[GDT]... ");
    print_setcolor(0x07, 0x00);
    if (gdt_init() != 0) {
        print_setcolor(0x04, 0x00);
        print("FAIL\n");
        print_setcolor(0x07, 0x00);
        while(1) __asm__ volatile("hlt");
    }
    print_setcolor(0x0A, 0x00);
    print("OK\n");
    print_setcolor(0x07, 0x00);

    print_setcolor(0x0E, 0x00);
    print("[IDT]... ");
    print_setcolor(0x07, 0x00);
    if (idt_init() != 0) {
        print_setcolor(0x04, 0x00);
        print("FAIL\n");
        print_setcolor(0x07, 0x00);
        while(1) __asm__ volatile("hlt");
    }
    print_setcolor(0x0A, 0x00);
    print("OK\n");
    print_setcolor(0x07, 0x00);

    tss_init();
    print_setcolor(0x0E, 0x00);
    print("[TSS]... ");
    print_setcolor(0x0A, 0x00);
    print("OK\n");
    print_setcolor(0x07, 0x00);

    __asm__ volatile ("sti");
    print_setcolor(0x0E, 0x00);
    print("[memory]... ");
    print_setcolor(0x07, 0x00);
    init_memory_from_multiboot(mb_info_addr);
    print_setcolor(0x0A, 0x00);
    print("OK\n");
    print("\n");
    print_setcolor(0x07, 0x00);

    print("\nDisks found: ");
    printnum(disk_get_disk_count());
    print("\n");
    automount_first_ufs();
    print("\n");

    int total = 0;
    #define X(name, func, crit, ...) total++;
    #include "../kernel/init_table.h"
    #undef X
    int current = 0;
    #define X(name, func, crit, ...) \
        do { \
            current++; \
            print_setcolor(0x0B, 0x00); \
            print("["); \
            print_num(current); \
            print("/"); \
            print_num(total); \
            print("] "); \
            print_setcolor(0x07, 0x00); \
            print(name); \
            int pad = 24 - (int)(sizeof(name) - 1); \
            for (int _i = 0; _i < pad; _i++) print_char(' '); \
            int res = func(__VA_ARGS__); \
            if (res != 0) { \
                if (crit) { \
                    print_setcolor(0x0C, 0x00); \
                    print("CRITICAL FAIL (code="); \
                } else { \
                    print_setcolor(0x0E, 0x00); \
                    print("SKIP (code="); \
                } \
                print_int(res); \
                print(")\n"); \
                if (crit) { \
                    print_setcolor(0x0C, 0x00); \
                    print("CRITICAL FAILURE, HALTING\n"); \
                    while(1) __asm__ volatile("hlt"); \
                } \
            } else { \
                print_setcolor(0x0A, 0x00); \
                print("OK\n"); \
            } \
            print_setcolor(0x07, 0x00); \
        } while(0);
    #include "../kernel/init_table.h"
    #undef X

    print("\nUTMS Kernel loaded\\\\\nUTMS Innovative Technologies [UIT], under UOPL_1.6.4\n\n");
}
