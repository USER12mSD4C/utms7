#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"
#include "kinit.h"
#include "../include/shell_api.h"

void kernel_main(void *mb_info) {
    (void)mb_info;
    
    __asm__ volatile ("cli");
    
    vga_init();
    vga_clear();
    vga_write("UTMS v0.2\n");
    
    // Только самое необходимое для работы kinit
    memory_init(0x100000, 32 * 1024 * 1024);
    paging_init();
    
    // Kinit всё делает сам
    kinit_run_all();
    
    __asm__ volatile ("sti");
    
    // Shell
    shell_init();
    vga_write("\nType 'help' for commands\n");
    shell_run();
    
    while(1) { __asm__ volatile ("hlt"); }
}
