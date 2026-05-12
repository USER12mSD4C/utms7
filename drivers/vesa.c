// drivers/vesa.c
#include "vesa.h"
#include "../include/string.h"
#include "../include/font.h"
#include "../kernel/memory.h"
#include "../kernel/paging.h"

// --- Параметры фреймбуфера (заполняются из Multiboot2) ---
static u32* framebuffer = NULL;
static u32 gfx_width = 0;
static u32 gfx_height = 0;
static u32 gfx_pitch = 0;  // байт на строку
static u32 gfx_bpp = 0;
static int initialized = 0;

// --- Параметры текстовой консоли ---
static u32 text_cols = 50;
static u32 text_rows = 25;
static u32 cursor_x = 0;
static u32 cursor_y = 0;
static u32 text_fg = 0xFFFFFFFF;
static u32 text_bg = 0x00000000;

// ============================================================
// ИНИЦИАЛИЗАЦИЯ
// ============================================================

int vesa_init(void) {
    if (gfx_width == 0 || gfx_height == 0 || framebuffer == NULL) {
        // Нет фреймбуфера — ничего не делаем
        return -1;
    }

    text_cols = gfx_width / 8;
    text_rows = gfx_height / 16;
    initialized = 1;

    // Очищаем экран
    for (u32 i = 0; i < gfx_width * gfx_height; i++) {
        framebuffer[i] = text_bg;
    }
    return 0;
}

// Установка параметров фреймбуфера из Multiboot2
void vesa_set_framebuffer(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp) {
    // Маппим фреймбуфер
    u64 fb_phys = addr;
    u64 fb_size = (u64)pitch * height;

    // Маппим по 2MB huge pages
    for (u64 offset = 0; offset < fb_size; offset += 0x200000) {
        paging_map(fb_phys + offset, fb_phys + offset,
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE);
    }

    framebuffer = (u32*)fb_phys;
    gfx_width = width;
    gfx_height = height;
    gfx_pitch = pitch;
    gfx_bpp = bpp;
}

// ============================================================
// ВЫВОД СИМВОЛА
// ============================================================

static void gfx_putchar(char c) {
    if (!initialized) return;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= text_rows) {
            // Скролл — сдвигаем экран на одну строку вверх
            u32 row_bytes = gfx_width * 16;  // пикселей в одной текстовой строке
            u32 total_pixels = gfx_width * (gfx_height - 16);

            for (u32 i = 0; i < total_pixels; i++) {
                framebuffer[i] = framebuffer[i + row_bytes];
            }
            // Очищаем последнюю строку
            for (u32 i = total_pixels; i < gfx_width * gfx_height; i++) {
                framebuffer[i] = text_bg;
            }
            cursor_y = text_rows - 1;
        }
        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            // Затираем символ
            for (u32 dy = 0; dy < 16; dy++) {
                for (u32 dx = 0; dx < 8; dx++) {
                    u32 px = cursor_x * 8 + dx;
                    u32 py = cursor_y * 16 + dy;
                    if (px < gfx_width && py < gfx_height) {
                        framebuffer[py * gfx_width + px] = text_bg;
                    }
                }
            }
        }
        return;
    }

    if (c < 32) return;  // непечатные символы

    // Перенос строки если не влезает
    if (cursor_x >= text_cols) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= text_rows) {
            // Скролл
            u32 row_bytes = gfx_width * 16;
            u32 total_pixels = gfx_width * (gfx_height - 16);
            for (u32 i = 0; i < total_pixels; i++) {
                framebuffer[i] = framebuffer[i + row_bytes];
            }
            for (u32 i = total_pixels; i < gfx_width * gfx_height; i++) {
                framebuffer[i] = text_bg;
            }
            cursor_y = text_rows - 1;
        }
    }

    // Рисуем символ из шрифта 8x16
    const u8* glyph = font8x16[(int)c];
    for (u32 dy = 0; dy < 16; dy++) {
        u8 line = glyph[dy];
        for (u32 dx = 0; dx < 8; dx++) {
            u32 px = cursor_x * 8 + dx;
            u32 py = cursor_y * 16 + dy;
            if (px < gfx_width && py < gfx_height) {
                if (line & (1 << (7 - dx))) {
                    framebuffer[py * gfx_width + px] = text_fg;
                } else {
                    framebuffer[py * gfx_width + px] = text_bg;
                }
            }
        }
    }

    cursor_x++;
}

// ============================================================
// ПУБЛИЧНЫЕ ФУНКЦИИ ВЫВОДА
// ============================================================

void print(const char* s) {
    if (!s) return;
    while (*s) {
        gfx_putchar(*s);
        s++;
    }
}

void println(const char* s) {
    print(s);
    gfx_putchar('\n');
}

void printnum(u64 num) {
    char buf[32];
    int i = 0;

    if (num == 0) {
        gfx_putchar('0');
        return;
    }

    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    while (i > 0) {
        gfx_putchar(buf[--i]);
    }
}

void printhex(u64 num) {
    char hex[] = "0123456789ABCDEF";
    gfx_putchar('0');
    gfx_putchar('x');
    for (int i = 60; i >= 0; i -= 4) {
        gfx_putchar(hex[(num >> i) & 0xF]);
    }
}

void print_setcolor(u8 fg, u8 bg) {
    static const u32 colors[] = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
        0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
        0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
    };
    text_fg = colors[fg & 0x0F];
    text_bg = colors[bg & 0x0F];
}

void print_clear(void) {
    if (!initialized) return;
    for (u32 i = 0; i < gfx_width * gfx_height; i++) {
        framebuffer[i] = text_bg;
    }
    cursor_x = 0;
    cursor_y = 0;
}

int print_is_graphic(void) {
    return initialized;
}

void print_setpos(u8 x, u8 y) {
    cursor_x = x;
    cursor_y = y;
}

void print_getpos(u8* x, u8* y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

// ============================================================
// ГРАФИЧЕСКИЕ ФУНКЦИИ
// ============================================================

void print_char(char c) { gfx_putchar(c); }

void vesa_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!initialized || x >= gfx_width || y >= gfx_height) return;
    framebuffer[y * gfx_width + x] = (r << 16) | (g << 8) | b;
}

void vesa_clear(u8 r, u8 g, u8 b) {
    if (!initialized) return;
    u32 color = (r << 16) | (g << 8) | b;
    for (u32 i = 0; i < gfx_width * gfx_height; i++) {
        framebuffer[i] = color;
    }
}

void vesa_draw_char(char c, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!initialized || c < 32 || c > 126) return;
    u32 color = (r << 16) | (g << 8) | b;
    const u8* glyph = font8x16[(int)c];
    for (u32 dy = 0; dy < 16; dy++) {
        u8 line = glyph[dy];
        for (u32 dx = 0; dx < 8; dx++) {
            if (line & (1 << (7 - dx))) {
                u32 px = x + dx;
                u32 py = y + dy;
                if (px < gfx_width && py < gfx_height) {
                    framebuffer[py * gfx_width + px] = color;
                }
            }
        }
    }
}

void vesa_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!initialized) return;
    while (*s) {
        vesa_draw_char(*s, x, y, r, g, b);
        x += 8;
        if (*s == '\n') {
            x = 0;  // упрощённо
            y += 16;
        }
        s++;
    }
}

u32 vesa_get_width(void) { return gfx_width; }
u32 vesa_get_height(void) { return gfx_height; }
int vesa_is_active(void) { return initialized; }

static const char __vesa_name[] __attribute__((section(".module_name"))) = "vesa";
static int (*__vesa_entry)(void) __attribute__((section(".module_entry"))) = vesa_init;
