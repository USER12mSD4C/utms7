#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"
#include "kinit.h"
#include "../include/shell_api.h"

// Явные прототипы
int gdt_init(void);
int idt_init(void);
int timer_init(void);

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS v0.2 - Auto Kinit\n");
    
    // 1. GDT
    vga_write("[1/6] GDT... ");
    gdt_init();
    vga_write("OK\n");
    
    // 2. IDT
    vga_write("[2/6] IDT... ");
    idt_init();
    vga_write("OK\n");
    
    // 3. Память
    vga_write("[3/6] Memory... ");
    memory_init(0x100000, 32 * 1024 * 1024);
    vga_write("OK\n");
    
    // 4. Paging
    vga_write("[4/6] Paging... ");
    if (paging_init() != 0) {
        vga_write("FAILED\n");
        while(1);
    }
    vga_write("OK\n");
    
    // 5. Таймер
    vga_write("[5/6] Timer... ");
    timer_init();
    vga_write("OK\n");
    
    // Включаем прерывания
    __asm__ volatile ("sti");
    
    // 6. Kinit
    vga_write("[6/6] Kinit...\n");
    kinit_run_all();
    
    // Shell
    shell_init();
    vga_write("\nType 'help' for commands\n");
    shell_run();
    
    while(1) { __asm__ volatile ("hlt"); }
}
