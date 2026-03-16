#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../include/path.h"

typedef struct line {
    char *text;
    int len;
    struct line *next;
    struct line *prev;
} line_t;

typedef struct {
    line_t *first;
    line_t *last;
    line_t *cur;
    int cx;
    int cy;
    int top;
    int left;
    int count;
    int modified;
    char name[256];
    char path[256];
    int run;
    int mode;
    char buf[256];
    int pos;
    char pat[256];
    int ppos;
    int pdir;
    char *yank;
    int ylen;
} ed_t;

enum {
    NORMAL,
    INSERT,
    COMMAND,
    SEARCH
};

static ed_t e;

static line_t *line_alloc(void) {
    line_t *l = kmalloc(sizeof(line_t));
    l->text = kmalloc(1);
    l->text[0] = 0;
    l->len = 0;
    l->next = 0;
    l->prev = 0;
    return l;
}

static void line_free(line_t *l) {
    if (!l) return;
    if (l->text) kfree(l->text);
    kfree(l);
}

static void line_insert(line_t *l, int p, char c) {
    char *new = kmalloc(l->len + 2);
    int i;
    for (i = 0; i < p; i++) new[i] = l->text[i];
    new[p] = c;
    for (i = p; i < l->len; i++) new[i+1] = l->text[i];
    new[l->len+1] = 0;
    kfree(l->text);
    l->text = new;
    l->len++;
}

static void line_delete(line_t *l, int p) {
    char *new = kmalloc(l->len);
    int i;
    for (i = 0; i < p; i++) new[i] = l->text[i];
    for (i = p+1; i < l->len; i++) new[i-1] = l->text[i];
    new[l->len-1] = 0;
    kfree(l->text);
    l->text = new;
    l->len--;
}

static void line_append(line_t *l, const char *s) {
    int sl = strlen(s);
    char *new = kmalloc(l->len + sl + 1);
    int i;
    for (i = 0; i < l->len; i++) new[i] = l->text[i];
    for (i = 0; i < sl; i++) new[l->len + i] = s[i];
    new[l->len + sl] = 0;
    kfree(l->text);
    l->text = new;
    l->len += sl;
}

static void line_cut(line_t *l, int p) {
    char *new = kmalloc(p + 1);
    int i;
    for (i = 0; i < p; i++) new[i] = l->text[i];
    new[p] = 0;
    kfree(l->text);
    l->text = new;
    l->len = p;
}

static void ed_init(void) {
    e.first = e.last = e.cur = 0;
    e.cx = 0;
    e.cy = 0;
    e.top = 0;
    e.left = 0;
    e.count = 0;
    e.modified = 0;
    e.name[0] = 0;
    e.path[0] = 0;
    e.run = 1;
    e.mode = NORMAL;
    e.pos = 0;
    e.buf[0] = 0;
    e.ppos = 0;
    e.pat[0] = 0;
    e.pdir = 1;
    e.yank = 0;
    e.ylen = 0;
}

static void ed_free(void) {
    line_t *l = e.first;
    while (l) {
        line_t *n = l->next;
        line_free(l);
        l = n;
    }
}

static void ed_goto(int n) {
    if (n < 0) n = 0;
    if (n >= e.count) n = e.count - 1;
    if (n < 0) return;
    line_t *l = e.first;
    int i;
    for (i = 0; i < n && l; i++) l = l->next;
    if (!l) return;
    e.cur = l;
    e.cy = n;
    if (e.cx > e.cur->len) e.cx = e.cur->len;
    if (e.cy < e.top) e.top = e.cy;
    if (e.cy >= e.top + 23) e.top = e.cy - 22;
}

static void ed_line_open(void) {
    line_t *l = line_alloc();
    l->prev = e.cur;
    l->next = e.cur->next;
    if (e.cur->next) e.cur->next->prev = l;
    else e.last = l;
    e.cur->next = l;
    e.cur = l;
    e.cy++;
    e.cx = 0;
    e.count++;
    if (e.cy >= e.top + 23) e.top = e.cy - 22;
}

static void ed_line_kill(void) {
    if (e.count <= 1) {
        e.cur->len = 0;
        char *new = kmalloc(1);
        new[0] = 0;
        kfree(e.cur->text);
        e.cur->text = new;
        e.cx = 0;
        return;
    }
    line_t *old = e.cur;
    if (old->prev) {
        e.cur = old->prev;
        e.cy--;
    } else if (old->next) {
        e.cur = old->next;
        e.cy = 0;
    }
    if (old->prev) old->prev->next = old->next;
    else e.first = old->next;
    if (old->next) old->next->prev = old->prev;
    else e.last = old->prev;
    line_free(old);
    e.count--;
    if (e.cx > e.cur->len) e.cx = e.cur->len;
    if (e.cy < e.top) e.top = e.cy;
}

static void ed_line_join(void) {
    if (!e.cur || !e.cur->next) return;
    line_t *n = e.cur->next;
    int old = e.cur->len;
    line_append(e.cur, n->text);
    e.cur->next = n->next;
    if (n->next) n->next->prev = e.cur;
    else e.last = e.cur;
    line_free(n);
    e.count--;
    if (e.cx > old) e.cx = old;
}

static void ed_up(void) {
    if (!e.cur || !e.cur->prev) return;
    e.cur = e.cur->prev;
    e.cy--;
    if (e.cx > e.cur->len) e.cx = e.cur->len;
    if (e.cy < e.top) e.top = e.cy;
}

static void ed_down(void) {
    if (!e.cur || !e.cur->next) return;
    e.cur = e.cur->next;
    e.cy++;
    if (e.cx > e.cur->len) e.cx = e.cur->len;
    if (e.cy >= e.top + 23) e.top = e.cy - 22;
}

static void ed_left(void) {
    if (!e.cur) return;
    if (e.cx > 0) e.cx--;
    else if (e.cur->prev) {
        e.cur = e.cur->prev;
        e.cy--;
        e.cx = e.cur->len;
        if (e.cy < e.top) e.top = e.cy;
    }
}

static void ed_right(void) {
    if (!e.cur) return;
    if (e.cx < e.cur->len) e.cx++;
    else if (e.cur->next) {
        e.cur = e.cur->next;
        e.cy++;
        e.cx = 0;
        if (e.cy >= e.top + 23) e.top = e.cy - 22;
    }
}

static void ed_word(void) {
    if (!e.cur) return;
    while (e.cx < e.cur->len && e.cur->text[e.cx] == ' ') e.cx++;
    while (e.cx < e.cur->len && e.cur->text[e.cx] != ' ') e.cx++;
    if (e.cx >= e.cur->len && e.cur->next) {
        e.cur = e.cur->next;
        e.cy++;
        e.cx = 0;
        if (e.cy >= e.top + 23) e.top = e.cy - 22;
    }
}

static void ed_bword(void) {
    if (!e.cur) return;
    if (e.cx > 0) {
        e.cx--;
        while (e.cx > 0 && e.cur->text[e.cx] == ' ') e.cx--;
        while (e.cx > 0 && e.cur->text[e.cx-1] != ' ') e.cx--;
    } else if (e.cur->prev) {
        e.cur = e.cur->prev;
        e.cy--;
        e.cx = e.cur->len;
        if (e.cy < e.top) e.top = e.cy;
        while (e.cx > 0 && e.cur->text[e.cx-1] == ' ') e.cx--;
        while (e.cx > 0 && e.cur->text[e.cx-1] != ' ') e.cx--;
    }
}

static void ed_home(void) { e.cx = 0; }
static void ed_end(void) { if (e.cur) e.cx = e.cur->len; }
static void ed_pgup(void) { int i; for (i = 0; i < 20 && e.cur && e.cur->prev; i++) ed_up(); }
static void ed_pgdn(void) { int i; for (i = 0; i < 20 && e.cur && e.cur->next; i++) ed_down(); }
static void ed_top(void) { ed_goto(0); }
static void ed_bot(void) { ed_goto(e.count - 1); }

static void ed_put(char c) {
    if (!e.cur) return;
    line_insert(e.cur, e.cx, c);
    e.cx++;
    e.modified = 1;
}

static void ed_nl(void) {
    if (!e.cur) return;
    line_t *l = line_alloc();
    if (e.cx < e.cur->len) {
        line_append(l, e.cur->text + e.cx);
        line_cut(e.cur, e.cx);
    }
    l->prev = e.cur;
    l->next = e.cur->next;
    if (e.cur->next) e.cur->next->prev = l;
    else e.last = l;
    e.cur->next = l;
    e.cur = l;
    e.cy++;
    e.cx = 0;
    e.count++;
    e.modified = 1;
    if (e.cy >= e.top + 23) e.top = e.cy - 22;
}

static void ed_bs(void) {
    if (!e.cur) return;
    if (e.cx > 0) {
        line_delete(e.cur, e.cx - 1);
        e.cx--;
        e.modified = 1;
    } else if (e.cur->prev) {
        line_t *p = e.cur->prev;
        int old = p->len;
        line_append(p, e.cur->text);
        p->next = e.cur->next;
        if (e.cur->next) e.cur->next->prev = p;
        else e.last = p;
        line_free(e.cur);
        e.cur = p;
        e.cy--;
        e.cx = old;
        e.count--;
        e.modified = 1;
        if (e.top > e.cy) e.top = e.cy;
    }
}

static void ed_del(void) {
    if (!e.cur) return;
    if (e.cx < e.cur->len) {
        line_delete(e.cur, e.cx);
        e.modified = 1;
    } else if (e.cur->next) {
        line_t *n = e.cur->next;
        line_append(e.cur, n->text);
        e.cur->next = n->next;
        if (n->next) n->next->prev = e.cur;
        else e.last = e.cur;
        line_free(n);
        e.count--;
        e.modified = 1;
    }
}

static void ed_del_word(void) {
    if (!e.cur) return;
    int s = e.cx;
    int en = s;
    while (en < e.cur->len && e.cur->text[en] == ' ') en++;
    while (en < e.cur->len && e.cur->text[en] != ' ') en++;
    if (en == s) {
        while (en < e.cur->len && e.cur->text[en] == ' ') en++;
        while (en < e.cur->len && e.cur->text[en] != ' ') en++;
    }
    int i;
    for (i = en - 1; i >= s; i--) line_delete(e.cur, i);
    e.modified = 1;
}

static void ed_tab(void) { int i; for (i = 0; i < 8; i++) ed_put(' '); }

static void ed_yank(void) {
    if (!e.cur) return;
    if (e.yank) kfree(e.yank);
    e.ylen = e.cur->len;
    e.yank = kmalloc(e.ylen + 1);
    memcpy(e.yank, e.cur->text, e.ylen);
    e.yank[e.ylen] = 0;
}

static void ed_yank_line(void) { ed_yank(); }

static void ed_kill_yank(void) {
    ed_yank_line();
    ed_line_kill();
}

static void ed_put_below(void) {
    if (!e.yank || e.ylen == 0) return;
    line_t *l = line_alloc();
    line_append(l, e.yank);
    l->prev = e.cur;
    l->next = e.cur->next;
    if (e.cur->next) e.cur->next->prev = l;
    else e.last = l;
    e.cur->next = l;
    e.count++;
    e.modified = 1;
}

static void ed_put_above(void) {
    if (!e.yank || e.ylen == 0) return;
    line_t *l = line_alloc();
    line_append(l, e.yank);
    l->next = e.cur;
    l->prev = e.cur->prev;
    if (e.cur->prev) e.cur->prev->next = l;
    else e.first = l;
    e.cur->prev = l;
    e.count++;
    e.modified = 1;
}

static void ed_search(void) {
    if (e.pat[0] == 0) return;
    line_t *l = e.cur;
    int n = e.cy;
    int f = 0;
    while (!f) {
        if (e.pdir > 0) {
            if (l->next) {
                l = l->next;
                n++;
            } else break;
        } else {
            if (l->prev) {
                l = l->prev;
                n--;
            } else break;
        }
        char *p = strstr(l->text, e.pat);
        if (p) {
            e.cur = l;
            e.cy = n;
            e.cx = p - l->text;
            if (e.cy < e.top) e.top = e.cy;
            if (e.cy >= e.top + 23) e.top = e.cy - 22;
            f = 1;
        }
    }
}

static void ed_search_dir(int d) {
    e.mode = SEARCH;
    e.pdir = d;
    e.ppos = 0;
    e.pat[0] = 0;
}

static int ed_open(const char *f) {
    if (f && f[0]) {
        strcpy(e.name, f);
        build_path(f, e.path);
    } else {
        strcpy(e.name, "new");
        e.path[0] = 0;
    }
    ed_free();
    ed_init();
    if (e.path[0] == 0 || !ufs_exists(e.path) || ufs_isdir(e.path)) {
        e.first = e.last = e.cur = line_alloc();
        e.count = 1;
        return 0;
    }
    u8 *d;
    u32 sz;
    if (ufs_read(e.path, &d, &sz) != 0) {
        e.first = e.last = e.cur = line_alloc();
        e.count = 1;
        return 0;
    }
    char tmp[4096];
    int p = 0;
    line_t *last = 0;
    u32 i;
    for (i = 0; i < sz; i++) {
        if (d[i] == '\n') {
            tmp[p] = 0;
            line_t *l = line_alloc();
            line_append(l, tmp);
            if (!e.first) e.first = l;
            if (last) {
                last->next = l;
                l->prev = last;
            }
            last = l;
            e.count++;
            p = 0;
        } else if (d[i] != '\r') {
            tmp[p++] = d[i];
        }
    }
    if (p > 0 || e.count == 0) {
        tmp[p] = 0;
        line_t *l = line_alloc();
        line_append(l, tmp);
        if (!e.first) e.first = l;
        if (last) {
            last->next = l;
            l->prev = last;
        }
        last = l;
        e.count++;
    }
    kfree(d);
    e.last = last;
    e.cur = e.first;
    e.cx = 0;
    e.cy = 0;
    e.top = 0;
    e.left = 0;
    e.modified = 0;
    return 0;
}

static int ed_save(void) {
    if (e.path[0] == 0) return -1;
    int tot = 0;
    line_t *l = e.first;
    while (l) {
        tot += l->len + 1;
        l = l->next;
    }
    u8 *d = kmalloc(tot + 1);
    u8 *p = d;
    l = e.first;
    while (l) {
        if (l->len > 0) {
            memcpy(p, l->text, l->len);
            p += l->len;
        }
        *p++ = '\n';
        l = l->next;
    }
    int r = ufs_rewrite(e.path, d, tot);
    kfree(d);
    if (r == 0) e.modified = 0;
    return r;
}

static void ed_draw_status(void) {
    vga_setcolor(0x0F, 0);
    vga_setpos(0, 0);
    vga_write("UWR ");
    vga_write(e.name);
    if (e.modified) vga_write(" [+]");
    vga_setpos(50, 0);
    vga_write("Ln ");
    vga_write_num(e.cy + 1);
    vga_write("/");
    vga_write_num(e.count);
    vga_write(" Col ");
    vga_write_num(e.cx + 1);
    vga_setpos(75, 0);
    if (e.mode == NORMAL) vga_write("NORMAL");
    else if (e.mode == INSERT) vga_write("INSERT");
    else if (e.mode == COMMAND) vga_write("COMMAND");
    else if (e.mode == SEARCH) vga_write("SEARCH");
    int i;
    for (i = 0; i < 80; i++) {
        u8 x, y;
        vga_getpos(&x, &y);
        if (x >= 80) break;
        vga_putchar(' ');
    }
}

static void ed_draw_text(void) {
    line_t *l = e.first;
    int n = 0;
    int i;
    for (i = 0; i < e.top && l; i++) {
        l = l->next;
        n++;
    }
    int r;
    for (r = 0; r < 23 && l; r++) {
        vga_setpos(0, r + 1);
        vga_setcolor(0x08, 0);
        int num = n + 1;
        if (num < 10) {
            vga_putchar(' ');
            vga_putchar(' ');
            vga_putchar(' ');
            vga_write_num(num);
        } else if (num < 100) {
            vga_putchar(' ');
            vga_putchar(' ');
            vga_write_num(num);
        } else if (num < 1000) {
            vga_putchar(' ');
            vga_write_num(num);
        } else {
            vga_write_num(num);
        }
        vga_putchar(' ');
        vga_setcolor(0x07, 0);
        int s = e.left;
        int ln = l->len - s;
        if (ln > 74) ln = 74;
        for (i = 0; i < ln; i++) {
            char c = l->text[s + i];
            if (c < 32) c = '.';
            vga_putchar(c);
        }
        for (i = ln; i < 74; i++) vga_putchar(' ');
        l = l->next;
        n++;
    }
    for (; r < 23; r++) {
        vga_setpos(0, r + 1);
        for (i = 0; i < 80; i++) vga_putchar(' ');
    }
}

static void ed_draw_cursor(void) {
    int x = e.cx - e.left + 5;
    int y = e.cy - e.top + 1;
    if (y >= 1 && y <= 23 && x >= 5 && x < 80) {
        vga_setpos(x, y);
    } else {
        vga_setpos(79, 24);
    }
    vga_update_cursor();
}

static void ed_draw_cmd(void) {
    vga_setcolor(0x0F, 0);
    vga_setpos(0, 24);
    int i;
    for (i = 0; i < 80; i++) vga_putchar(' ');
    vga_setpos(0, 24);
    if (e.mode == COMMAND) {
        vga_write(":");
        vga_write(e.buf);
    } else if (e.mode == SEARCH) {
        if (e.pdir > 0) vga_write("/");
        else vga_write("?");
        vga_write(e.pat);
    }
}

static void ed_draw(void) {
    vga_clear();
    ed_draw_status();
    ed_draw_text();
    ed_draw_cursor();
    ed_draw_cmd();
}

static void ed_normal(u8 k) {
    if (k == 'i') {
        e.mode = INSERT;
    } else if (k == 'a') {
        if (e.cx < e.cur->len) e.cx++;
        e.mode = INSERT;
    } else if (k == 'I') {
        e.cx = 0;
        e.mode = INSERT;
    } else if (k == 'A') {
        e.cx = e.cur->len;
        e.mode = INSERT;
    } else if (k == 'o') {
        ed_line_open();
        e.mode = INSERT;
    } else if (k == 'O') {
        if (e.cur->prev) {
            e.cur = e.cur->prev;
            e.cy--;
        }
        ed_line_open();
        e.mode = INSERT;
    } else if (k == 'h') {
        ed_left();
    } else if (k == 'j') {
        ed_down();
    } else if (k == 'k') {
        ed_up();
    } else if (k == 'l') {
        ed_right();
    } else if (k == 'w') {
        ed_word();
    } else if (k == 'b') {
        ed_bword();
    } else if (k == '0') {
        ed_home();
    } else if (k == '$') {
        ed_end();
    } else if (k == 'G') {
        ed_bot();
    } else if (k == 'g') {
        ed_top();
    } else if (k == 'x') {
        ed_del();
    } else if (k == 'X') {
        if (e.cx > 0) {
            e.cx--;
            ed_del();
        }
    } else if (k == 'd' && (e.pos == 0 || e.buf[e.pos-1] == 'd')) {
        if (e.pos > 0 && e.buf[e.pos-1] == 'd') {
            ed_kill_yank();
            e.pos = 0;
        } else {
            e.buf[e.pos++] = 'd';
        }
    } else if (k == 'D') {
        if (e.cx < e.cur->len) {
            line_cut(e.cur, e.cx);
            e.modified = 1;
        }
    } else if (k == 'J') {
        ed_line_join();
    } else if (k == 'y' && (e.pos == 0 || e.buf[e.pos-1] == 'y')) {
        if (e.pos > 0 && e.buf[e.pos-1] == 'y') {
            ed_yank_line();
            e.pos = 0;
        } else {
            e.buf[e.pos++] = 'y';
        }
    } else if (k == 'Y') {
        ed_yank_line();
    } else if (k == 'p') {
        ed_put_below();
    } else if (k == 'P') {
        ed_put_above();
    } else if (k == '/') {
        ed_search_dir(1);
    } else if (k == '?') {
        ed_search_dir(-1);
    } else if (k == 'n') {
        ed_search();
    } else if (k == 'N') {
        e.pdir = -e.pdir;
        ed_search();
        e.pdir = -e.pdir;
    } else if (k == ':') {
        e.mode = COMMAND;
        e.pos = 0;
        e.buf[0] = 0;
    } else if (k == 0xE4) {
        ed_top();
    } else if (k == 0xE5) {
        ed_bot();
    } else if (k == 0xE6) {
        ed_pgup();
    } else if (k == 0xE7) {
        ed_pgdn();
    } else if (k == 0x1B) {
        e.pos = 0;
    }
}

static void ed_insert(u8 k) {
    if (k == 0x1B) {
        if (e.cx > 0) e.cx--;
        e.mode = NORMAL;
    } else if (k == '\b') {
        ed_bs();
    } else if (k == '\n') {
        ed_nl();
    } else if (k == '\t') {
        ed_tab();
    } else if (k >= 32 && k <= 126) {
        ed_put(k);
    }
}

static void ed_command(u8 k) {
    if (k == 0x1B) {
        e.mode = NORMAL;
        e.pos = 0;
    } else if (k == '\n') {
        e.buf[e.pos] = 0;
        if (strcmp(e.buf, "w") == 0) {
            ed_save();
        } else if (strcmp(e.buf, "q") == 0) {
            e.run = 0;
        } else if (strcmp(e.buf, "wq") == 0) {
            ed_save();
            e.run = 0;
        } else if (strcmp(e.buf, "q!") == 0) {
            e.run = 0;
        }
        e.mode = NORMAL;
        e.pos = 0;
    } else if (k == '\b') {
        if (e.pos > 0) e.pos--;
    } else if (k >= 32 && k <= 126) {
        e.buf[e.pos++] = k;
    }
}

static void ed_search_mode(u8 k) {
    if (k == 0x1B) {
        e.mode = NORMAL;
        e.ppos = 0;
    } else if (k == '\n') {
        e.pat[e.ppos] = 0;
        e.mode = NORMAL;
        ed_search();
    } else if (k == '\b') {
        if (e.ppos > 0) e.ppos--;
        e.pat[e.ppos] = 0;
    } else if (k >= 32 && k <= 126) {
        e.pat[e.ppos++] = k;
        e.pat[e.ppos] = 0;
    }
}

void uwr_main(const char *f, int v) {
    (void)v;
    ed_open(f);
    e.run = 1;
    ed_draw();
    while (e.run) {
        if (!keyboard_data_ready()) continue;
        u8 k = keyboard_getc();
        if (e.mode == NORMAL) ed_normal(k);
        else if (e.mode == INSERT) ed_insert(k);
        else if (e.mode == COMMAND) ed_command(k);
        else if (e.mode == SEARCH) ed_search_mode(k);
        ed_draw();
    }
    if (e.yank) kfree(e.yank);
    ed_free();
    vga_clear();
}
