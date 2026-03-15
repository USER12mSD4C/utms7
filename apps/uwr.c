#include "../include/string.h"
#include "../include/path.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../commands/fs.h"
#include "uwr.h"

#define TAB_SIZE 4
#define CHUNK_SIZE 1024
#define MAX_LINES 1024
#define MAX_LINE_LEN 4096

typedef struct Line {
    char *text;
    int len;
    int alloc;
    struct Line *next;
    struct Line *prev;
} Line;

typedef struct {
    Line *first;
    Line *last;
    Line *current;
    int cursor_x;
    int cursor_y;
    int top_line;
    int lines_count;
    int modified;
    char filename[256];      // Оригинальное имя для отображения
    char fullpath[256];      // Полный путь для сохранения
    int running;
} UWR;

static UWR e;

// Прототип функции из fs/ufs.c (она уже есть в проекте)
void build_path(const char* arg, char* result);

// ========== РАБОТА СО СТРОКАМИ ==========

static Line* line_new(void) {
    Line *l = kmalloc(sizeof(Line));
    if (!l) return NULL;
    
    l->alloc = CHUNK_SIZE;
    l->text = kmalloc(l->alloc);
    if (!l->text) {
        kfree(l);
        return NULL;
    }
    
    l->text[0] = '\0';
    l->len = 0;
    l->next = NULL;
    l->prev = NULL;
    return l;
}

static void line_free(Line *l) {
    if (!l) return;
    if (l->text) kfree(l->text);
    kfree(l);
}

static void line_ensure_capacity(Line *l, int needed) {
    if (!l || !l->text) return;
    
    if (l->len + needed + 2 >= l->alloc) {
        int new_alloc = l->alloc + (needed + CHUNK_SIZE);
        char *new_text = kmalloc(new_alloc);
        if (!new_text) return;
        
        memcpy(new_text, l->text, l->len + 1);
        kfree(l->text);
        l->text = new_text;
        l->alloc = new_alloc;
    }
}

static void line_insert_char(Line *l, int pos, char c) {
    if (!l || !l->text || pos < 0 || pos > l->len) return;
    
    line_ensure_capacity(l, 1);
    
    for (int i = l->len; i >= pos; i--) {
        l->text[i+1] = l->text[i];
    }
    l->text[pos] = c;
    l->len++;
}

static void line_delete_char(Line *l, int pos) {
    if (!l || !l->text || pos < 0 || pos >= l->len) return;
    
    for (int i = pos; i < l->len; i++) {
        l->text[i] = l->text[i+1];
    }
    l->len--;
}

static void line_append(Line *l, const char *s) {
    if (!l || !s) return;
    
    int add_len = strlen(s);
    if (add_len == 0) return;
    
    line_ensure_capacity(l, add_len);
    
    memcpy(l->text + l->len, s, add_len);
    l->len += add_len;
    l->text[l->len] = '\0';
}

// ========== ИНИЦИАЛИЗАЦИЯ ==========

static void e_init(void) {
    e.first = e.last = e.current = NULL;
    e.cursor_x = 0;
    e.cursor_y = 0;
    e.top_line = 0;
    e.lines_count = 0;
    e.modified = 0;
    e.running = 1;
    
    Line *l = line_new();
    if (l) {
        e.first = e.last = e.current = l;
        e.lines_count = 1;
    }
}

static void e_free(void) {
    Line *l = e.first;
    while (l) {
        Line *next = l->next;
        line_free(l);
        l = next;
    }
    e.first = e.last = e.current = NULL;
    e.lines_count = 0;
}

// ========== НАВИГАЦИЯ ==========

static void e_move_up(void) {
    if (!e.current || !e.current->prev) return;
    
    e.current = e.current->prev;
    e.cursor_y--;
    
    if (e.cursor_x > e.current->len) {
        e.cursor_x = e.current->len;
    }
    
    if (e.cursor_y < e.top_line) {
        e.top_line = e.cursor_y;
    }
}

static void e_move_down(void) {
    if (!e.current || !e.current->next) return;
    
    e.current = e.current->next;
    e.cursor_y++;
    
    if (e.cursor_x > e.current->len) {
        e.cursor_x = e.current->len;
    }
    
    if (e.cursor_y >= e.top_line + 23) {
        e.top_line = e.cursor_y - 22;
    }
}

static void e_move_left(void) {
    if (!e.current) return;
    
    if (e.cursor_x > 0) {
        e.cursor_x--;
    } else if (e.current->prev) {
        e.current = e.current->prev;
        e.cursor_y--;
        e.cursor_x = e.current->len;
        
        if (e.cursor_y < e.top_line) {
            e.top_line = e.cursor_y;
        }
    }
}

static void e_move_right(void) {
    if (!e.current) return;
    
    if (e.cursor_x < e.current->len) {
        e.cursor_x++;
    } else if (e.current->next) {
        e.current = e.current->next;
        e.cursor_y++;
        e.cursor_x = 0;
        
        if (e.cursor_y >= e.top_line + 23) {
            e.top_line = e.cursor_y - 22;
        }
    }
}

static void e_move_home(void) {
    e.cursor_x = 0;
}

static void e_move_end(void) {
    if (e.current) {
        e.cursor_x = e.current->len;
    }
}

static void e_page_up(void) {
    for (int i = 0; i < 20; i++) {
        if (e.current && e.current->prev) {
            e_move_up();
        } else {
            break;
        }
    }
}

static void e_page_down(void) {
    for (int i = 0; i < 20; i++) {
        if (e.current && e.current->next) {
            e_move_down();
        } else {
            break;
        }
    }
}

// ========== РЕДАКТИРОВАНИЕ ==========

static void e_insert_char(char c) {
    if (!e.current) return;
    
    line_insert_char(e.current, e.cursor_x, c);
    e.cursor_x++;
    e.modified = 1;
}

static void e_newline(void) {
    if (!e.current) return;
    
    Line *new_line = line_new();
    if (!new_line) return;
    
    if (e.cursor_x < e.current->len) {
        int rest_len = e.current->len - e.cursor_x;
        line_ensure_capacity(new_line, rest_len);
        memcpy(new_line->text, e.current->text + e.cursor_x, rest_len);
        new_line->text[rest_len] = '\0';
        new_line->len = rest_len;
        e.current->text[e.cursor_x] = '\0';
        e.current->len = e.cursor_x;
    }
    
    new_line->prev = e.current;
    new_line->next = e.current->next;
    
    if (e.current->next) {
        e.current->next->prev = new_line;
    } else {
        e.last = new_line;
    }
    e.current->next = new_line;
    
    e.current = new_line;
    e.cursor_y++;
    e.cursor_x = 0;
    e.lines_count++;
    e.modified = 1;
    
    if (e.cursor_y >= e.top_line + 23) {
        e.top_line = e.cursor_y - 22;
    }
}

static void e_backspace(void) {
    if (!e.current) return;
    
    if (e.cursor_x > 0) {
        line_delete_char(e.current, e.cursor_x - 1);
        e.cursor_x--;
        e.modified = 1;
    }
    else if (e.current->prev) {
        Line *prev = e.current->prev;
        int prev_len = prev->len;
        
        line_append(prev, e.current->text);
        
        if (e.current->next) {
            e.current->next->prev = prev;
        } else {
            e.last = prev;
        }
        prev->next = e.current->next;
        
        line_free(e.current);
        e.current = prev;
        e.cursor_y--;
        e.cursor_x = prev_len;
        e.lines_count--;
        e.modified = 1;
        
        if (e.top_line > e.cursor_y) {
            e.top_line = e.cursor_y;
        }
    }
}

static void e_delete(void) {
    if (!e.current) return;
    
    if (e.cursor_x < e.current->len) {
        line_delete_char(e.current, e.cursor_x);
        e.modified = 1;
    }
    else if (e.current->next) {
        Line *next = e.current->next;
        
        line_append(e.current, next->text);
        
        e.current->next = next->next;
        if (next->next) {
            next->next->prev = e.current;
        } else {
            e.last = e.current;
        }
        
        line_free(next);
        e.lines_count--;
        e.modified = 1;
    }
}

static void e_tab(void) {
    for (int i = 0; i < TAB_SIZE; i++) {
        e_insert_char(' ');
    }
}

// ========== ФАЙЛОВЫЕ ОПЕРАЦИИ ==========

int uwr_open(const char *filename) {
    // Сохраняем имя для отображения
    if (filename && filename[0]) {
        strcpy(e.filename, filename);
        
        // Используем готовую build_path из UFS
        build_path(filename, e.fullpath);
    } else {
        strcpy(e.filename, "untitled");
        e.fullpath[0] = '\0';
    }
    
    // Очищаем старый документ
    e_free();
    
    // Создаём новый пустой документ
    e.first = e.last = e.current = NULL;
    e.cursor_x = 0;
    e.cursor_y = 0;
    e.top_line = 0;
    e.lines_count = 0;
    e.modified = 0;
    e.running = 1;
    
    Line *l = line_new();
    if (l) {
        e.first = e.last = e.current = l;
        e.lines_count = 1;
    }
    
    // Если файл не существует - выходим
    if (e.fullpath[0] == '\0' || !ufs_exists(e.fullpath) || ufs_isdir(e.fullpath)) {
        return 0;
    }
    
    // Читаем файл
    u8 *data;
    u32 size;
    if (ufs_read(e.fullpath, &data, &size) != 0) {
        return 0;
    }
    
    // Очищаем пустой документ
    e_free();
    e.first = e.last = e.current = NULL;
    e.lines_count = 0;
    
    // Разбираем строки
    char *p = (char*)data;
    while (*p) {
        Line *l = line_new();
        if (!l) break;
        
        char line_buf[MAX_LINE_LEN];
        int i = 0;
        while (*p && *p != '\n' && i < MAX_LINE_LEN - 1) {
            line_buf[i++] = *p++;
        }
        line_buf[i] = '\0';
        if (*p == '\n') p++;
        
        line_ensure_capacity(l, i);
        memcpy(l->text, line_buf, i);
        l->text[i] = '\0';
        l->len = i;
        
        if (!e.first) {
            e.first = e.last = l;
        } else {
            e.last->next = l;
            l->prev = e.last;
            e.last = l;
        }
        e.lines_count++;
    }
    
    kfree(data);
    
    // Если файл был пуст
    if (e.lines_count == 0) {
        Line *l = line_new();
        if (l) {
            e.first = e.last = e.current = l;
            e.lines_count = 1;
        }
    }
    
    e.current = e.first;
    e.cursor_x = 0;
    e.cursor_y = 0;
    e.top_line = 0;
    e.modified = 0;
    
    return 0;
}

int uwr_save(void) {
    if (e.fullpath[0] == '\0') {
        return -1;
    }
    
    int total_size = 0;
    Line *l = e.first;
    while (l) {
        total_size += l->len + 1;
        l = l->next;
    }
    
    if (total_size <= 0) {
        int result = ufs_write(e.fullpath, NULL, 0);
        if (result == 0) e.modified = 0;
        return result;
    }
    
    u8 *data = kmalloc(total_size + 1);
    if (!data) return -1;
    
    u8 *p = data;
    l = e.first;
    while (l) {
        if (l->len > 0) {
            memcpy(p, l->text, l->len);
            p += l->len;
        }
        *p++ = '\n';
        l = l->next;
    }
    
    int result = ufs_rewrite(e.fullpath, data, total_size);
    kfree(data);
    
    if (result == 0) e.modified = 0;
    
    return result;
}

// ========== ОТОБРАЖЕНИЕ ==========

static void e_draw_status(void) {
    vga_setcolor(0x0F, 0);
    vga_setpos(0, 0);
    
    vga_write("UWR ");
    if (e.filename[0]) {
        vga_write(e.filename);
    } else {
        vga_write("untitled");
    }
    
    if (e.modified) {
        vga_write(" [modified]");
    }
    
    vga_setpos(50, 0);
    vga_write("Line ");
    vga_write_num(e.cursor_y + 1);
    vga_write("/");
    vga_write_num(e.lines_count);
    vga_write(" Col ");
    vga_write_num(e.cursor_x + 1);
    
    for (int i = 70; i < 80; i++) {
        vga_putchar(' ');
    }
    
    // Отладка путей
    vga_setcolor(0x08, 0);
    vga_setpos(0, 24);
    vga_write("file='");
    vga_write(e.filename);
    vga_write("' path='");
    vga_write(e.fullpath);
    vga_write("'        ");
    vga_setcolor(0x07, 0);
}

static void e_draw_text(void) {
    if (!e.current) return;
    
    Line *l = e.first;
    int line_num = 0;
    
    for (int i = 0; i < e.top_line && l; i++) {
        l = l->next;
        line_num++;
    }
    
    for (int row = 0; row < 23 && l; row++) {
        vga_setpos(0, row + 1);
        
        vga_setcolor(0x08, 0);
        int n = line_num + 1;
        
        if (n < 10) {
            vga_putchar(' ');
            vga_putchar(' ');
            vga_putchar(' ');
            vga_write_num(n);
        } else if (n < 100) {
            vga_putchar(' ');
            vga_putchar(' ');
            vga_write_num(n);
        } else if (n < 1000) {
            vga_putchar(' ');
            vga_write_num(n);
        } else {
            vga_write_num(n);
        }
        vga_putchar(' ');
        
        vga_setcolor(0x07, 0);
        if (l->text) {
            vga_write(l->text);
        }
        
        int text_len = l->text ? l->len : 0;
        for (int i = text_len + 5; i < 80; i++) {
            vga_putchar(' ');
        }
        
        l = l->next;
        line_num++;
    }
    
    for (int row = line_num - e.top_line; row < 23; row++) {
        vga_setpos(0, row + 1);
        for (int i = 0; i < 80; i++) {
            vga_putchar(' ');
        }
    }
}

static void e_draw_cursor(void) {
    if (!e.current) return;
    
    int screen_y = e.cursor_y - e.top_line + 1;
    if (screen_y >= 1 && screen_y <= 23) {
        vga_setpos(e.cursor_x + 5, screen_y);
        vga_update_cursor();
    }
}

static void e_draw(void) {
    vga_clear();
    e_draw_status();
    e_draw_text();
    e_draw_cursor();
}

static void e_show_message(const char *msg, int color) {
    vga_setcolor(color, 0);
    vga_setpos(40, 24);
    
    for (int i = 0; i < 40; i++) {
        vga_putchar(' ');
    }
    
    vga_setpos(40, 24);
    vga_write(msg);
    vga_setcolor(0x07, 0);
}

// ========== ОСНОВНОЙ ЦИКЛ ==========

void uwr_main(const char *filename, int vesa) {
    (void)vesa;
    
    uwr_open(filename);
    e_draw();
    
    while (e.running) {
        if (!keyboard_data_ready()) continue;
        
        u8 k = keyboard_getc();
        int mods = keyboard_get_modifiers();
        
        // Ctrl+S (19) - сохранить
        if (k == 19) {
            if (e.fullpath[0] == '\0') {
                e_show_message("No filename!", 0x0C);
            } else {
                e_show_message("Saving...", 0x0F);
                e_draw();
                
                for (int i = 0; i < 500000; i++) asm volatile ("nop");
                
                if (uwr_save() == 0) {
                    e_show_message("Saved", 0x0A);
                } else {
                    e_show_message("Save failed!", 0x0C);
                }
            }
            
            for (int i = 0; i < 2000000; i++) asm volatile ("nop");
            e_draw();
            continue;
        }
        
        // Ctrl+Q (17) - выход
        if (k == 17) {
            if (e.modified) {
                e_show_message("Not saved! (S)ave (Q)uit", 0x0C);
                
                while (1) {
                    if (!keyboard_data_ready()) continue;
                    u8 c = keyboard_getc();
                    
                    if (c == 's' || c == 'S') {
                        if (uwr_save() == 0) {
                            e.running = 0;
                            break;
                        }
                    } else if (c == 'q' || c == 'Q') {
                        e.running = 0;
                        break;
                    }
                }
            } else {
                e.running = 0;
            }
            if (e.running) e_draw();
            continue;
        }
        
        // Специальные клавиши
        if (k >= 0xE0 && k <= 0xE9) {
            switch (k) {
                case 0xE0: e_page_up(); break;
                case 0xE1: e_page_down(); break;
                case 0xE2: e_move_left(); break;
                case 0xE3: e_move_right(); break;
                case 0xE4: e_move_home(); break;
                case 0xE5: e_move_end(); break;
                case 0xE6: e_page_up(); break;
                case 0xE7: e_page_down(); break;
                case 0xE8: break;
                case 0xE9: e_delete(); break;
            }
            e_draw();
            continue;
        }
        
        // Обычные клавиши
        if (!(mods & KEY_MOD_CTRL)) {
            switch (k) {
                case '\b': e_backspace(); e_draw(); break;
                case '\n': e_newline(); e_draw(); break;
                case '\t': e_tab(); e_draw(); break;
                default:
                    if (k >= 32 && k <= 126) {
                        e_insert_char(k);
                        e_draw();
                    }
                    break;
            }
        }
    }
    
    e_free();
    vga_clear();
}
