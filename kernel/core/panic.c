#include "panic.h"
#include "../drivers/gpu/drm.h"
#include "../../include/io.h"
#include "../../include/string.h"
#include "../../include/font.h"
#include "sched.h"

#define PANIC_ART_WIDTH 68
#define PANIC_INFO_WIDTH 46
#define PANIC_TOTAL_WIDTH (PANIC_ART_WIDTH + 1 + PANIC_INFO_WIDTH)

#define PANIC_BG     0x0000AAu
#define PANIC_ART_FG 0x0055FFFFu
#define PANIC_TXT_FG 0x00FFFFFFu

static const char* panic_art[] = {
    "++++++++.         +++++++++++++++-...............................",
    "+         ++++++++      +++++++++++-.............................",
    " +++++++++++++++++++++++.-  +++++++++--..........................",
    "++++.                   ++++. .++++++++ .........................",
    " -++++++++++++-++++ -+++++++   +++. ++++++ ......................",
    "++++++.     +++++-       ++++++++ ++ .+++++......................",
    "+ ++   +  -+++++- ++--.      -++++. +- +++++-....................",
    "+  +++++.--++++  ++   +++       -+++  +  ++++ ...................",
    "+++++++++.++++++++++++.+++         ++. ++ ++++...................",
    "+++++++.+ --++++.++++-  +++          ++  + +++...................",
    "++++++   .--+++-.+++ .++++++          ++  + +++ .................",
    "++++ .   -. --.++-  ++.+++++++         ++  + +++.................",
    "++++ -  -     .-  + - ++++++++ +        ++ + +++.................",
    "++++++-. .  +.-++ . .+-++++--- .+        ++  +++.................",
    "++++++++++  + +++++-+++++++.++ .++        + ++++ ................",
    " +++++++++++++++++..  --       ++-+    ++++ +++- ................",
    "  +++++++++++++++++    ++++++++++++   ++ .++   ..................",
    "  ++++++++-+++++++++++++++++ -- +++  +++++-- ....................",
    "   +++++++++. +++++++++.-++++++++-++++-  + ++....................",
    "   .+++-+-++++++++++++- ++-+++ ..++++++ ++++ ....................",
    ". .     +.      ++++++ .       -.- ++++-+..+-....................",
    ".-.++   .+- + ++++++++++  ++++++++++++-.+. - ....................",
    "-.-.         ++++++++++++++++++++++++++  +++ ....................",
    ".. +-     +-.-    ..+++++++++++++++++ ++++++ -...................",
    "+.+  --  +   -   . ++ -  +++++++++++++       -...................",
    " -----. - . - ++++++-+++++++++++++++++  .     ...................",
    "+-.--. .   ++ +++++ - ...+++++++++++++ -++    ...................",
    " ++---. - .+++++++    ++++++++++++++++ +++-   ...................",
    "++-+.--  +++++.     ++++++++++++++++++ ++++  .-..................",
    "-.+  +-+ +.     ++++++++++++++++++++  ++++++ -...................",
    "+ -++ ...-   +++++++++++++++++  -. - +++++++- ...................",
    "       -++. -+++++++++++. -    .  ++-++++++++ -..................",
    "..     -.---. +++++-.    ..-+-+ +++ ++++++++++-..................",
    "...+ -- +. + . .     .  .-. - +++++++++++++++++ ................."
};

#define PANIC_ART_LINES ((u32)(sizeof(panic_art) / sizeof(panic_art[0])))

static u32 pan_ox = 0;
static u32 pan_oy = 0;

typedef struct {
    char* buf;
    u32 cap;
    u32 len;
} pb_t;

static void pb_open(pb_t* p, char* buf, u32 cap) {
    p->buf = buf;
    p->cap = cap;
    p->len = 0;
    buf[0] = '\0';
}

static void pb_add_str(pb_t* p, const char* s) {
    while (s && *s && p->len + 1 < p->cap) p->buf[p->len++] = *s++;
    p->buf[p->len] = '\0';
}

static void pb_add_hex(pb_t* p, u64 v) {
    char t[17];
    const char* hex = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) t[(60 - i) / 4] = hex[(v >> i) & 0xF];
    t[16] = '\0';
    pb_add_str(p, t);
}

static void pb_add_num(pb_t* p, u64 v) {
    char t[24];
    int i = 0;
    if (v == 0) {
        pb_add_str(p, "0");
        return;
    }
    while (v > 0) { t[i++] = (char)('0' + (v % 10)); v /= 10; }
    char r[24];
    int j = 0;
    while (i > 0) r[j++] = t[--i];
    r[j] = '\0';
    pb_add_str(p, r);
}

static inline u32* panic_fb(void) { return (u32*)drm_dev.primary_fb.vaddr; }
static inline u32 panic_wpl(void) { return drm_dev.primary_fb.pitch / 4; }

static void panic_fill(u32 color) {
    if (!drm_dev.initialized) return;
    u32* base = panic_fb();
    u32 wpl = panic_wpl();
    u32 w = drm_dev.primary_fb.width;
    u32 h = drm_dev.primary_fb.height;
    for (u32 y = 0; y < h; y++) {
        u32* row = base + y * wpl;
        for (u32 x = 0; x < w; x++) row[x] = color;
    }
}

static void panic_glyph(u32 px, u32 py, char c, u32 fg) {
    if (!drm_dev.initialized) return;
    if (c < 32 || c > 126) c = ' ';
    u32* base = panic_fb();
    u32 wpl = panic_wpl();
    u32 w = drm_dev.primary_fb.width;
    u32 h = drm_dev.primary_fb.height;
    const u8* glyph = font8x16[(int)c];
    for (u32 dy = 0; dy < 16; dy++) {
        u32 yy = py + dy;
        if (yy >= h) break;
        u8 line = glyph[dy];
        u32* row = base + yy * wpl;
        for (u32 dx = 0; dx < 8; dx++) {
            u32 xx = px + dx;
            if (xx >= w) break;
            row[xx] = (line & (1 << (7 - dx))) ? fg : PANIC_BG;
        }
    }
}

static void panic_text_at(u32 col, u32 row, const char* s, u32 fg) {
    u32 px = pan_ox + col * 8;
    u32 py = pan_oy + row * 16;
    while (*s) {
        panic_glyph(px, py, *s, fg);
        px += 8;
        s++;
    }
}

static void panic_begin(void) {
    __asm__ volatile ("cli");
    u32 w = drm_dev.primary_fb.width;
    u32 h = drm_dev.primary_fb.height;
    u32 need_w = PANIC_TOTAL_WIDTH * 8;
    u32 need_h = PANIC_ART_LINES * 16;
    pan_ox = (w > need_w) ? (w - need_w) / 2 : 0;
    pan_oy = (h > need_h) ? (h - need_h) / 2 : 0;
    panic_fill(PANIC_BG);
}

static void panic_render(const char info[][PANIC_INFO_WIDTH + 1]) {
    for (u32 i = 0; i < PANIC_ART_LINES; i++) {
        char artbuf[PANIC_ART_WIDTH + 1];
        const char* art = panic_art[i];
        u32 len = strlen(art);
        if (len > PANIC_ART_WIDTH) len = PANIC_ART_WIDTH;
        memcpy(artbuf, art, len);
        for (u32 k = len; k < PANIC_ART_WIDTH; k++) artbuf[k] = ' ';
        artbuf[PANIC_ART_WIDTH] = '\0';
        panic_text_at(0, i, artbuf, PANIC_ART_FG);
        if (info[i][0]) panic_text_at(PANIC_ART_WIDTH + 1, i, info[i], PANIC_TXT_FG);
    }
}

static void panic_end(void) {
    drm_flush_sync();
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void panic(const char* message) {
    u64 rsp, rip, cr3;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    rip = (u64)__builtin_return_address(0);

    char info[PANIC_ART_LINES][PANIC_INFO_WIDTH + 1];
    memset(info, 0, sizeof(info));
    pb_t p;

    pb_open(&p, info[0], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "KERNEL PANIC");
    pb_open(&p, info[1], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, message);
    pb_open(&p, info[2], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "RIP=");
    pb_add_hex(&p, rip);
    pb_open(&p, info[3], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "RSP=");
    pb_add_hex(&p, rsp);
    pb_open(&p, info[4], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "CR3=");
    pb_add_hex(&p, cr3);

    panic_begin();
    panic_render(info);

    outb(0xE9, 'P'); outb(0xE9, 'A'); outb(0xE9, 'N'); outb(0xE9, 'I'); outb(0xE9, 'C'); outb(0xE9, ':');
    if (message) { while (*message) outb(0xE9, *message++); }
    outb(0xE9, '\n');

    panic_end();
}

void panic_assert(const char* file, u32 line, const char* expr) {
    char info[PANIC_ART_LINES][PANIC_INFO_WIDTH + 1];
    memset(info, 0, sizeof(info));
    pb_t p;

    pb_open(&p, info[0], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "ASSERTION FAILED");
    pb_open(&p, info[1], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "FILE=");
    pb_add_str(&p, file);
    pb_open(&p, info[2], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "LINE=");
    pb_add_num(&p, line);
    pb_open(&p, info[3], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "EXPR=");
    pb_add_str(&p, expr);

    panic_begin();
    panic_render(info);

    outb(0xE9, 'A'); outb(0xE9, 'S'); outb(0xE9, 'S'); outb(0xE9, 'E'); outb(0xE9, 'R'); outb(0xE9, 'T'); outb(0xE9, ':');
    if (file) { while (*file) outb(0xE9, *file++); }
    outb(0xE9, '\n');

    panic_end();
}

void double_fault_handler(void) {
    char info[PANIC_ART_LINES][PANIC_INFO_WIDTH + 1];
    memset(info, 0, sizeof(info));
    pb_t p;

    pb_open(&p, info[0], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "DOUBLE FAULT");
    pb_open(&p, info[1], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "not your fault, right?");

    panic_begin();
    panic_render(info);
    outb(0xE9, 'D'); outb(0xE9, 'F'); outb(0xE9, '\n');
    panic_end();
}

void triple_fault_handler(void) {
    char info[PANIC_ART_LINES][PANIC_INFO_WIDTH + 1];
    memset(info, 0, sizeof(info));
    pb_t p;

    pb_open(&p, info[0], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "TRIPLE FAULT");

    panic_begin();
    panic_render(info);
    outb(0xE9, 'T'); outb(0xE9, 'F'); outb(0xE9, '\n');
    panic_end();
}

void panic_exception(int num, u64 error_code, u64 cr2, u64 rip, u64 cs, u64 rsp) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    process_t *cur = sched_current();

    char info[PANIC_ART_LINES][PANIC_INFO_WIDTH + 1];
    memset(info, 0, sizeof(info));
    pb_t p;

    pb_open(&p, info[0], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "KERNEL EXCEPTION");
    pb_open(&p, info[1], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "VECTOR=");
    pb_add_num(&p, (u64)num);
    pb_add_str(&p, " ERR=");
    pb_add_hex(&p, error_code);
    pb_open(&p, info[2], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "CR2=");
    pb_add_hex(&p, cr2);
    pb_add_str(&p, " RIP=");
    pb_add_hex(&p, rip);
    pb_open(&p, info[3], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "CS=");
    pb_add_hex(&p, cs);
    pb_add_str(&p, " RSP=");
    pb_add_hex(&p, rsp);
    pb_open(&p, info[4], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "CR3=");
    pb_add_hex(&p, cr3);
    pb_open(&p, info[5], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "PID=");
    if (cur) pb_add_num(&p, cur->pid); else pb_add_str(&p, "null");
    pb_add_str(&p, " NAME=");
    if (cur) pb_add_str(&p, cur->name); else pb_add_str(&p, "null");
    pb_open(&p, info[6], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "KSTACK=");
    if (cur) pb_add_hex(&p, cur->kstack); else pb_add_hex(&p, 0);
    pb_add_str(&p, " KTOP=");
    if (cur) pb_add_hex(&p, cur->kstack_top); else pb_add_hex(&p, 0);
    pb_open(&p, info[7], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "USER_RIP=");
    if (cur) pb_add_hex(&p, cur->user_rip); else pb_add_hex(&p, 0);
    pb_open(&p, info[8], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "USER_RSP=");
    if (cur) pb_add_hex(&p, cur->user_rsp); else pb_add_hex(&p, 0);
    pb_open(&p, info[9], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "CR3_PROC=");
    if (cur) pb_add_hex(&p, cur->cr3); else pb_add_hex(&p, 0);
    pb_open(&p, info[10], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "STATE=");
    if (cur) pb_add_num(&p, cur->state); else pb_add_str(&p, "null");
    pb_open(&p, info[11], PANIC_INFO_WIDTH + 1);
    pb_add_str(&p, "BACKTRACE:");

    u64* frame = (u64*)rsp;
    u32 row = 12;
    for (int depth = 0; depth < 8 && frame && row < PANIC_ART_LINES; depth++) {
        u64 ret_addr = frame[1];
        if (ret_addr == 0) break;
        pb_open(&p, info[row], PANIC_INFO_WIDTH + 1);
        pb_add_str(&p, "  [");
        pb_add_num(&p, (u64)depth);
        pb_add_str(&p, "] ");
        pb_add_hex(&p, ret_addr);
        u64* next_frame = (u64*)frame[0];
        if ((u64)next_frame <= (u64)frame || (u64)next_frame > (u64)frame + 0x10000) break;
        frame = next_frame;
        row++;
    }

    panic_begin();
    panic_render(info);

    outb(0xE9, 'E'); outb(0xE9, 'X'); outb(0xE9, 'C'); outb(0xE9, ':');
    outb(0xE9, (char)('0' + (num / 10)));
    outb(0xE9, (char)('0' + (num % 10)));
    outb(0xE9, '\n');

    panic_end();
}
