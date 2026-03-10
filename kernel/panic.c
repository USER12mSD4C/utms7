#include "panic.h"
#include "../drivers/vga.h"
#include "../include/io.h"

static void print_num(u32 num) {
    char buf[16];
    int i = 0;
    
    if (num == 0) {
        vga_putchar('0');
        return;
    }
    
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0) {
        vga_putchar(buf[--i]);
    }
}

void panic(const char* message) {
    __asm__ volatile ("cli");
    
    vga_setcolor(0x4F, 0x00);
    vga_clear();
    vga_write("\n\n KERNEL PANIC\n\n");
    vga_write(" Error: ");
    vga_write(message);
    vga_write("\n\n System halted.\n");
    
    outb(0xE9, 'P');
    outb(0xE9, 'A');
    outb(0xE9, 'N');
    outb(0xE9, 'I');
    outb(0xE9, 'C');
    
    while(1) {
        __asm__ volatile ("hlt");
    }
}

void panic_assert(const char* file, u32 line, const char* expr) {
    __asm__ volatile ("cli");
    
    vga_setcolor(0x4F, 0x00);
    vga_clear();
    vga_write("\n\n ASSERT FAILED\n\n");
    vga_write(" File: "); 
    vga_write(file); 
    vga_putchar('\n');
    vga_write(" Line: "); 
    print_num(line);
    vga_write("\n Expression: "); 
    vga_write(expr);
    vga_write("\n\n System halted.\n");
    
    while(1) {
        __asm__ volatile ("hlt");
    }
}
