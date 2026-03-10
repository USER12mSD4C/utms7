#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/vesa.h"
#include "../drivers/keyboard.h"
#include "../drivers/disk.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../kernel/kapi.h"
#include "uwr.h"

#define UWR_MAX_LINES 1000
#define UWR_MAX_LINE_LEN 1024
#define UWR_TAB_SIZE 4
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16

typedef struct {
    char lines[UWR_MAX_LINES][UWR_MAX_LINE_LEN];
    int line_count;
    int cursor_x;
    int cursor_y;
    int scroll_x;
    int scroll_y;
    char filename[256];
    int modified;
    int dirty;
    int vesa_mode;
    u32 screen_width;
    u32 screen_height;
    u32 cols;
    u32 rows;
} UWR;

static UWR uwr;

static void uwr_calc_dimensions(void) {
    if (uwr.vesa_mode && vesa_is_active()) {
        uwr.screen_width = vesa_get_width();
        uwr.screen_height = vesa_get_height();
        uwr.cols = uwr.screen_width / CHAR_WIDTH;
        uwr.rows = uwr.screen_height / CHAR_HEIGHT;
    } else {
        uwr.screen_width = 80 * CHAR_WIDTH;
        uwr.screen_height = 25 * CHAR_HEIGHT;
        uwr.cols = 80;
        uwr.rows = 25;
    }
}

static void uwr_insert_char(u8 key) {
    int len = strlen(uwr.lines[uwr.cursor_y]);
    if (len < UWR_MAX_LINE_LEN - 1) {
        for (int i = len; i >= uwr.cursor_x; i--) {
            uwr.lines[uwr.cursor_y][i+1] = uwr.lines[uwr.cursor_y][i];
        }
        uwr.lines[uwr.cursor_y][uwr.cursor_x] = key;
        uwr.cursor_x++;
        uwr.modified = 1;
        uwr.dirty = 1;
    }
}

static void uwr_backspace(void) {
    int len = strlen(uwr.lines[uwr.cursor_y]);
    
    if (uwr.cursor_x > 0) {
        for (int i = uwr.cursor_x - 1; i < len; i++) {
            uwr.lines[uwr.cursor_y][i] = uwr.lines[uwr.cursor_y][i+1];
        }
        uwr.cursor_x--;
        uwr.modified = 1;
        uwr.dirty = 1;
    } else if (uwr.cursor_y > 0) {
        int prev_len = strlen(uwr.lines[uwr.cursor_y - 1]);
        
        strcpy(uwr.lines[uwr.cursor_y - 1] + prev_len, uwr.lines[uwr.cursor_y]);
        
        for (int i = uwr.cursor_y; i < uwr.line_count - 1; i++) {
            strcpy(uwr.lines[i], uwr.lines[i+1]);
        }
        
        uwr.line_count--;
        uwr.cursor_y--;
        uwr.cursor_x = prev_len;
        uwr.modified = 1;
        uwr.dirty = 1;
    }
}

static void uwr_newline(void) {
    if (uwr.line_count >= UWR_MAX_LINES - 1) return;
    
    int len = strlen(uwr.lines[uwr.cursor_y]);
    
    for (int i = uwr.line_count; i > uwr.cursor_y + 1; i--) {
        strcpy(uwr.lines[i], uwr.lines[i-1]);
    }
    
    if (uwr.cursor_x < len) {
        strcpy(uwr.lines[uwr.cursor_y + 1], uwr.lines[uwr.cursor_y] + uwr.cursor_x);
        uwr.lines[uwr.cursor_y][uwr.cursor_x] = '\0';
    } else {
        uwr.lines[uwr.cursor_y + 1][0] = '\0';
    }
    
    uwr.line_count++;
    uwr.cursor_y++;
    uwr.cursor_x = 0;
    uwr.modified = 1;
    uwr.dirty = 1;
}

static void uwr_tab(void) {
    for (int i = 0; i < UWR_TAB_SIZE; i++) {
        uwr_insert_char(' ');
    }
}

static void uwr_move_up(void) {
    if (uwr.cursor_y > 0) {
        uwr.cursor_y--;
        if (uwr.cursor_x > (int)strlen(uwr.lines[uwr.cursor_y])) {
            uwr.cursor_x = strlen(uwr.lines[uwr.cursor_y]);
        }
        uwr.dirty = 1;
    }
}

static void uwr_move_down(void) {
    if (uwr.cursor_y < uwr.line_count - 1) {
        uwr.cursor_y++;
        if (uwr.cursor_x > (int)strlen(uwr.lines[uwr.cursor_y])) {
            uwr.cursor_x = strlen(uwr.lines[uwr.cursor_y]);
        }
        uwr.dirty = 1;
    }
}

static void uwr_move_left(void) {
    if (uwr.cursor_x > 0) {
        uwr.cursor_x--;
    } else if (uwr.cursor_y > 0) {
        uwr.cursor_y--;
        uwr.cursor_x = strlen(uwr.lines[uwr.cursor_y]);
    }
    uwr.dirty = 1;
}

static void uwr_move_right(void) {
    int len = strlen(uwr.lines[uwr.cursor_y]);
    if (uwr.cursor_x < len) {
        uwr.cursor_x++;
    } else if (uwr.cursor_y < uwr.line_count - 1) {
        uwr.cursor_y++;
        uwr.cursor_x = 0;
    }
    uwr.dirty = 1;
}

static void uwr_home(void) {
    uwr.cursor_x = 0;
    uwr.dirty = 1;
}

static void uwr_end(void) {
    uwr.cursor_x = strlen(uwr.lines[uwr.cursor_y]);
    uwr.dirty = 1;
}

static void uwr_page_up(void) {
    uwr.cursor_y -= uwr.rows;
    if (uwr.cursor_y < 0) uwr.cursor_y = 0;
    uwr.dirty = 1;
}

static void uwr_page_down(void) {
    uwr.cursor_y += uwr.rows;
    if (uwr.cursor_y >= uwr.line_count) uwr.cursor_y = uwr.line_count - 1;
    uwr.dirty = 1;
}

void uwr_init(int use_vesa) {
    memset(&uwr, 0, sizeof(uwr));
    uwr.line_count = 1;
    uwr.lines[0][0] = '\0';
    uwr.cursor_x = 0;
    uwr.cursor_y = 0;
    uwr.scroll_x = 0;
    uwr.scroll_y = 0;
    uwr.modified = 0;
    uwr.dirty = 1;
    uwr.filename[0] = '\0';
    uwr.vesa_mode = use_vesa && vesa_is_active();
    uwr_calc_dimensions();
}

int uwr_open(const char* filename, int use_vesa) {
    uwr_init(use_vesa);
    
    char path[256];
    if (filename[0] == '/') {
        strcpy(path, filename);
    } else {
        snprintf(path, sizeof(path), "/DOCS/%s", filename);
    }
    
    u8 *data;
    u32 size;
    
    if (ufs_read(path, &data, &size) != 0) {
        strcpy(uwr.filename, filename);
        return 0;
    }
    
    char *p = (char*)data;
    uwr.line_count = 0;
    
    while (*p && uwr.line_count < UWR_MAX_LINES) {
        int i = 0;
        while (*p && *p != '\n' && *p != '\r' && i < UWR_MAX_LINE_LEN - 1) {
            uwr.lines[uwr.line_count][i++] = *p++;
        }
        uwr.lines[uwr.line_count][i] = '\0';
        uwr.line_count++;
        
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    
    if (uwr.line_count == 0) {
        uwr.line_count = 1;
        uwr.lines[0][0] = '\0';
    }
    
    kfree(data);
    strcpy(uwr.filename, filename);
    uwr.cursor_x = 0;
    uwr.cursor_y = 0;
    uwr.scroll_x = 0;
    uwr.scroll_y = 0;
    uwr.modified = 0;
    uwr.dirty = 1;
    
    return 0;
}

int uwr_save(void) {
    if (uwr.filename[0] == '\0') return -1;
    
    char path[256];
    if (uwr.filename[0] == '/') {
        strcpy(path, uwr.filename);
    } else {
        snprintf(path, sizeof(path), "/DOCS/%s", uwr.filename);
    }
    
    int size = 0;
    for (int i = 0; i < uwr.line_count; i++) {
        size += strlen(uwr.lines[i]) + 2;
    }
    
    u8* data = kmalloc(size + 1);
    if (!data) return -1;
    
    u8* p = data;
    for (int i = 0; i < uwr.line_count; i++) {
        int len = strlen(uwr.lines[i]);
        if (len > 0) {
            memcpy(p, uwr.lines[i], len);
            p += len;
        }
        *p++ = '\r';
        *p++ = '\n';
    }
    
    int ret = ufs_write(path, data, size);
    kfree(data);
    
    if (ret == 0) {
        uwr.modified = 0;
        uwr.dirty = 1;
    }
    return ret;
}

static void uwr_draw_vesa(void) {
    vesa_clear(0, 0, 0);
    
    // Статус бар
    vesa_draw_string("UWR - ", 0, 0, 255, 255, 255);
    int x = 6 * CHAR_WIDTH;
    
    if (uwr.filename[0]) {
        vesa_draw_string(uwr.filename, x, 0, 255, 255, 255);
        x += strlen(uwr.filename) * CHAR_WIDTH;
    } else {
        vesa_draw_string("untitled", x, 0, 255, 255, 255);
        x += 8 * CHAR_WIDTH;
    }
    
    if (uwr.modified) {
        vesa_draw_string(" [modified]", x, 0, 255, 255, 0);
        x += 11 * CHAR_WIDTH;
    }
    
    vesa_draw_string("  Ctrl+S:save  Ctrl+Q:quit", x, 0, 180, 180, 180);
    
    // Линии
    for (u32 row = 0; row < uwr.rows - 2; row++) {
        int line_num = uwr.scroll_y + row;
        u32 y = (row + 1) * CHAR_HEIGHT;
        
        // Номер строки
        char num[16];
        int len = sprintf(num, "%4d ", line_num + 1);
        vesa_draw_string(num, 0, y, 100, 100, 100);
        
        if (line_num < uwr.line_count) {
            const char *line = uwr.lines[line_num];
            int line_len = strlen(line);
            int start = uwr.scroll_x;
            int display_len = line_len - start;
            if (display_len > (int)uwr.cols - 5) display_len = uwr.cols - 5;
            
            if (display_len > 0) {
                char buf[256];
                memcpy(buf, line + start, display_len);
                buf[display_len] = '\0';
                vesa_draw_string(buf, 5 * CHAR_WIDTH, y, 200, 200, 200);
            }
        }
    }
    
    // Футер
    char footer[64];
    sprintf(footer, "Line: %d/%d  Col: %d  %s", 
            uwr.cursor_y + 1, uwr.line_count, 
            uwr.cursor_x + 1, uwr.modified ? "*" : "");
    vesa_draw_string(footer, 0, (uwr.rows - 1) * CHAR_HEIGHT, 180, 180, 255);
    
    // Курсор
    int vis_y = uwr.cursor_y - uwr.scroll_y;
    int vis_x = uwr.cursor_x - uwr.scroll_x + 5;
    if (vis_y >= 0 && vis_y < (int)uwr.rows - 2 && vis_x >= 5 && vis_x < (int)uwr.cols) {
        vesa_draw_string("_", vis_x * CHAR_WIDTH, (vis_y + 1) * CHAR_HEIGHT, 255, 255, 255);
    }
}

static void uwr_draw_vga(void) {
    vga_clear();
    
    // Статус бар
    vga_setcolor(0x0F, 0x00);
    vga_setpos(0, 0);
    vga_write("UWR - ");
    if (uwr.filename[0]) {
        vga_write(uwr.filename);
    } else {
        vga_write("untitled");
    }
    if (uwr.modified) vga_write(" [modified]");
    vga_write("    Ctrl+S:save  Ctrl+Q:quit");
    
    // Линии
    vga_setcolor(0x07, 0x00);
    for (u32 row = 0; row < 23; row++) {
        int line_num = uwr.scroll_y + row;
        vga_setpos(0, row + 1);
        
        // Номер строки (ручной вывод)
        vga_setcolor(0x08, 0x00);
        int n = line_num + 1;
        if (n > 9999) n = 9999;
        
        char num[5] = {' ', ' ', ' ', ' ', ' '};
        if (n >= 1000) { num[0] = '0' + (n / 1000); n %= 1000; }
        if (n >= 100)  { num[1] = '0' + (n / 100);  n %= 100;  }
        if (n >= 10)   { num[2] = '0' + (n / 10);   n %= 10;   }
        num[3] = '0' + n;
        num[4] = ' ';
        
        for (int j = 0; j < 5; j++) vga_putchar(num[j]);
        
        vga_setcolor(0x07, 0x00);
        
        if (line_num < uwr.line_count) {
            const char *line = uwr.lines[line_num];
            int line_len = strlen(line);
            int start = uwr.scroll_x;
            int display_len = line_len - start;
            if (display_len > 75) display_len = 75;
            
            if (display_len > 0) {
                for (int j = 0; j < display_len; j++) {
                    vga_putchar(line[start + j]);
                }
            }
        }
        
        // Очистка остатка строки
        for (int j = 5 + 75; j < 80; j++) vga_putchar(' ');
    }
    
    // Футер
    vga_setcolor(0x0F, 0x00);
    vga_setpos(0, 24);
    char footer[64];
    
    // Ручной вывод чисел в футере
    vga_write("Line: ");
    int line = uwr.cursor_y + 1;
    if (line > 9999) line = 9999;
    char line_str[5];
    int li = 0;
    if (line >= 1000) line_str[li++] = '0' + (line / 1000);
    if (line >= 100) line_str[li++] = '0' + ((line / 100) % 10);
    if (line >= 10) line_str[li++] = '0' + ((line / 10) % 10);
    line_str[li++] = '0' + (line % 10);
    line_str[li] = '\0';
    vga_write(line_str);
    
    vga_write("/");
    int total = uwr.line_count;
    if (total > 9999) total = 9999;
    char total_str[5];
    li = 0;
    if (total >= 1000) total_str[li++] = '0' + (total / 1000);
    if (total >= 100) total_str[li++] = '0' + ((total / 100) % 10);
    if (total >= 10) total_str[li++] = '0' + ((total / 10) % 10);
    total_str[li++] = '0' + (total % 10);
    total_str[li] = '\0';
    vga_write(total_str);
    
    vga_write("  Col: ");
    int col = uwr.cursor_x + 1;
    if (col > 999) col = 999;
    char col_str[4];
    li = 0;
    if (col >= 100) col_str[li++] = '0' + (col / 100);
    if (col >= 10) col_str[li++] = '0' + ((col / 10) % 10);
    col_str[li++] = '0' + (col % 10);
    col_str[li] = '\0';
    vga_write(col_str);
    
    if (uwr.modified) vga_write("  *");
    
    // Очистка остатка
    int len = 5 + strlen(line_str) + 1 + strlen(total_str) + 7 + strlen(col_str) + (uwr.modified ? 3 : 0);
    for (int j = len; j < 80; j++) vga_putchar(' ');
    
    // Курсор
    int vis_y = uwr.cursor_y - uwr.scroll_y;
    int vis_x = uwr.cursor_x - uwr.scroll_x + 5;
    if (vis_y >= 0 && vis_y < 23 && vis_x >= 5 && vis_x < 80) {
        vga_setpos(vis_x, vis_y + 1);
    }
}

void uwr_draw(void) {
    if (!uwr.dirty) return;
    
    if (uwr.vesa_mode) {
        uwr_draw_vesa();
    } else {
        uwr_draw_vga();
    }
    
    uwr.dirty = 0;
}

void uwr_handle_key(u8 key) {
    if (key == 0x13) { // Ctrl+S
        if (uwr_save() == 0) {
            if (uwr.vesa_mode) {
                vesa_draw_string("Saved successfully", 0, (uwr.rows - 2) * CHAR_HEIGHT, 0, 255, 0);
            } else {
                vga_setcolor(0x0A, 0x00);
                vga_setpos(0, 23);
                vga_write("Saved successfully");
            }
        }
        uwr.dirty = 1;
        return;
    }
    
    if (key == 0x11) { // Ctrl+Q
        if (uwr.modified) {
            if (uwr.vesa_mode) {
                vesa_draw_string("File not saved! Press Ctrl+Q again to force quit", 
                                0, (uwr.rows - 2) * CHAR_HEIGHT, 255, 0, 0);
            } else {
                vga_setcolor(0x0C, 0x00);
                vga_setpos(0, 23);
                vga_write("File not saved! Press Ctrl+Q again to force quit");
            }
            uwr.modified = 0;
            return;
        }
        if (uwr.vesa_mode) vesa_clear(0, 0, 0);
        else vga_clear();
        return;
    }
    
    if (key == 0x0E) { // Ctrl+N
        if (uwr.modified) {
            if (uwr.vesa_mode) {
                vesa_draw_string("File not saved! Press Ctrl+N again to force new", 
                                0, (uwr.rows - 2) * CHAR_HEIGHT, 255, 0, 0);
            } else {
                vga_setcolor(0x0C, 0x00);
                vga_setpos(0, 23);
                vga_write("File not saved! Press Ctrl+N again to force new");
            }
            uwr.modified = 0;
            return;
        }
        uwr_init(uwr.vesa_mode);
        uwr.dirty = 1;
        return;
    }
    
    if (key >= 0xE0) {
        switch(key) {
            case 0xE0: uwr_move_up(); break;
            case 0xE1: uwr_move_down(); break;
            case 0xE2: uwr_move_left(); break;
            case 0xE3: uwr_move_right(); break;
            case 0xE4: uwr_home(); break;
            case 0xE5: uwr_end(); break;
            case 0xE6: uwr_page_up(); break;    // PgUp
            case 0xE7: uwr_page_down(); break;  // PgDown
        }
        
        // Скроллинг
        if (uwr.cursor_y < uwr.scroll_y) {
            uwr.scroll_y = uwr.cursor_y;
        } else if (uwr.cursor_y >= uwr.scroll_y + (int)uwr.rows - 2) {
            uwr.scroll_y = uwr.cursor_y - (uwr.rows - 3);
        }
        
        if (uwr.cursor_x < uwr.scroll_x) {
            uwr.scroll_x = uwr.cursor_x;
        } else if (uwr.cursor_x >= uwr.scroll_x + (int)uwr.cols - 5) {
            uwr.scroll_x = uwr.cursor_x - (uwr.cols - 6);
        }
        
        uwr_draw();
        return;
    }
    
    switch(key) {
        case '\b':
            uwr_backspace();
            break;
        case '\n':
        case '\r':
            uwr_newline();
            break;
        case '\t':
            uwr_tab();
            break;
        default:
            if (key >= 32 && key <= 126) {
                uwr_insert_char(key);
            }
            break;
    }
    
    // Скроллинг
    if (uwr.cursor_y < uwr.scroll_y) {
        uwr.scroll_y = uwr.cursor_y;
    } else if (uwr.cursor_y >= uwr.scroll_y + (int)uwr.rows - 2) {
        uwr.scroll_y = uwr.cursor_y - (uwr.rows - 3);
    }
    
    if (uwr.cursor_x < uwr.scroll_x) {
        uwr.scroll_x = uwr.cursor_x;
    } else if (uwr.cursor_x >= uwr.scroll_x + (int)uwr.cols - 5) {
        uwr.scroll_x = uwr.cursor_x - (uwr.cols - 6);
    }
    
    uwr_draw();
}

void uwr_main(const char* filename, int use_vesa) {
    if (filename && filename[0]) {
        uwr_open(filename, use_vesa);
    } else {
        uwr_init(use_vesa);
    }
    
    uwr_draw();
    
    while (1) {
        if (keyboard_data_ready()) {
            u8 key = keyboard_getc();
            uwr_handle_key(key);
            
            if (key == 0x11 && !uwr.modified) {
                break;
            }
        }
    }
}
