#include "panic.h"
#include "../drivers/vga.h"
#include "../include/io.h"
#include "../include/string.h"
#include "sched.h"

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
    // Сначала запрещаем прерывания
    __asm__ volatile ("cli");

    // Переключаемся на безопасный стек ядра только если планировщик работает
    extern process_t *current;
    if (current && current->kstack_top) {
        __asm__ volatile ("mov %0, %%rsp" : : "r"(current->kstack_top + 8192 - 4096));
    }

    // Устанавливаем цвета для panic
    vga_setcolor(0x4F, 0);
    vga_clear();
    vga_write("KERNEL PANIC: ");
    vga_write(message);
    vga_write("\n\n");

    // Сохраняем регистры
    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15;
    u64 cr0, cr2, cr3, cr4, rip;

    __asm__ volatile("mov %%rax, %0" : "=r"(rax));
    __asm__ volatile("mov %%rbx, %0" : "=r"(rbx));
    __asm__ volatile("mov %%rcx, %0" : "=r"(rcx));
    __asm__ volatile("mov %%rdx, %0" : "=r"(rdx));
    __asm__ volatile("mov %%rsi, %0" : "=r"(rsi));
    __asm__ volatile("mov %%rdi, %0" : "=r"(rdi));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%r8, %0"  : "=r"(r8));
    __asm__ volatile("mov %%r9, %0"  : "=r"(r9));
    __asm__ volatile("mov %%r10, %0" : "=r"(r10));
    __asm__ volatile("mov %%r11, %0" : "=r"(r11));
    __asm__ volatile("mov %%r12, %0" : "=r"(r12));
    __asm__ volatile("mov %%r13, %0" : "=r"(r13));
    __asm__ volatile("mov %%r14, %0" : "=r"(r14));
    __asm__ volatile("mov %%r15, %0" : "=r"(r15));

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    // RIP получаем из стека (адрес возврата)
    rip = (u64)__builtin_return_address(0);

    vga_write("Registers:\n");
    vga_write("RAX="); print_hex(rax); vga_write(" RBX="); print_hex(rbx); vga_write("\n");
    vga_write("RCX="); print_hex(rcx); vga_write(" RDX="); print_hex(rdx); vga_write("\n");
    vga_write("RSI="); print_hex(rsi); vga_write(" RDI="); print_hex(rdi); vga_write("\n");
    vga_write("RBP="); print_hex(rbp); vga_write(" RSP="); print_hex(rsp); vga_write("\n");
    vga_write("R8 ="); print_hex(r8);  vga_write(" R9 ="); print_hex(r9);  vga_write("\n");
    vga_write("R10="); print_hex(r10); vga_write(" R11="); print_hex(r11); vga_write("\n");
    vga_write("R12="); print_hex(r12); vga_write(" R13="); print_hex(r13); vga_write("\n");
    vga_write("R14="); print_hex(r14); vga_write(" R15="); print_hex(r15); vga_write("\n\n");

    vga_write("CR0="); print_hex(cr0); vga_write(" CR2="); print_hex(cr2); vga_write("\n");
    vga_write("CR3="); print_hex(cr3); vga_write(" CR4="); print_hex(cr4); vga_write("\n");
    vga_write("RIP="); print_hex(rip); vga_write("\n\n");

    vga_write("System halted.\n");

    // Отправляем в порт отладки
    outb(0xE9, 'P');
    outb(0xE9, 'A');
    outb(0xE9, 'N');
    outb(0xE9, 'I');
    outb(0xE9, 'C');
    outb(0xE9, ':');
    while (*message) outb(0xE9, *message++);
    outb(0xE9, '\n');

    while(1) {
        __asm__ volatile ("cli; hlt");
    }
}

void panic_assert(const char* file, u32 line, const char* expr) {
    __asm__ volatile ("cli");
    vga_setcolor(0x4F, 0);
    vga_clear();
    vga_write("ASSERTION FAILED\n");
    vga_write("File: ");
    vga_write(file);
    vga_write("\nLine: ");
    print_num(line);
    vga_write("\nExpr: ");
    vga_write(expr);
    vga_write("\n\nSystem halted.\n");
    while(1) {
        __asm__ volatile ("cli; hlt");
    }
}

void double_fault_handler(void) {
    // Для double fault используем отдельный стек (IST в IDT)
    __asm__ volatile ("cli");
    vga_setcolor(0x4F, 0);
    vga_clear();
    vga_write("DOUBLE FAULT\n");
    vga_write("System halted.\n");
    while(1) {
        __asm__ volatile ("cli; hlt");
    }
}
