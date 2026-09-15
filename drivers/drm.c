// drivers/drm.c

#include "drm.h"
#include "../include/string.h"
#include "../include/font.h"
#include "../include/io.h"
#include "../kernel/paging.h"
#include "../kernel/memory.h"

static u32 drm_scratch[1024 * 768];

static void drm_use_scratch(void) {
    drm_set_framebuffer((u64)drm_scratch, 1024, 768, 4096, 32);
}

drm_device_t drm_dev;
volatile int drm_fb_dirty = 0;

void (*drm_flush_hook)(void);
u32 drm_dirty_x0 = 0xFFFFFFFFu;
u32 drm_dirty_y0 = 0xFFFFFFFFu;
u32 drm_dirty_x1 = 0;
u32 drm_dirty_y1 = 0;

static const u32 ansi_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

#define DRM_MAX_DUMB_BUFFERS 16

typedef struct {
    u32 handle;
    u32 width;
    u32 height;
    u32 bpp;
    u32 pitch;
    u64 size;
    u64 paddr;
    void* vaddr;
    int used;
} drm_dumb_buffer_t;

static drm_dumb_buffer_t dumb_buffers[DRM_MAX_DUMB_BUFFERS];

typedef struct { u8 ch; u32 fg; u32 bg; } cons_cell_t;
#define MAX_CONS_COLS 256
#define MAX_CONS_ROWS 128
static cons_cell_t cons_screen_static[MAX_CONS_COLS * MAX_CONS_ROWS];
static cons_cell_t* cons_screen = NULL;
static u32 cons_cols = 0;
static u32 cons_rows = 0;
static u32 cons_cur_x = 0;
static u32 cons_cur_y = 0;
static int cons_cur_on = 0;

static void cons_render_cell(u32 x, u32 y, int with_cursor) {
    if (!drm_dev.initialized || !cons_screen) return;
    if (x >= cons_cols || y >= cons_rows) return;
    drm_fb_dirty = 1;
    if (x < drm_dirty_x0) drm_dirty_x0 = x;
    if (y < drm_dirty_y0) drm_dirty_y0 = y;
    if (x > drm_dirty_x1) drm_dirty_x1 = x;
    if (y > drm_dirty_y1) drm_dirty_y1 = y;
    cons_cell_t* cell = &cons_screen[y * cons_cols + x];
    u32 fg = cell->fg;
    u32 bg = cell->bg;
    if (with_cursor) { u32 t = fg; fg = bg; bg = t; }
    drm_framebuffer_t* fb = drm_dev.crtc.fb;
    u32 wpl = fb->pitch / 4;
    volatile u32* base = (volatile u32*)fb->vaddr;
    u32 px = x * 8;
    u32 py = y * 16;
    if (cell->ch < 32 || cell->ch > 126) {
        for (u32 dy = 0; dy < 16; dy++) {
            for (u32 dx = 0; dx < 8; dx++) {
                base[(py + dy) * wpl + px + dx] = bg;
            }
        }
        return;
    }
    const u8* glyph = font8x16[(int)cell->ch];
    for (u32 dy = 0; dy < 16; dy++) {
        u8 line = glyph[dy];
        for (u32 dx = 0; dx < 8; dx++) {
            base[(py + dy) * wpl + px + dx] = (line & (1 << (7 - dx))) ? fg : bg;
        }
    }
}

static void cons_render_all(void) {
    for (u32 y = 0; y < cons_rows; y++) {
        for (u32 x = 0; x < cons_cols; x++) {
            cons_render_cell(x, y, 0);
        }
    }
}

static void cons_cursor_refresh(void) {
    if (!drm_dev.initialized || !cons_screen) return;
    u32 nx = drm_dev.cursor_x;
    u32 ny = drm_dev.cursor_y;
    if (nx >= cons_cols) nx = cons_cols - 1;
    if (ny >= cons_rows) ny = cons_rows - 1;
    if (cons_cur_on && (cons_cur_x != nx || cons_cur_y != ny)) {
        cons_render_cell(cons_cur_x, cons_cur_y, 0);
    }
    cons_render_cell(nx, ny, 1);
    cons_cur_x = nx;
    cons_cur_y = ny;
    cons_cur_on = 1;
}

void drm_flush_sync(void) {
    if (drm_flush_hook) drm_flush_hook();
}

static void console_putchar(char c);
void drm_clear(u8 r, u8 g, u8 b);

void print(const char* s) {
    while (*s) console_putchar(*s++);
}

void println(const char* s) {
    print(s);
    console_putchar('\n');
}

void printnum(u64 num) {
    char buf[32];
    int i = 0;
    if (num == 0) {
        console_putchar('0');
        return;
    }
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) console_putchar(buf[--i]);
}

void printhex(u64 num) {
    static const char hex[] = "0123456789ABCDEF";
    console_putchar('0');
    console_putchar('x');
    for (int i = 60; i >= 0; i -= 4) console_putchar(hex[(num >> i) & 0xF]);
}

void print_setcolor(u8 fg, u8 bg) {
    drm_dev.text_fg = ansi_palette[fg & 0x0F];
    drm_dev.text_bg = ansi_palette[bg & 0x0F];
}

void print_clear(void) {
    if (!drm_dev.initialized) return;
    drm_clear(0, 0, 0);
    drm_dev.cursor_x = 0;
    drm_dev.cursor_y = 0;
    if (cons_screen) {
        for (u32 i = 0; i < cons_cols * cons_rows; i++) {
            cons_screen[i].ch = ' ';
            cons_screen[i].fg = ansi_palette[15];
            cons_screen[i].bg = ansi_palette[0];
        }
    }
    cons_cur_on = 0;
    cons_cursor_refresh();
    drm_flush_sync();
}

void print_char(char c) {
    console_putchar(c);
}

void drm_switch_fb(u64 paddr, void* vaddr, u32 w, u32 h, u32 pitch) {
    drm_framebuffer_t* fb = &drm_dev.primary_fb;
    fb->paddr = paddr;
    fb->vaddr = vaddr;
    fb->width = w;
    fb->height = h;
    fb->pitch = pitch;
    fb->bpp = 32;
    fb->size = (u64)pitch * h;
    fb->refcount = 1;

    drm_dev.crtc.fb = fb;
    drm_dev.crtc.enabled = 1;
    drm_dev.text_cols = w / 8;
    drm_dev.text_rows = h / 16;
    drm_dev.cursor_x = 0;
    drm_dev.cursor_y = 0;

    cons_cols = drm_dev.text_cols;
    cons_rows = drm_dev.text_rows;
    if (cons_cols > MAX_CONS_COLS) cons_cols = MAX_CONS_COLS;
    if (cons_rows > MAX_CONS_ROWS) cons_rows = MAX_CONS_ROWS;

    if (!cons_screen) cons_screen = cons_screen_static;
    for (u32 i = 0; i < cons_cols * cons_rows; i++) {
        cons_screen[i].ch = ' ';
        cons_screen[i].fg = ansi_palette[15];
        cons_screen[i].bg = ansi_palette[0];
    }
    cons_cur_on = 0;
    cons_render_all();
    cons_cursor_refresh();
    drm_flush_sync();
}

static drm_display_mode_t make_mode(u32 w, u32 h) {
    drm_display_mode_t m;
    m.hdisplay    = w;
    m.hsync_start = w + (w / 16);
    m.hsync_end   = m.hsync_start + (w / 80);
    m.htotal      = m.hsync_end + (w / 16);
    m.vdisplay    = h;
    m.vsync_start = h + (h / 16);
    m.vsync_end   = m.vsync_start + 3;
    m.vtotal      = m.vsync_end + (h / 16);
    m.clock       = (w * h * 60) / 1000;
    m.flags       = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
    return m;
}

#define DRM_MAX_USER_FBS  4
static drm_framebuffer_t user_fb_pool[DRM_MAX_USER_FBS];
static int user_fb_used[DRM_MAX_USER_FBS];

drm_framebuffer_t* drm_framebuffer_create(u64 paddr, u32 width,
                                          u32 height, u32 pitch, u32 bpp) {
    int slot = -1;
    for (int i = 0; i < DRM_MAX_USER_FBS; i++) {
        if (!user_fb_used[i]) { slot = i; break; }
    }
    if (slot < 0) return (drm_framebuffer_t*)NULL;

    drm_framebuffer_t* fb = &user_fb_pool[slot];
    fb->width   = width;
    fb->height  = height;
    fb->pitch   = pitch;
    fb->bpp     = bpp;
    fb->paddr   = paddr;
    fb->size    = (u64)pitch * height;
    fb->vaddr   = (void*)paddr;
    fb->refcount = 1;

    for (u64 off = 0; off < fb->size; off += 0x200000) {
        paging_map(paddr + off, paddr + off,
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE);
    }

    user_fb_used[slot] = 1;
    return fb;
}

void drm_framebuffer_destroy(drm_framebuffer_t* fb) {
    if (!fb) return;
    for (int i = 0; i < DRM_MAX_USER_FBS; i++) {
        if (&user_fb_pool[i] == fb) {
            user_fb_used[i] = 0;
            fb->refcount = 0;
            return;
        }
    }
}

void drm_set_framebuffer(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp) {
    drm_framebuffer_t* fb = &drm_dev.primary_fb;
    fb->paddr  = addr;
    fb->width  = width;
    fb->height = height;
    fb->pitch  = pitch;
    fb->bpp    = bpp;
    fb->size   = (u64)pitch * height;
    fb->vaddr  = (void*)addr;
    fb->refcount = 1;

    for (u64 off = 0; off < fb->size; off += 0x200000) {
        paging_map(addr + off, addr + off,
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE);
    }
}

static void drm_connector_init(drm_connector_t* c) {
    c->id = 1;
    c->connected = 1;
    c->type = DRM_CONN_TYPE_VIRTUAL;
    c->mode_count = 0;
    c->crtc = &drm_dev.crtc;

    static const struct { u32 w, h; } presets[] = {
        { 640,  480  },
        { 800,  600  },
        { 1024, 768  },
        { 1280, 720  },
        { 1280, 1024 },
        { 1920, 1080 },
    };
    for (u32 i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
        if (c->mode_count >= 8) break;
        if (presets[i].w > drm_dev.primary_fb.width)  continue;
        if (presets[i].h > drm_dev.primary_fb.height) continue;
        c->modes[c->mode_count++] = make_mode(presets[i].w, presets[i].h);
    }
    if (c->mode_count == 0) {
        c->modes[0] = make_mode(drm_dev.primary_fb.width,
                                drm_dev.primary_fb.height);
        c->mode_count = 1;
    }
}

int drm_init(void) {
    if (drm_dev.initialized) return 0;

    drm_framebuffer_t* fb = &drm_dev.primary_fb;
    if (fb->width == 0 || fb->height == 0 || fb->paddr == 0) {
        return -1;
    }

    drm_dev.crtc.id = 1;
    drm_dev.crtc.enabled = 0;
    drm_dev.crtc.fb = fb;
    drm_dev.crtc.x = 0;
    drm_dev.crtc.y = 0;

    drm_connector_init(&drm_dev.connector);

    drm_display_mode_t best = drm_dev.connector.modes[0];
    for (u32 i = 1; i < drm_dev.connector.mode_count; i++) {
        drm_display_mode_t* m = &drm_dev.connector.modes[i];
        if ((u64)m->hdisplay * m->vdisplay >
            (u64)best.hdisplay * best.vdisplay) {
            best = *m;
        }
    }
    drm_mode_set_crtc(drm_dev.crtc.id, fb, 0, 0, &best);

    drm_dev.text_cols = fb->width / 8;
    drm_dev.text_rows = fb->height / 16;
    drm_dev.cursor_x = 0;
    drm_dev.cursor_y = 0;
    drm_dev.text_fg  = ansi_palette[15];
    drm_dev.text_bg  = ansi_palette[0];

    cons_cols = drm_dev.text_cols;
    cons_rows = drm_dev.text_rows;
    if (cons_cols > MAX_CONS_COLS) cons_cols = MAX_CONS_COLS;
    if (cons_rows > MAX_CONS_ROWS) cons_rows = MAX_CONS_ROWS;
    cons_screen = cons_screen_static;
    for (u32 i = 0; i < cons_cols * cons_rows; i++) {
        cons_screen[i].ch = ' ';
        cons_screen[i].fg = ansi_palette[15];
        cons_screen[i].bg = ansi_palette[0];
    }
    cons_cur_on = 0;

    for (int i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        dumb_buffers[i].used = 0;
    }

    drm_dev.initialized = 1;
    return 0;
}

int drm_mode_set_crtc(u32 crtc_id,
                      drm_framebuffer_t* fb,
                      u32 x, u32 y,
                      const drm_display_mode_t* mode) {
    if (crtc_id != 1 || !fb || !mode) return -1;
    if (mode->hdisplay == 0 || mode->vdisplay == 0) return -1;
    if (x + mode->hdisplay > fb->width)  return -1;
    if (y + mode->vdisplay > fb->height) return -1;

    drm_dev.crtc.id      = crtc_id;
    drm_dev.crtc.fb      = fb;
    drm_dev.crtc.x       = x;
    drm_dev.crtc.y       = y;
    drm_dev.crtc.mode    = *mode;
    drm_dev.crtc.enabled = 1;
    return 0;
}

void drm_clear(u8 r, u8 g, u8 b) {
    if (!drm_dev.initialized) return;
    drm_framebuffer_t* fb = drm_dev.crtc.fb;
    u32 color = (u32)r << 16 | (u32)g << 8 | b;
    u32 words_per_line = fb->pitch / 4;
    volatile u32* base = (volatile u32*)fb->vaddr;
    for (u32 y = 0; y < fb->height; y++) {
        for (u32 x = 0; x < fb->width; x++) {
            base[y * words_per_line + x] = color;
        }
    }
    drm_dev.cursor_x = 0;
    drm_dev.cursor_y = 0;
}

u32 drm_get_width (void) { return drm_dev.primary_fb.width;  }
u64 drm_fb_phys(void) { return drm_dev.primary_fb.paddr; }
u64 drm_fb_size(void) { return drm_dev.primary_fb.size; }
u32 drm_get_height(void) { return drm_dev.primary_fb.height; }

static inline void serial_putchar(char c) {
    if (c == '\n') {
        outb(0x3F8, '\r');
    }
    outb(0x3F8, (u8)c);
}

#define CONS_NORMAL 0
#define CONS_ESC    1
#define CONS_CSI    2

static int cons_state = CONS_NORMAL;
static int cons_params[16];
static int cons_param_count = 0;
static int cons_cur_param = 0;
static int cons_has_param = 0;
static u32 cons_saved_x = 0;
static u32 cons_saved_y = 0;

static int cons_param(int idx, int defval) {
    if (idx < cons_param_count && cons_params[idx] >= 0) return cons_params[idx];
    return defval;
}

static void scroll_one(void);
static void cons_clear_cell(u32 col, u32 row);
static void cons_clear_line_range(u32 row, u32 from_col, u32 to_col);
static void cons_clear_screen(void);
static void console_emit(char c);
static void console_putchar(char c);

static void cons_csi_dispatch(char final) {
    switch (final) {
        case 'H':
        case 'f': {
            int row = cons_param(0, 1);
            int col = cons_param(1, 1);
            if (row < 1) row = 1;
            if (col < 1) col = 1;
            drm_dev.cursor_y = (u32)(row - 1);
            drm_dev.cursor_x = (u32)(col - 1);
            if (drm_dev.cursor_y >= drm_dev.text_rows) drm_dev.cursor_y = drm_dev.text_rows - 1;
            if (drm_dev.cursor_x >= drm_dev.text_cols) drm_dev.cursor_x = drm_dev.text_cols - 1;
            break;
        }
        case 'J': {
            int mode = cons_param(0, 0);
            if (mode == 2 || mode == 3) {
                cons_clear_screen();
                drm_dev.cursor_x = 0;
                drm_dev.cursor_y = 0;
            } else if (mode == 1) {
                cons_clear_line_range(drm_dev.cursor_y, 0, drm_dev.cursor_x + 1);
                for (u32 r = 0; r < drm_dev.cursor_y; r++) {
                    cons_clear_line_range(r, 0, drm_dev.text_cols);
                }
            } else {
                cons_clear_line_range(drm_dev.cursor_y, drm_dev.cursor_x, drm_dev.text_cols);
                for (u32 r = drm_dev.cursor_y + 1; r < drm_dev.text_rows; r++) {
                    cons_clear_line_range(r, 0, drm_dev.text_cols);
                }
            }
            break;
        }
        case 'K': {
            int mode = cons_param(0, 0);
            if (mode == 1) {
                cons_clear_line_range(drm_dev.cursor_y, 0, drm_dev.cursor_x + 1);
            } else if (mode == 2) {
                cons_clear_line_range(drm_dev.cursor_y, 0, drm_dev.text_cols);
            } else {
                cons_clear_line_range(drm_dev.cursor_y, drm_dev.cursor_x, drm_dev.text_cols);
            }
            break;
        }
        case 'A': {
            int n = cons_param(0, 1);
            if ((int)drm_dev.cursor_y >= n) drm_dev.cursor_y -= (u32)n;
            else drm_dev.cursor_y = 0;
            break;
        }
        case 'B': {
            int n = cons_param(0, 1);
            drm_dev.cursor_y += (u32)n;
            if (drm_dev.cursor_y >= drm_dev.text_rows) drm_dev.cursor_y = drm_dev.text_rows - 1;
            break;
        }
        case 'C': {
            int n = cons_param(0, 1);
            drm_dev.cursor_x += (u32)n;
            if (drm_dev.cursor_x >= drm_dev.text_cols) drm_dev.cursor_x = drm_dev.text_cols - 1;
            break;
        }
        case 'D': {
            int n = cons_param(0, 1);
            if ((int)drm_dev.cursor_x >= n) drm_dev.cursor_x -= (u32)n;
            else drm_dev.cursor_x = 0;
            break;
        }
        case 's':
            cons_saved_x = drm_dev.cursor_x;
            cons_saved_y = drm_dev.cursor_y;
            break;
        case 'u':
            drm_dev.cursor_x = cons_saved_x;
            drm_dev.cursor_y = cons_saved_y;
            break;
        case 'm': {
            for (int i = 0; i < cons_param_count; i++) {
                int v = cons_params[i];
                if (v < 0) v = 0;
                if (v == 0) {
                    drm_dev.text_fg = ansi_palette[15];
                    drm_dev.text_bg = ansi_palette[0];
                } else if (v == 7) {
                    u32 t = drm_dev.text_fg;
                    drm_dev.text_fg = drm_dev.text_bg;
                    drm_dev.text_bg = t;
                } else if (v >= 30 && v <= 37) {
                    drm_dev.text_fg = ansi_palette[v - 30];
                } else if (v >= 40 && v <= 47) {
                    drm_dev.text_bg = ansi_palette[v - 40];
                } else if (v >= 90 && v <= 97) {
                    drm_dev.text_fg = ansi_palette[v - 90 + 8];
                } else if (v >= 100 && v <= 107) {
                    drm_dev.text_bg = ansi_palette[v - 100 + 8];
                }
            }
            break;
        }
        default:
            break;
    }
}

static void mode_to_uapi(const drm_display_mode_t* in,
                         struct drm_mode_modeinfo* out) {
    out->clock        = in->clock;
    out->hdisplay     = (u16)in->hdisplay;
    out->hsync_start  = (u16)in->hsync_start;
    out->hsync_end    = (u16)in->hsync_end;
    out->htotal       = (u16)in->htotal;
    out->hskew        = 0;
    out->vdisplay     = (u16)in->vdisplay;
    out->vsync_start  = (u16)in->vsync_start;
    out->vsync_end    = (u16)in->vsync_end;
    out->vtotal       = (u16)in->vtotal;
    out->vscan        = 0;
    out->vrefresh     = 60;
    out->flags        = in->flags;
    out->type         = DRM_MODE_TYPE_BUILTIN | DRM_MODE_TYPE_PREFERRED;
    for (int i = 0; i < 32; i++) out->name[i] = 0;
    const char label[] = "UTMS7";
    for (int i = 0; i < 32 && label[i]; i++) out->name[i] = label[i];
}

static int drm_uapi_ready(void) {
    return drm_dev.initialized;
}

static int drm_uapi_version(struct drm_version* v) {
    if (!v) return -22;
    const char name[]  = "utms7-drm";
    const char date[]  = "20260616";
    const char desc[]  = "UTMS7 virtual KMS";
    if (v->name_len  > 0) {
        u32 n = v->name_len;
        for (u32 i = 0; i < n; i++) v->name[i] = name[i];
        v->name[n - 1] = '\0';
    }
    if (v->date_len  > 0) {
        u32 n = v->date_len;
        for (u32 i = 0; i < n; i++) v->date[i] = date[i];
        v->date[n - 1] = '\0';
    }
    if (v->desc_len  > 0) {
        u32 n = v->desc_len;
        for (u32 i = 0; i < n; i++) v->desc[i] = desc[i];
        v->desc[n - 1] = '\0';
    }
    v->version_major     = 1;
    v->version_minor     = 0;
    v->version_patchlevel = 0;
    return 0;
}

static int drm_uapi_get_cap(struct drm_get_cap* c) {
    if (!c) return -22;
    switch (c->capability) {
        case DRM_CAP_TIMESTAMP_MONOTONIC:
        case DRM_CAP_PRIME:
        case DRM_CAP_DUMB_PREFER_SHADOW:
        case DRM_CAP_DUMB_BUFFER:
            c->value = 1;
            break;
        case DRM_CAP_CURSOR_WIDTH:
        case DRM_CAP_CURSOR_HEIGHT:
            c->value = 64;
            break;
        default:
            return -22;
    }
    return 0;
}

static int drm_uapi_get_resources(struct drm_mode_card_res* r) {
    if (!r) return -22;
    r->count_crtcs      = 1;
    r->count_connectors = 1;
    r->count_encoders   = 1;
    r->count_fbs        = 1;
    r->min_width        = 0;
    r->max_width        = drm_dev.primary_fb.width;
    r->min_height       = 0;
    r->max_height       = drm_dev.primary_fb.height;
    if (r->crtc_id_ptr) {
        ((u32*)r->crtc_id_ptr)[0] = drm_dev.crtc.id;
    }
    if (r->connector_id_ptr) {
        ((u32*)r->connector_id_ptr)[0] = drm_dev.connector.id;
    }
    if (r->encoder_id_ptr) {
        ((u32*)r->encoder_id_ptr)[0] = 1;
    }
    if (r->fb_id_ptr) {
        ((u32*)r->fb_id_ptr)[0] = 1;
    }
    return 0;
}

static int drm_uapi_get_connector(struct drm_mode_get_connector* c) {
    if (!c) return -22;
    c->connector_type     = DRM_MODE_CONNECTOR_VIRTUAL;
    c->connector_type_id  = 0;
    c->connection         = drm_dev.connector.connected
                            ? DRM_MODE_CONNECTED
                            : DRM_MODE_DISCONNECTED;
    c->mm_width           = 0;
    c->mm_height          = 0;
    c->subpixel           = 0;
    c->count_modes        = drm_dev.connector.mode_count;
    c->count_props        = 0;
    c->count_encoders     = 1;
    c->encoder_id         = 1;
    if (c->modes_ptr && c->count_modes > 0) {
        u32 n = c->count_modes;
        if (n > drm_dev.connector.mode_count) n = drm_dev.connector.mode_count;
        for (u32 i = 0; i < n; i++) {
            mode_to_uapi(&drm_dev.connector.modes[i],
                         ((struct drm_mode_modeinfo*)c->modes_ptr) + i);
        }
        c->count_modes = n;
    }
    if (c->encoders_ptr) {
        ((u32*)c->encoders_ptr)[0] = 1;
    }
    return 0;
}

static int drm_uapi_get_encoder(struct drm_mode_get_encoder* e) {
    if (!e) return -22;
    if (e->encoder_id != 1) return -2;
    e->encoder_type = 1;
    e->crtc_id = 1;
    e->possible_crtcs = 1;
    e->possible_clones = 0;
    return 0;
}

static void scroll_one(void) {
    if (!cons_screen) return;
    for (u32 y = 0; y + 1 < cons_rows; y++) {
        for (u32 x = 0; x < cons_cols; x++) {
            cons_screen[y * cons_cols + x] = cons_screen[(y + 1) * cons_cols + x];
        }
    }
    for (u32 x = 0; x < cons_cols; x++) {
        cons_cell_t* cell = &cons_screen[(cons_rows - 1) * cons_cols + x];
        cell->ch = ' ';
        cell->fg = ansi_palette[15];
        cell->bg = ansi_palette[0];
    }
    cons_render_all();
    drm_dirty_x0 = 0;
    drm_dirty_y0 = 0;
    drm_dirty_x1 = cons_cols - 1;
    drm_dirty_y1 = cons_rows - 1;
}

static void cons_clear_cell(u32 col, u32 row) {
    if (!cons_screen || col >= cons_cols || row >= cons_rows) return;
    cons_cell_t* cell = &cons_screen[row * cons_cols + col];
    cell->ch = ' ';
    cell->fg = drm_dev.text_fg;
    cell->bg = drm_dev.text_bg;
    cons_render_cell(col, row, 0);
}

static void cons_clear_line_range(u32 row, u32 from_col, u32 to_col) {
    for (u32 c = from_col; c < to_col && c < cons_cols; c++) {
        cons_clear_cell(c, row);
    }
}

static void cons_clear_screen(void) {
    for (u32 r = 0; r < cons_rows; r++) {
        cons_clear_line_range(r, 0, cons_cols);
    }
}

static void console_emit(char c) {
    serial_putchar(c);
    if (!drm_dev.initialized) return;

    if (c == '\n') {
        drm_dev.cursor_x = 0;
        drm_dev.cursor_y++;
        if (drm_dev.cursor_y >= drm_dev.text_rows) {
            scroll_one();
            drm_dev.cursor_y = drm_dev.text_rows - 1;
        }
        return;
    }
    if (c == '\r') { drm_dev.cursor_x = 0; return; }

    if (c == '\b') {
        if (drm_dev.cursor_x > 0) drm_dev.cursor_x--;
        return;
    }

    if ((u8)c < 32) return;

    if (drm_dev.cursor_x >= drm_dev.text_cols) {
        drm_dev.cursor_x = 0;
        drm_dev.cursor_y++;
        if (drm_dev.cursor_y >= drm_dev.text_rows) {
            scroll_one();
            drm_dev.cursor_y = drm_dev.text_rows - 1;
        }
    }

    if (cons_screen && drm_dev.cursor_y < cons_rows) {
        cons_cell_t* cell = &cons_screen[drm_dev.cursor_y * cons_cols + drm_dev.cursor_x];
        cell->ch = (u8)c;
        cell->fg = drm_dev.text_fg;
        cell->bg = drm_dev.text_bg;
        cons_render_cell(drm_dev.cursor_x, drm_dev.cursor_y, 0);
    }
    drm_dev.cursor_x++;
}

static void console_putchar(char c) {
    if (cons_state == CONS_ESC) {
        if (c == '[') {
            cons_state = CONS_CSI;
            cons_param_count = 0;
            cons_cur_param = 0;
            cons_has_param = 0;
        } else {
            cons_state = CONS_NORMAL;
        }
        return;
    }

    if (cons_state == CONS_CSI) {
        if (c >= '0' && c <= '9') {
            cons_cur_param = cons_cur_param * 10 + (c - '0');
            cons_has_param = 1;
        } else if (c == ';') {
            if (cons_param_count < 16) {
                cons_params[cons_param_count++] = cons_has_param ? cons_cur_param : -1;
            }
            cons_cur_param = 0;
            cons_has_param = 0;
        } else if (c >= 0x40 && c <= 0x7E) {
            if (cons_param_count < 16) {
                cons_params[cons_param_count++] = cons_has_param ? cons_cur_param : -1;
            }
            cons_csi_dispatch(c);
            cons_state = CONS_NORMAL;
            cons_cursor_refresh();
        }
        return;
    }

    if (c == 0x1B) {
        cons_state = CONS_ESC;
        return;
    }

    console_emit(c);
    cons_cursor_refresh();
}

static int drm_uapi_get_crtc(struct drm_mode_crtc* c) {
    if (!c) return -22;
    c->crtc_id           = drm_dev.crtc.id;
    c->fb_id             = drm_dev.crtc.enabled ? 1 : 0;
    c->x                 = drm_dev.crtc.x;
    c->y                 = drm_dev.crtc.y;
    c->gamma_size        = 0;
    c->count_connectors  = 1;
    c->mode_valid        = drm_dev.crtc.enabled ? 1 : 0;
    if (drm_dev.crtc.enabled) {
        mode_to_uapi(&drm_dev.crtc.mode, &c->mode);
    }
    return 0;
}

static int drm_uapi_set_crtc(struct drm_mode_crtc* c) {
    if (!c) return -22;
    if (c->crtc_id != 1) return -2;

    drm_framebuffer_t* fb = NULL;
    if (c->fb_id == 1) {
        fb = &drm_dev.primary_fb;
    } else {
        int slot = c->fb_id - 2;
        if (slot >= 0 && slot < DRM_MAX_USER_FBS && user_fb_used[slot]) {
            fb = &user_fb_pool[slot];
        }
    }
    if (!fb) return -2;

    drm_display_mode_t mode;
    mode.hdisplay = c->mode.hdisplay;
    mode.vdisplay = c->mode.vdisplay;
    mode.clock = c->mode.clock;
    mode.flags = c->mode.flags;
    mode.hsync_start = c->mode.hsync_start;
    mode.hsync_end = c->mode.hsync_end;
    mode.htotal = c->mode.htotal;
    mode.vsync_start = c->mode.vsync_start;
    mode.vsync_end = c->mode.vsync_end;
    mode.vtotal = c->mode.vtotal;

    int ret = drm_mode_set_crtc(c->crtc_id, fb, c->x, c->y, &mode);
    if (ret != 0) return -22;

    return 0;
}

static int drm_uapi_get_fb(struct drm_mode_fb_cmd* f) {
    if (!f) return -22;
    if (f->fb_id == 0 || f->fb_id == 1) {
        f->fb_id  = 1;
        f->width  = drm_dev.primary_fb.width;
        f->height = drm_dev.primary_fb.height;
        f->pitch  = drm_dev.primary_fb.pitch;
        f->bpp    = drm_dev.primary_fb.bpp;
        f->depth  = 24;
        f->handle = 1;
        return 0;
    }

    int slot = f->fb_id - 2;
    if (slot >= 0 && slot < DRM_MAX_USER_FBS && user_fb_used[slot]) {
        drm_framebuffer_t* fb = &user_fb_pool[slot];
        f->width  = fb->width;
        f->height = fb->height;
        f->pitch  = fb->pitch;
        f->bpp    = fb->bpp;
        f->depth  = 24;
        f->handle = slot + 2;
        return 0;
    }
    return -2;
}

static int drm_uapi_add_fb(struct drm_mode_fb_cmd* f) {
    if (!f) return -22;
    if (f->width != 0 && f->height != 0) return -38;
    return drm_uapi_get_fb(f);
}

static int drm_uapi_add_fb2(struct drm_mode_fb_cmd2* f) {
    if (!f) return -22;

    u32 gem_handle = f->handles[0];
    drm_dumb_buffer_t* buf = NULL;
    for (int i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        if (dumb_buffers[i].used && dumb_buffers[i].handle == gem_handle) {
            buf = &dumb_buffers[i];
            break;
        }
    }
    if (!buf) return -2;

    drm_framebuffer_t* fb = drm_framebuffer_create(buf->paddr, f->width, f->height, f->pitches[0], buf->bpp);
    if (!fb) return -12;

    int slot = -1;
    for (int i = 0; i < DRM_MAX_USER_FBS; i++) {
        if (&user_fb_pool[i] == fb) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -12;

    f->fb_id = slot + 2;
    return 0;
}

static int drm_uapi_rm_fb(unsigned int* fb_id) {
    if (!fb_id) return -22;
    unsigned int id = *fb_id;
    if (id == 1) return -1;

    int slot = id - 2;
    if (slot >= 0 && slot < DRM_MAX_USER_FBS) {
        if (user_fb_used[slot]) {
            drm_framebuffer_destroy(&user_fb_pool[slot]);
            return 0;
        }
    }
    return -2;
}

static int drm_uapi_get_planes(struct drm_mode_get_plane_res* p) {
    if (!p) return -22;
    p->count_planes = 0;
    return 0;
}

static u32 next_gem_handle = 2;

static int drm_uapi_gem_create(struct drm_gem_create* g) {
    if (!g) return -22;
    g->handle = next_gem_handle++;
    g->pad    = 0;
    return 0;
}

static int drm_uapi_gem_close(struct drm_gem_close* g) {
    if (!g) return -22;
    return 0;
}

static int drm_uapi_gem_mmap(struct drm_gem_mmap* m) {
    if (!m) return -22;
    m->offset = (u64)m->handle * 0x1000000ULL;
    return 0;
}

static int drm_uapi_create_dumb(struct drm_mode_create_dumb* c) {
    if (!c) return -22;

    int slot = -1;
    for (int i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        if (!dumb_buffers[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -12;

    u32 bpp = c->bpp;
    if (bpp == 0) bpp = 32;

    u32 pitch = (c->width * (bpp / 8) + 3) & ~3;
    u64 size = (u64)pitch * c->height;
    size = (size + 4095) & ~4095;

    void* vaddr = kmalloc(size);
    if (!vaddr) return -12;

    memset(vaddr, 0, size);

    drm_dumb_buffer_t* buf = &dumb_buffers[slot];
    buf->handle = slot + 1;
    buf->width = c->width;
    buf->height = c->height;
    buf->bpp = bpp;
    buf->pitch = pitch;
    buf->size = size;
    buf->paddr = (u64)vaddr;
    buf->vaddr = vaddr;
    buf->used = 1;

    c->handle = buf->handle;
    c->pitch = buf->pitch;
    c->size = buf->size;

    return 0;
}

static int drm_uapi_map_dumb(struct drm_mode_map_dumb* m) {
    if (!m) return -22;

    drm_dumb_buffer_t* buf = NULL;
    for (int i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        if (dumb_buffers[i].used && dumb_buffers[i].handle == m->handle) {
            buf = &dumb_buffers[i];
            break;
        }
    }
    if (!buf) return -2;

    m->offset = (u64)buf->handle * 0x1000000ULL;
    return 0;
}

static int drm_uapi_destroy_dumb(struct drm_mode_destroy_dumb* d) {
    if (!d) return -22;

    for (int i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        if (dumb_buffers[i].used && dumb_buffers[i].handle == d->handle) {
            kfree(dumb_buffers[i].vaddr);
            dumb_buffers[i].used = 0;
            return 0;
        }
    }
    return -2;
}

int drm_ioctl(unsigned int cmd, unsigned long arg) {
    if (!drm_uapi_ready()) return -19;

    void* a = (void*)arg;
    switch (cmd) {
        case DRM_IOCTL_VERSION:               return drm_uapi_version((struct drm_version*)a);
        case DRM_IOCTL_GET_CAP:               return drm_uapi_get_cap((struct drm_get_cap*)a);
        case DRM_IOCTL_MODE_GETRESOURCES:     return drm_uapi_get_resources((struct drm_mode_card_res*)a);
        case DRM_IOCTL_MODE_GETCRTC:          return drm_uapi_get_crtc((struct drm_mode_crtc*)a);
        case DRM_IOCTL_MODE_SETCRTC:          return drm_uapi_set_crtc((struct drm_mode_crtc*)a);
        case DRM_IOCTL_MODE_GETENCODER:       return drm_uapi_get_encoder((struct drm_mode_get_encoder*)a);
        case DRM_IOCTL_MODE_GETCONNECTOR:     return drm_uapi_get_connector((struct drm_mode_get_connector*)a);
        case DRM_IOCTL_MODE_GETFB:            return drm_uapi_get_fb((struct drm_mode_fb_cmd*)a);
        case DRM_IOCTL_MODE_ADDFB:            return drm_uapi_add_fb((struct drm_mode_fb_cmd*)a);
        case DRM_IOCTL_MODE_ADDFB2:           return drm_uapi_add_fb2((struct drm_mode_fb_cmd2*)a);
        case DRM_IOCTL_MODE_RMFB:             return drm_uapi_rm_fb((unsigned int*)a);
        case DRM_IOCTL_MODE_GETPLANERESOURCES:return drm_uapi_get_planes((struct drm_mode_get_plane_res*)a);
        case DRM_IOCTL_GEM_CREATE:            return drm_uapi_gem_create((struct drm_gem_create*)a);
        case DRM_IOCTL_GEM_CLOSE:             return drm_uapi_gem_close((struct drm_gem_close*)a);
        case DRM_IOCTL_GEM_MMAP:              return drm_uapi_gem_mmap((struct drm_gem_mmap*)a);
        case DRM_IOCTL_MODE_CREATE_DUMB:      return drm_uapi_create_dumb((struct drm_mode_create_dumb*)a);
        case DRM_IOCTL_MODE_MAP_DUMB:         return drm_uapi_map_dumb((struct drm_mode_map_dumb*)a);
        case DRM_IOCTL_MODE_DESTROY_DUMB:     return drm_uapi_destroy_dumb((struct drm_mode_destroy_dumb*)a);
        default:                              return -25;
    }
}

u64 drm_mmap_fb(u64 offset, u64 size) {
    (void)size;
    if (offset == 0) {
        return drm_dev.primary_fb.paddr;
    }

    u32 handle = (u32)(offset / 0x1000000ULL);
    if (handle > 0 && handle <= DRM_MAX_DUMB_BUFFERS) {
        int idx = handle - 1;
        if (dumb_buffers[idx].used) {
            return dumb_buffers[idx].paddr;
        }
    }
    return 0;
}

void drm_parse_multiboot(u64 mb_info) {
    outb(0x3F8, 'D');
    outb(0x3F8, 'R');
    outb(0x3F8, 'M');
    outb(0x3F8, '\n');

    if (!mb_info) {
        drm_use_scratch();
        return;
    }

    u8* ptr = (u8*)(mb_info + 8);
    while (1) {
        u32 type = *(u32*)ptr;
        u32 size = *(u32*)(ptr + 4);
        if (type == 0 || size == 0) break;
        if (type == 8) {
            drm_use_scratch();
            return;
        }
        ptr += (size + 7) & ~7;
    }

    drm_use_scratch();
}

static const char __drm_name[] __attribute__((section(".module_name"))) = "drm";
static int (*__drm_entry)(void) __attribute__((section(".module_entry"))) = drm_init;
