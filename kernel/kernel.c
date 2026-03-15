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

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS v0.2\n");
    
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
    
    vga_write("[5/8] Timer... ");
    timer_init();
    vga_write("OK\n");
    
    vga_write("[6/8] Scheduler... ");
    sched_init();
    vga_write("OK\n");
    
    vga_write("[7/8] Disk... ");
    disk_init();
    vga_write("OK\n");
    
    vga_write("[8/8] Keyboard... ");
    keyboard_init();
    vga_write("OK\n");
    
    __asm__ volatile ("sti");
    
    vga_write("Mounting UFS... ");
    if (ufs_mount(2048, 0) == 0) {
        vga_write("OK\n");
    } else {
        vga_write("FAILED (no UFS partition)\n");
    }
    
    kinit_run_all();
    
    vga_write("shell init... ");
    shell_init();
    commands_init();
    fs_commands_init();
    // disk_commands_init() убрали
    vga_write("OK\n");
    
    vga_write("welcome to UTMS!\n");
    vga_write("Type 'help' for commands\n");
    shell_run();
    
    while(1) { __asm__ volatile ("hlt"); }
}
