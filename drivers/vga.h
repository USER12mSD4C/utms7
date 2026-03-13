#ifndef VGA_H
#define VGA_H

#include "../include/types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   0xB8000

int vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_write(const char* s);
void vga_write_num(u32 num);
void vga_write_hex(u32 num);
void vga_setcolor(u8 fg, u8 bg);
void vga_setpos(u8 x, u8 y);
void vga_update_cursor(void);
void vga_scroll_lines(i8 lines);

#endif
