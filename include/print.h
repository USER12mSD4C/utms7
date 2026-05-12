// include/print.h
#ifndef PRINT_H
#define PRINT_H

#include "types.h"

int print_init(void);
void print(const char* s);
void println(const char* s);
void printnum(u64 num);
void printhex(u64 num);
void print_setcolor(u8 fg, u8 bg);  // для совместимости
void print_clear(void);
int print_is_graphic(void);         // возвращает 1 если vesa, 0 если vga

#endif
