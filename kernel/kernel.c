#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"
#include "gdt.h"
#include "idt.h"
#include "sched.h"
#include "syscall.h"
#include "../drivers/pci.h"
#include "../drivers/disk.h"
#include "../net/net.h"
#include "../fs/ufs.h"
#include "../include/shell_api.h"
#include "../commands/builtin.h"
#include "../commands/fs.h"

extern void disk_commands_init(void);
extern void commands_init(void);
extern void fs_commands_init(void);
extern void shell_init(void);
extern void shell_run(void);
extern void kinit_run_all(void);

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    vga_init();
    vga_clear();
    vga_write("UTMS v0.4 - Network Edition\n");
    
    vga_write("[1/9] GDT... ");
    if (gdt_init() != 0) {
        vga_write("FAILED\n");
        while(1) { __asm__ volatile ("hlt"); }
    }
    vga_write("OK\n");
    
    vga_write("[2/9] IDT... ");
    if (idt_init() != 0) {
        vga_write("FAILED\n");
        while(1) { __asm__ volatile ("hlt"); }
    }
    vga_write("OK\n");
    
    vga_write("[3/9] Memory... ");
    memory_init(0x100000, 64 * 1024 * 1024);
    vga_write("OK\n");
    
    vga_write("[4/9] Paging... ");
    if (paging_init() != 0) {
        vga_write("FAILED\n");
        while(1);
    }
    vga_write("OK\n");
    
    vga_write("[5/9] Timer... ");
    timer_init();
    vga_write("OK\n");
    
    vga_write("[6/9] Scheduler... ");
    sched_init();
    vga_write("OK\n");
    
    vga_write("[7/9] Syscalls... ");
    syscall_init();
    vga_write("OK\n");
    
    vga_write("[8/9] Disk... ");
    disk_init();
    vga_write("OK\n");
    
    vga_write("[9/9] PCI... ");
    pci_init();
    vga_write("OK\n");
    
    vga_write("\nNetwork init... ");
    net_init();
    vga_write("OK\n");
    
    vga_write("Mounting UFS... ");
    if (ufs_mount(2048, 0) == 0) {
        vga_write("OK\n");
    } else {
        vga_write("FAILED\n");
    }
    
    vga_write("Disks found: ");
    vga_write_num(disk_get_disk_count());
    vga_write("\n");
    
    vga_write("Loading modules... ");
    kinit_run_all();
    vga_write("OK\n");
    
    vga_write("\nShell init... ");
    shell_init();
    commands_init();
    fs_commands_init();
    disk_commands_init();
    vga_write("OK\n");
    
    vga_write("\nUTMS is ready!\n");
    vga_write("Type 'help' for commands\n");
    vga_write("Type 'upac -Sy' to sync packages\n\n");
    
    __asm__ volatile ("sti");
    shell_run();
    
    while(1) {
        __asm__ volatile ("hlt");
    }
}
