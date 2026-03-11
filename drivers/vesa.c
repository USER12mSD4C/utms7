#include "vesa.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/font.h"
#include "../kernel/memory.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_ID      0
#define VBE_DISPI_INDEX_XRES    1
#define VBE_DISPI_INDEX_YRES    2
#define VBE_DISPI_INDEX_BPP     3
#define VBE_DISPI_INDEX_ENABLE  4
#define VBE_DISPI_INDEX_BANK    5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET 8
#define VBE_DISPI_INDEX_Y_OFFSET 9

#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40

static u32* framebuffer = (u32*)0xFD000000;
static u32 width = 0;
static u32 height = 0;
static int vesa_active = 0;

static void vesa_write_reg(u16 reg, u16 val) {
    outw(VBE_DISPI_IOPORT_INDEX, reg);
    outw(VBE_DISPI_IOPORT_DATA, val);
}

static u16 vesa_read_reg(u16 reg) {
    outw(VBE_DISPI_IOPORT_INDEX, reg);
    return inw(VBE_DISPI_IOPORT_DATA);
}

int vesa_init(void) {
    vesa_write_reg(VBE_DISPI_INDEX_ID, 0xB0C4);
    if (vesa_read_reg(VBE_DISPI_INDEX_ID) != 0xB0C4) return -1;
    
    vesa_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    
    struct { u16 w, h; } modes[] = {{1024, 768}, {800, 600}, {640, 480}};
    int mode_found = 0;
    
    for (int i = 0; i < 3; i++) {
        vesa_write_reg(VBE_DISPI_INDEX_XRES, modes[i].w);
        vesa_write_reg(VBE_DISPI_INDEX_YRES, modes[i].h);
        vesa_write_reg(VBE_DISPI_INDEX_BPP, 32);
        vesa_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
        
        if (vesa_read_reg(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_ENABLED) {
            width = modes[i].w;
            height = modes[i].h;
            mode_found = 1;
            break;
        }
    }
    
    if (!mode_found) return -1;
    
    framebuffer[0] = 0x00FF0000;
    if (framebuffer[0] != 0x00FF0000) return -1;
    
    vesa_active = 1;
    for (u32 i = 0; i < width * height; i++) framebuffer[i] = 0;
    return 0;
}

void vesa_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!vesa_active || x >= width || y >= height) return;
    framebuffer[y * width + x] = (r << 16) | (g << 8) | b;
}

void vesa_clear(u8 r, u8 g, u8 b) {
    if (!vesa_active) return;
    u32 color = (r << 16) | (g << 8) | b;
    for (u32 i = 0; i < width * height; i++) framebuffer[i] = color;
}

void vesa_draw_char(char c, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!vesa_active || c < 32 || c > 126) return;
    u32 color = (r << 16) | (g << 8) | b;
    for (int cy = 0; cy < 16; cy++) {
        u8 line = font8x16[(int)c][cy];
        for (int cx = 0; cx < 8; cx++) {
            if (line & (1 << (7 - cx))) {
                framebuffer[(y + cy) * width + (x + cx)] = color;
            }
        }
    }
}

void vesa_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!vesa_active) return;
    u32 orig_x = x;
    while (*s) {
        if (*s == '\n') {
            x = orig_x;
            y += 16;
        } else {
            vesa_draw_char(*s, x, y, r, g, b);
            x += 8;
        }
        s++;
    }
}

u32 vesa_get_width(void) { return width; }
u32 vesa_get_height(void) { return height; }
int vesa_is_active(void) { return vesa_active; }

static const char __vesa_name[] __attribute__((section(".module_name"))) = "vesa";
static int (*__vesa_entry)(void) __attribute__((section(".module_entry"))) = vesa_init;
