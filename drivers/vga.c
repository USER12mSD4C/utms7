#include "vga.h"
#include "../include/io.h"

static volatile u16* const vga = (u16*)0xB8000;
static u8 cursor_x = 0;
static u8 cursor_y = 0;
static u8 color = 0x07;

void vga_update_cursor(void) {
    u16 pos = cursor_y * 80 + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
}

int vga_init(void) {
    vga_clear();
    return 0;
}

void vga_clear(void) {
    u16 blank = (color << 8) | ' ';
    for (int i = 0; i < 80 * 25; i++) vga[i] = blank;
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_setcolor(u8 fg, u8 bg) {
    color = (bg << 4) | (fg & 0x0F);
}

void vga_setpos(u8 x, u8 y) {
    if (x < 80 && y < 25) {
        cursor_x = x;
        cursor_y = y;
        vga_update_cursor();
    }
}

static void vga_scroll(void) {
    u16 blank = (color << 8) | ' ';
    for (int i = 0; i < 24 * 80; i++) vga[i] = vga[i + 80];
    for (int i = 24 * 80; i < 25 * 80; i++) vga[i] = blank;
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= 25) {
            vga_scroll();
            cursor_y = 24;
        }
        vga_update_cursor();
        return;
    }
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga[cursor_y * 80 + cursor_x] = (color << 8) | ' ';
        }
        vga_update_cursor();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        vga_update_cursor();
        return;
    }
    if (c >= ' ') {
        vga[cursor_y * 80 + cursor_x] = (color << 8) | c;
        cursor_x++;
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= 25) {
                vga_scroll();
                cursor_y = 24;
            }
        }
        vga_update_cursor();
    }
}
void vga_write_num(u32 num) {
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

void vga_write_hex(u32 num) {
    char hex[] = "0123456789ABCDEF";
    vga_write("0x");
    vga_putchar(hex[(num >> 28) & 0xF]);
    vga_putchar(hex[(num >> 24) & 0xF]);
    vga_putchar(hex[(num >> 20) & 0xF]);
    vga_putchar(hex[(num >> 16) & 0xF]);
    vga_putchar(hex[(num >> 12) & 0xF]);
    vga_putchar(hex[(num >> 8) & 0xF]);
    vga_putchar(hex[(num >> 4) & 0xF]);
    vga_putchar(hex[num & 0xF]);
}

void vga_getpos(u8* x, u8* y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}
void vga_write(const char* s) {
    while (*s) vga_putchar(*s++);
}

static const char __vga_name[] __attribute__((section(".module_name"))) = "vga";
static int (*__vga_entry)(void) __attribute__((section(".module_entry"))) = vga_init;
