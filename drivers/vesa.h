#ifndef VESA_H
#define VESA_H

#include "../include/types.h"

int vesa_init(void);
void vesa_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_clear(u8 r, u8 g, u8 b);
void vesa_draw_char(char c, u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_scroll(i32 lines);
u32 vesa_get_width(void);
u32 vesa_get_height(void);
int vesa_is_active(void);
void vesa_set_cursor(u32 x, u32 y);

#endif
