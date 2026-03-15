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
#include "../commands/disk.h"
#include "../apps/installer.h"

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS LiveCD\n");
    
    vga_write("[1/7] GDT... ");
    gdt_init();
    vga_write("OK\n");
    
    vga_write("[2/7] IDT... ");
    idt_init();
    vga_write("OK\n");
    
    vga_write("[3/7] Memory... ");
    memory_init(0x100000, 32 * 1024 * 1024);
    vga_write("OK\n");
    
    vga_write("[4/7] Paging... ");
    if (paging_init() != 0) {
        vga_write("FAILED\n");
        while(1);
    }
    vga_write("OK\n");
    
    vga_write("[5/7] Timer... ");
    timer_init();
    vga_write("OK\n");
    
    vga_write("[6/7] Scheduler... ");
    sched_init();
    vga_write("OK\n");
    
    vga_write("[7/7] Disk... ");
    disk_init();
    vga_write("OK\n");
    
    vga_write("[8/7] Keyboard... ");
    keyboard_init();
    vga_write("OK\n");
    
    __asm__ volatile ("sti");
    
    vga_write("Mounting UFS (live)... ");
    if (ufs_mount(2048, 0) == 0) {  // livecd всегда на первом диске
        vga_write("OK\n");
    } else {
        vga_write("FAILED\n");
    }
    
    kinit_run_all();
    
    shell_init();
    commands_init();
    fs_commands_init();
    disk_commands_init();
    
    shell_register_command("install", install_main, "install system to disk");
    
    vga_write("\nType 'install' after mounting target disk\n");
    shell_run();
    
    while(1) { __asm__ volatile ("hlt"); }
}
