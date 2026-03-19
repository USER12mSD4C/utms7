#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"
#include "gdt.h"
#include "idt.h"
#include "kinit.h"
#include "sched.h"
#include "../include/shell_api.h"
#include "../fs/ufs.h"
#include "../drivers/keyboard.h"
#include "../drivers/disk.h"
#include "../commands/builtin.h"
#include "../commands/fs.h"
#include "../apps/installer.h"
#include "../include/udisk.h"

void disk_commands_init(void);

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS LiveCD v0.3\n");
    vga_write("================\n\n");
    
    vga_write("[1/9] GDT... ");
    gdt_init();
    vga_write("OK\n");
    
    vga_write("[2/9] IDT... ");
    idt_init();
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
    
    vga_write("[7/9] Disk... ");
    disk_init();
    vga_write("OK\n");
    
    vga_write("[8/9] UDISK... ");
    udisk_init();
    vga_write("OK\n");
    
    vga_write("[9/9] Keyboard... ");
    keyboard_init();
    vga_write("OK\n");
    
    __asm__ volatile ("sti");
    
    vga_write("\nScanning disks...\n");
    udisk_scan();
    
// Создаём папку для монтирования
ufs_mkdir("/mnt");
ufs_mkdir("/mnt/livecd");

// Теперь монтируем
vga_write("Mounting LiveCD to /mnt/livecd... ");
if (ufs_mount_with_point(2048, 2, "/mnt/livecd") == 0) {
    vga_write("OK\n");
} else {
    vga_write("FAILED (trying primary master)...\n");
    if (ufs_mount_with_point(2048, 0, "/mnt/livecd") == 0) {
        vga_write("Mounted LiveCD on primary master\n");
    } else {
        vga_write("Failed to mount LiveCD\n");
    }
}
    
    vga_write("Disks found: ");
    vga_write_num(disk_get_disk_count());
    vga_write("\n\n");
    
    vga_write("Loading modules...\n");
    kinit_run_all();
    
    vga_write("\nInitializing shell... ");
    shell_init();
    commands_init();
    fs_commands_init();
    disk_commands_init();
    vga_write("OK\n\n");
    
    shell_register_command("install", install_main, "install system to disk");
    
    vga_write("UTMS LiveCD is ready!\n");
    vga_write("Type 'help' for commands\n");
    vga_write("Type 'install' to install to disk\n\n");
    
    shell_run();
    
    while(1) { __asm__ volatile ("hlt"); }
}
