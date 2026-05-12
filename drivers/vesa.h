// drivers/vesa.h
#ifndef VESA_H
#define VESA_H

#include "../include/types.h"

// Инициализация и настройка
int vesa_init(void);
void vesa_set_framebuffer(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp);

// Основные функции вывода (единые для обоих режимов)
void print(const char* s);
void println(const char* s);
void printnum(u64 num);
void printhex(u64 num);
void print_setcolor(u8 fg, u8 bg);
void print_clear(void);
void print_char(char c);
int print_is_graphic(void);
void print_setpos(u8 x, u8 y);
void print_getpos(u8* x, u8* y);

// Графические функции
void vesa_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_clear(u8 r, u8 g, u8 b);
void vesa_draw_char(char c, u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b);
u32 vesa_get_width(void);
u32 vesa_get_height(void);
int vesa_is_active(void);

#endif
