// kernel/kernel.c
#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"
#include "gdt.h"
#include "idt.h"
#include "sched.h"
#include "syscall.h"      // НОВОЕ
#include "../drivers/pci.h"
#include "../drivers/disk.h"
#include "../net/net.h"
#include "../fs/ufs.h"
#include "../commands/builtin.h"
#include "../commands/fs.h"
#include "../include/shell_api.h"

extern void disk_commands_init(void);
extern void syscall_init(void);   // НОВОЕ

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS v0.4 - Full Network Edition\n");
    
    vga_write("[1/9] GDT... ");
    gdt_init();
    vga_write("OK\n");
    
    vga_write("[2/9] IDT... ");
    idt_init();
    vga_write("OK\n");
    
    vga_write("[3/9] Memory... ");
    memory_init(0x100000, 32 * 1024 * 1024);
    vga_write("OK\n");
    
    vga_write("[4/9] Paging... ");
    if (paging_init() != 0) {
        vga_write("FAILED\n");
        while(1);
    }
    vga_write("OK\n");
    
    vga_write("[5/9] Disk... ");
    disk_init();
    vga_write("OK\n");
    
    vga_write("[6/9] Scheduler... ");
    sched_init();
    vga_write("OK\n");
    
    vga_write("[7/9] Syscalls... ");
    syscall_init();   // НОВОЕ (вместо kapi_init)
    vga_write("OK\n");
    
    vga_write("[8/9] PCI... ");
    pci_init();
    vga_write("OK\n");
    
    vga_write("[9/9] Network... ");
    net_init();
    vga_write("OK\n");
    
    __asm__ volatile ("sti");
    
    vga_write("\nMounting UFS... ");
    if (ufs_mount(2048, 0) == 0) {
        vga_write("OK\n");
    } else {
        vga_write("FAILED\n");
    }
    
    vga_write("Disks found: ");
    vga_write_num(disk_get_disk_count());
    vga_write("\n");
    
    vga_write("\nShell init... ");
    shell_init();
    commands_init();
    fs_commands_init();
    disk_commands_init();
    vga_write("OK\n");
    
    vga_write("\nUTMS is ready!\n");
    vga_write("Type 'help' for commands\n");
    
    shell_run();
}
