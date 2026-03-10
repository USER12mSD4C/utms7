#include "vga.h"
#include "../include/io.h"

static volatile u16* const vga = (u16*)0xB8000;
static u8 cursor_x = 0;
static u8 cursor_y = 0;
static u8 color = 0x07;
static u8 scroll_offset = 0;

void vga_update_cursor(void) {
    u16 pos = cursor_y * VGA_WIDTH + cursor_x;
    
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void vga_init(void) {
    vga_clear();
}

void vga_clear(void) {
    u16 blank = (color << 8) | ' ';
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
    scroll_offset = 0;
    vga_update_cursor();
}

void vga_setcolor(u8 fg, u8 bg) {
    color = (bg << 4) | (fg & 0x0F);
}

void vga_setpos(u8 x, u8 y) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        cursor_x = x;
        cursor_y = y;
        vga_update_cursor();
    }
}

static void vga_scroll(void) {
    u16 blank = (color << 8) | ' ';
    
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga[i] = vga[i + VGA_WIDTH];
    }
    
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga[i] = blank;
    }
}

void vga_scroll_lines(i8 lines) {
    u16 blank = (color << 8) | ' ';
    
    if (lines > 0) {
        // Скролл вниз
        for (int i = 0; i < VGA_HEIGHT - lines; i++) {
            for (int j = 0; j < VGA_WIDTH; j++) {
                vga[i * VGA_WIDTH + j] = vga[(i + lines) * VGA_WIDTH + j];
            }
        }
        for (int i = VGA_HEIGHT - lines; i < VGA_HEIGHT; i++) {
            for (int j = 0; j < VGA_WIDTH; j++) {
                vga[i * VGA_WIDTH + j] = blank;
            }
        }
    } else if (lines < 0) {
        lines = -lines;
        for (int i = VGA_HEIGHT - 1; i >= lines; i--) {
            for (int j = 0; j < VGA_WIDTH; j++) {
                vga[i * VGA_WIDTH + j] = vga[(i - lines) * VGA_WIDTH + j];
            }
        }
        for (int i = 0; i < lines; i++) {
            for (int j = 0; j < VGA_WIDTH; j++) {
                vga[i * VGA_WIDTH + j] = blank;
            }
        }
    }
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            vga_scroll();
            cursor_y = VGA_HEIGHT - 1;
        }
        vga_update_cursor();
        return;
    }
    
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | ' ';
        }
        vga_update_cursor();
        return;
    }
    
    if (c == '\r') {
        cursor_x = 0;
        vga_update_cursor();
        return;
    }
    
    if (c == '\t') {
        vga_putchar(' ');
        return;
    }
    
    if (c >= ' ' && c <= '~') {
        vga[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | c;
        cursor_x++;
        
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= VGA_HEIGHT) {
                vga_scroll();
                cursor_y = VGA_HEIGHT - 1;
            }
        }
        vga_update_cursor();
    }
}

void vga_write(const char* s) {
    while (*s) {
        vga_putchar(*s++);
    }
}
