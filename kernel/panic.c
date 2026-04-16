#include "panic.h"
#include "../drivers/vga.h"
#include "../include/io.h"
#include "../include/string.h"

static void print_hex(u64 num) {
    char hex[] = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        vga_putchar(hex[(num >> i) & 0xF]);
    }
}

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
    while (i > 0) vga_putchar(buf[--i]);
}

void panic(const char* message) {
    __asm__ volatile ("cli");

    vga_setcolor(0x4F, 0);
    vga_clear();
    vga_write("d-tF&P handler: ");
    vga_write(message);
    vga_write("\n");

    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    __asm__ volatile(
        "mov %%rax, %0\n"
        "mov %%rbx, %1\n"
        "mov %%rcx, %2\n"
        "mov %%rdx, %3\n"
        "mov %%rsi, %4\n"
        "mov %%rdi, %5\n"
        "mov %%rbp, %6\n"
        "mov %%rsp, %7\n"
        : "=m"(rax), "=m"(rbx), "=m"(rcx), "=m"(rdx),
          "=m"(rsi), "=m"(rdi), "=m"(rbp), "=m"(rsp)
    );

    vga_write("RAX="); print_hex(rax); vga_write(" RBX="); print_hex(rbx); vga_write("\n");
    vga_write("RCX="); print_hex(rcx); vga_write(" RDX="); print_hex(rdx); vga_write("\n");
    vga_write("RSI="); print_hex(rsi); vga_write(" RDI="); print_hex(rdi); vga_write("\n");
    vga_write("RBP="); print_hex(rbp); vga_write(" RSP="); print_hex(rsp); vga_write("\n");
    vga_write("ye - Mari Kart\n");

    outb(0xE9, 'P');
    while (*message) outb(0xE9, *message++);

    while(1) { __asm__ volatile ("hlt"); }
}

void panic_assert(const char* file, u32 line, const char* expr) {
    __asm__ volatile ("cli");
    vga_setcolor(0x4F, 0);
    vga_clear();
    vga_write("ASSERT: ");
    vga_write(file);
    vga_write(":");
    print_num(line);
    vga_write(" ");
    vga_write(expr);
    vga_write("\n");
    vga_write("ye - Mari Kart\n");
    while(1) { __asm__ volatile ("hlt"); }
}

// Для двойной ошибки
void double_fault_handler(void) {
    panic("DOUBLE FAULT");
}
