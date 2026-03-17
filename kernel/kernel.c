#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"
#include "gdt.h"
#include "idt.h"
#include "sched.h"
#include "kapi.h"
#include "../drivers/pci.h"
#include "../drivers/disk.h"  // ← ДОБАВИТЬ ЭТУ СТРОКУ
#include "../net/rtl8139.h"
#include "../net/net.h"
#include "../fs/ufs.h"
#include "../commands/builtin.h"
#include "../commands/fs.h"
#include "../include/shell_api.h"

void disk_commands_init(void);

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS v0.3 - Network Edition\n");
    
    vga_write("[1/8] GDT... ");
    gdt_init();
    vga_write("OK\n");
    
    vga_write("[2/8] IDT... ");
    idt_init();
    vga_write("OK\n");
    
    vga_write("[3/8] Memory... ");
    memory_init(0x100000, 32 * 1024 * 1024);
    vga_write("OK\n");
    
    vga_write("[4/8] Paging... ");
    if (paging_init() != 0) {
        vga_write("FAILED\n");
        while(1);
    }
    vga_write("OK\n");
    
    vga_write("[7.5/8] Disk... ");
    disk_init();
    vga_write("OK\n");
    
    vga_write("[5/8] Scheduler... ");
    sched_init();
    vga_write("OK\n");
    
    vga_write("[6/8] Syscalls... ");
    kapi_init();
    vga_write("OK\n");
    
    vga_write("[7/8] PCI... ");
    pci_init();
    vga_write("OK\n");
    
    vga_write("[8/8] Network... ");
    net_init();
    vga_write("OK\n");
    
    __asm__ volatile ("sti");
    
    vga_write("\nMounting UFS... ");
    if (ufs_mount(2048, 0) == 0) {
        vga_write("OK\n");
    } else {
        vga_write("FAILED\n");
    }
    
    // disk_get_disk_count() теперь доступна
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
    vga_write("Type 'upac -Sy' to sync packages\n");
    
    shell_run();
}
