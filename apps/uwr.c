#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../commands/fs.h"
#include "uwr.h"

#define TAB_SIZE 4
#define CHUNK_SIZE 1024

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
    int lines_count;
    int modified;
    char filename[256];
} UWR;

static UWR e;

static Line* line_new(void) {
    Line *l = kmalloc(sizeof(Line));
    if (!l) return NULL;
    
    l->alloc = CHUNK_SIZE;
    l->text = kmalloc(l->alloc);
    if (!l->text) {
        kfree(l);
        return NULL;
    }
    
    l->text[0] = 0;
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

static void line_insert_char(Line *l, int pos, char c) {
    if (!l || !l->text) return;
    
    if (l->len + 2 >= l->alloc) {
        int new_alloc = l->alloc + CHUNK_SIZE;
        char *new_text = kmalloc(new_alloc);
        if (!new_text) return;
        
        memcpy(new_text, l->text, l->len + 1);
        kfree(l->text);
        l->text = new_text;
        l->alloc = new_alloc;
    }
    
    for (int i = l->len; i >= pos; i--) {
        l->text[i+1] = l->text[i];
    }
    l->text[pos] = c;
    l->len++;
}

static void line_delete_char(Line *l, int pos) {
    if (!l || !l->text || pos >= l->len) return;
    
    for (int i = pos; i < l->len; i++) {
        l->text[i] = l->text[i+1];
    }
    l->len--;
}

static void line_append(Line *l, const char *s) {
    if (!l || !s) return;
    
    int add_len = strlen(s);
    if (l->len + add_len + 1 >= l->alloc) {
        int new_alloc = l->alloc + add_len + CHUNK_SIZE;
        char *new_text = kmalloc(new_alloc);
        if (!new_text) return;
        
        memcpy(new_text, l->text, l->len + 1);
        kfree(l->text);
        l->text = new_text;
        l->alloc = new_alloc;
    }
    
    strcpy(l->text + l->len, s);
    l->len += add_len;
}

static void e_init(void) {
    e.first = e.last = e.current = NULL;
    e.cursor_x = 0;
    e.lines_count = 0;
    e.modified = 0;
    e.filename[0] = 0;
    
    // Создаём пустую строку
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

int uwr_open(const char *filename) {
    e_free();
    e_init();
    
    char path[256];
    if (filename[0] == '/') {
        strcpy(path, filename);
    } else {
        const char *cwd = fs_get_current_dir();
        if (!cwd || strcmp(cwd, "/") == 0) {
            snprintf(path, sizeof(path), "/%s", filename);
        } else {
            snprintf(path, sizeof(path), "%s/%s", cwd, filename);
        }
    }
    
    u8 *data;
    u32 size;
    
    if (ufs_read(path, &data, &size) != 0) {
        strcpy(e.filename, filename);
        return 0;
    }
    
    e_free();
    e.first = e.last = e.current = NULL;
    e.lines_count = 0;
    
    char *p = (char*)data;
    Line *current = NULL;
    
    while (*p) {
        Line *l = line_new();
        if (!l) break;
        
        char line_buf[1024];
        int i = 0;
        while (*p && *p != '\n' && *p != '\r' && i < 1023) {
            line_buf[i++] = *p++;
        }
        line_buf[i] = 0;
        
        l->len = i;
        if (l->alloc <= i) {
            kfree(l->text);
            l->alloc = i + 1;
            l->text = kmalloc(l->alloc);
            if (!l->text) {
                kfree(l);
                break;
            }
        }
        memcpy(l->text, line_buf, i + 1);
        
        if (!e.first) {
            e.first = e.last = l;
        } else {
            e.last->next = l;
            l->prev = e.last;
            e.last = l;
        }
        e.lines_count++;
        
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    
    kfree(data);
    strcpy(e.filename, filename);
    
    if (e.lines_count == 0) {
        e_init();
    }
    
    e.current = e.first;
    e.cursor_x = 0;
    e.modified = 0;
    return 0;
}

int uwr_save(void) {
    if (e.filename[0] == 0) return -1;
    
    char path[256];
    if (e.filename[0] == '/') {
        strcpy(path, e.filename);
    } else {
        const char *cwd = fs_get_current_dir();
        if (!cwd || strcmp(cwd, "/") == 0) {
            snprintf(path, sizeof(path), "/%s", e.filename);
        } else {
            snprintf(path, sizeof(path), "%s/%s", cwd, e.filename);
        }
    }
    
    // Считаем размер
    int total_size = 0;
    Line *l = e.first;
    while (l) {
        total_size += l->len + 1; // +1 для \n
        l = l->next;
    }
    
    if (total_size <= 0) return -1;
    
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
    
    int result = ufs_write(path, data, total_size);
    kfree(data);
    
    if (result == 0) {
        e.modified = 0;
    }
    return result;
}

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
    
    // Копируем остаток строки
    if (e.cursor_x < e.current->len) {
        int rest_len = e.current->len - e.cursor_x;
        if (new_line->alloc <= rest_len) {
            kfree(new_line->text);
            new_line->alloc = rest_len + 1;
            new_line->text = kmalloc(new_line->alloc);
            if (!new_line->text) {
                kfree(new_line);
                return;
            }
        }
        memcpy(new_line->text, e.current->text + e.cursor_x, rest_len);
        new_line->text[rest_len] = 0;
        new_line->len = rest_len;
        e.current->text[e.cursor_x] = 0;
        e.current->len = e.cursor_x;
    }
    
    // Вставляем в список
    new_line->prev = e.current;
    new_line->next = e.current->next;
    
    if (e.current->next) {
        e.current->next->prev = new_line;
    } else {
        e.last = new_line;
    }
    e.current->next = new_line;
    
    e.current = new_line;
    e.cursor_x = 0;
    e.lines_count++;
    e.modified = 1;
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
        
        // Присоединяем текущую строку к предыдущей
        line_append(prev, e.current->text);
        
        // Удаляем текущую
        if (e.current->next) {
            e.current->next->prev = prev;
        } else {
            e.last = prev;
        }
        prev->next = e.current->next;
        
        line_free(e.current);
        e.current = prev;
        e.cursor_x = prev_len;
        e.lines_count--;
        e.modified = 1;
    }
}

static void e_tab(void) {
    for (int i = 0; i < TAB_SIZE; i++) {
        e_insert_char(' ');
    }
}

static void e_draw(void) {
    vga_clear();
    
    // Статус сверху
    vga_setcolor(0x0F, 0);
    vga_setpos(0, 0);
    vga_write("UWR ");
    vga_write(e.filename[0] ? e.filename : "untitled");
    if (e.modified) vga_write(" [modified]");
    
    // Очистка строки статуса
    for (int i = 0; i < 80; i++) vga_putchar(' ');
    vga_setpos(0, 0);
    
    // Находим позицию текущей строки на экране
    int current_row = -1;
    Line *l = e.first;
    int row = 0;
    
    // Основной текст
    vga_setcolor(0x07, 0);
    l = e.first;
    row = 0;
    
    while (l && row < 23) {
        vga_setpos(5, row + 1);
        
        // Номер строки
        vga_setcolor(0x08, 0);
        vga_setpos(0, row + 1);
        int n = row + 1;
        char num[5];
        num[0] = ' ';
        num[1] = ' ';
        num[2] = ' ';
        num[3] = ' ';
        num[4] = ' ';
        
        if (n >= 1000) { num[0] = '0' + (n/1000); n %= 1000; }
        if (n >= 100)  { num[1] = '0' + (n/100);  n %= 100;  }
        if (n >= 10)   { num[2] = '0' + (n/10);   n %= 10;   }
        num[3] = '0' + n;
        
        for (int j = 0; j < 5; j++) vga_putchar(num[j]);
        
        // Текст строки
        vga_setcolor(0x07, 0);
        if (l->text) {
            vga_write(l->text);
        }
        
        // Очистка остатка
        int len = l->text ? l->len : 0;
        for (int i = len + 5; i < 80; i++) {
            vga_putchar(' ');
        }
        
        // Если это текущая строка - запоминаем позицию
        if (l == e.current) {
            current_row = row + 1;
        }
        
        l = l->next;
        row++;
    }
    
    // Статус снизу
    vga_setcolor(0x0F, 0);
    vga_setpos(0, 24);
    
    vga_write("Line: ");
    int line_num = 1;
    Line *tmp = e.first;
    while (tmp && tmp != e.current) {
        line_num++;
        tmp = tmp->next;
    }
    
    if (line_num >= 1000) vga_putchar('0' + (line_num/1000));
    if (line_num >= 100) vga_putchar('0' + ((line_num/100)%10));
    if (line_num >= 10) vga_putchar('0' + ((line_num/10)%10));
    vga_putchar('0' + (line_num%10));
    
    vga_write("/");
    if (e.lines_count >= 1000) vga_putchar('0' + (e.lines_count/1000));
    if (e.lines_count >= 100) vga_putchar('0' + ((e.lines_count/100)%10));
    if (e.lines_count >= 10) vga_putchar('0' + ((e.lines_count/10)%10));
    vga_putchar('0' + (e.lines_count%10));
    
    vga_write(" Col: ");
    int col = e.cursor_x + 1;
    if (col >= 100) vga_putchar('0' + (col/100));
    if (col >= 10) vga_putchar('0' + ((col/10)%10));
    vga_putchar('0' + (col%10));
    
    // Очистка остатка
    for (int i = 20; i < 80; i++) {
        vga_putchar(' ');
    }
    
    // Курсор - ЕСЛИ строка видна на экране
    if (current_row >= 1 && current_row <= 23) {
        vga_setpos(e.cursor_x + 5, current_row);
    } else {
        vga_setpos(79, 24);
    }
    vga_update_cursor();
}

void uwr_main(const char *filename, int vesa) {
    (void)vesa;
    
    if (filename && filename[0]) {
        uwr_open(filename);
    } else {
        e_init();
        strcpy(e.filename, "untitled");
    }
    
    e_draw();
    
    while (1) {
        if (!keyboard_data_ready()) continue;
        
        u8 k = keyboard_getc();
        
        if (k == 0x13) { // Ctrl+S
            if (uwr_save() == 0) {
                vga_setcolor(0x0A, 0);
                vga_setpos(40, 24);
                vga_write("Saved");
                vga_setcolor(0x07, 0);
            }
            e_draw();
            continue;
        }
        
        if (k == 0x11) { // Ctrl+Q
            if (!e.modified) break;
            vga_setcolor(0x0C, 0);
            vga_setpos(40, 24);
            vga_write("Not saved!");
            vga_setcolor(0x07, 0);
            e_draw();
            continue;
        }
        
        // Стрелки
        if (k == 0xE0 && e.current && e.current->prev) {
            e.current = e.current->prev;
            e.cursor_x = 0;
            e_draw();
            continue;
        }
        if (k == 0xE1 && e.current && e.current->next) {
            e.current = e.current->next;
            e.cursor_x = 0;
            e_draw();
            continue;
        }
        if (k == 0xE2) {
            if (e.cursor_x > 0) {
                e.cursor_x--;
            } else if (e.current && e.current->prev) {
                e.current = e.current->prev;
                e.cursor_x = e.current->len;
            }
            e_draw();
            continue;
        }
        if (k == 0xE3) {
            if (e.current && e.cursor_x < e.current->len) {
                e.cursor_x++;
            } else if (e.current && e.current->next) {
                e.current = e.current->next;
                e.cursor_x = 0;
            }
            e_draw();
            continue;
        }
        if (k == 0xE4) { // HOME
            e.cursor_x = 0;
            e_draw();
            continue;
        }
        if (k == 0xE5) { // END
            if (e.current) {
                e.cursor_x = e.current->len;
            }
            e_draw();
            continue;
        }
        
        // Обычные клавиши
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
    
    e_free();
    vga_clear();
}
