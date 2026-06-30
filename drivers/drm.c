// drivers/drm.c

#include "drm.h"
#include "../include/string.h"
#include "../include/font.h"
#include "../kernel/paging.h"

// =====================================================================
// Module singleton
// =====================================================================

drm_device_t drm_dev;

static const u32 ansi_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

// =====================================================================
// Mode helpers
// =====================================================================

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
    // 60 Hz with a 10% estimate of blanking overhead
    m.clock       = (w * h * 60) / 1000;
    m.flags       = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
    return m;
}

// =====================================================================
// Framebuffer object lifetime
// =====================================================================

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
    fb->vaddr   = (void*)paddr;     // identity-mapped region
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

// =====================================================================
// Boot-time setup
// =====================================================================

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

    // Identity-map the framebuffer into kernel virtual memory using
    // 2 MiB huge pages. This is the fix for the old vesa.c bug where
    // it stored the physical address directly in a pointer and tried
    // to dereference it from long-mode C, which faults because
    // physical addresses are not directly accessible.
    for (u64 off = 0; off < fb->size; off += 0x200000) {
        paging_map(addr + off, addr + off,
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE);
    }
}

// =====================================================================
// Initialization
// =====================================================================

static void drm_connector_init(drm_connector_t* c) {
    c->id = 1;
    c->connected = 1;
    c->type = DRM_CONN_TYPE_VIRTUAL;
    c->mode_count = 0;
    c->crtc = &drm_dev.crtc;

    // Enumerate a few standard modes that fit within the primary FB.
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
        // No preset fits; just advertise the actual FB size.
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

    // CRTC
    drm_dev.crtc.id = 1;
    drm_dev.crtc.enabled = 0;
    drm_dev.crtc.fb = fb;
    drm_dev.crtc.x = 0;
    drm_dev.crtc.y = 0;

    // Connector
    drm_connector_init(&drm_dev.connector);

    // Pick the largest advertised mode by default.
    drm_display_mode_t best = drm_dev.connector.modes[0];
    for (u32 i = 1; i < drm_dev.connector.mode_count; i++) {
        drm_display_mode_t* m = &drm_dev.connector.modes[i];
        if ((u64)m->hdisplay * m->vdisplay >
            (u64)best.hdisplay * best.vdisplay) {
            best = *m;
        }
    }
    drm_mode_set_crtc(drm_dev.crtc.id, fb, 0, 0, &best);

    // Text console
    drm_dev.text_cols = fb->width / 8;
    drm_dev.text_rows = fb->height / 16;
    drm_dev.cursor_x = 0;
    drm_dev.cursor_y = 0;
    drm_dev.text_fg  = ansi_palette[15];
    drm_dev.text_bg  = ansi_palette[0];

    drm_dev.initialized = 1;
    return 0;
}

int drm_is_active(void) { return drm_dev.initialized; }

// =====================================================================
// Modeset
// =====================================================================

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

// =====================================================================
// Drawing primitives
// =====================================================================

// Bounds-checked raw write of a 32-bit pixel. We only support 32 bpp
// in the boot path. Other bpp values are rejected.
static inline void fb_write32(drm_framebuffer_t* fb, u32 x, u32 y, u32 v) {
    if (x >= fb->width || y >= fb->height) return;
    volatile u32* base = (volatile u32*)fb->vaddr;
    base[y * (fb->pitch / 4) + x] = v;
}

void drm_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!drm_dev.initialized) return;
    if (drm_dev.crtc.fb->bpp != 32) return;
    fb_write32(drm_dev.crtc.fb, x, y, (u32)r << 16 | (u32)g << 8 | b);
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

void drm_draw_char(char c, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!drm_dev.initialized) return;
    if ((u8)c > 126) return;
    drm_framebuffer_t* fb = drm_dev.crtc.fb;
    u32 color = (u32)r << 16 | (u32)g << 8 | b;
    const u8* glyph = font8x16[(int)c];
    for (u32 dy = 0; dy < 16; dy++) {
        u8 line = glyph[dy];
        for (u32 dx = 0; dx < 8; dx++) {
            if (line & (1 << (7 - dx))) {
                fb_write32(fb, x + dx, y + dy, color);
            }
        }
    }
}

void drm_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!drm_dev.initialized) return;
    while (*s) {
        drm_draw_char(*s, x, y, r, g, b);
        x += 8;
        if (*s == '\n') { x = 0; y += 16; }
        s++;
    }
}

u32 drm_get_width (void) { return drm_dev.primary_fb.width;  }
u32 drm_get_height(void) { return drm_dev.primary_fb.height; }

// =====================================================================
// Text console
// =====================================================================

static void scroll_one(void) {
    drm_framebuffer_t* fb = drm_dev.crtc.fb;
    u32 words_per_line = fb->pitch / 4;
    volatile u32* base = (volatile u32*)fb->vaddr;

    for (u32 y = 0; y < fb->height - 16; y++) {
        for (u32 x = 0; x < fb->width; x++) {
            u32 dst = y * words_per_line + x;
            u32 src = (y + 16) * words_per_line + x;
            base[dst] = base[src];
        }
    }
    for (u32 y = fb->height - 16; y < fb->height; y++) {
        for (u32 x = 0; x < fb->width; x++) {
            base[y * words_per_line + x] = drm_dev.text_bg;
        }
    }
}

static void console_putchar(char c) {
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
        if (drm_dev.cursor_x > 0) {
            drm_dev.cursor_x--;
            drm_framebuffer_t* fb = drm_dev.crtc.fb;
            u32 words_per_line = fb->pitch / 4;
            volatile u32* base = (volatile u32*)fb->vaddr;
            for (u32 dy = 0; dy < 16; dy++) {
                for (u32 dx = 0; dx < 8; dx++) {
                    u32 px = drm_dev.cursor_x * 8 + dx;
                    u32 py = drm_dev.cursor_y * 16 + dy;
                    if (px < fb->width && py < fb->height) {
                        base[py * words_per_line + px] = drm_dev.text_bg;
                    }
                }
            }
        }
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

    drm_draw_char(c,
                  drm_dev.cursor_x * 8,
                  drm_dev.cursor_y * 16,
                  (drm_dev.text_fg >> 16) & 0xFF,
                  (drm_dev.text_fg >>  8) & 0xFF,
                  (drm_dev.text_fg      ) & 0xFF);
    drm_dev.cursor_x++;
}

// =====================================================================
// Legacy vesa / print shims
// =====================================================================

int  vesa_init(void)                              { return drm_init(); }
void vesa_set_framebuffer(u64 a, u32 w, u32 h, u32 p, u32 b)
                                                   { drm_set_framebuffer(a,w,h,p,b); }

void print(const char* s)                         { while (*s) console_putchar(*s++); }
void println(const char* s)                       { print(s); console_putchar('\n'); }

void printnum(u64 num) {
    char buf[32]; int i = 0;
    if (num == 0) { console_putchar('0'); return; }
    while (num > 0) { buf[i++] = '0' + (num % 10); num /= 10; }
    while (i > 0) console_putchar(buf[--i]);
}

void printhex(u64 num) {
    static const char hex[] = "0123456789ABCDEF";
    console_putchar('0'); console_putchar('x');
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
}

void print_char(char c)                           { console_putchar(c); }
int  print_is_graphic(void)                       { return drm_dev.initialized; }

void print_setpos(u8 x, u8 y) {
    if (x < drm_dev.text_cols) drm_dev.cursor_x = x;
    if (y < drm_dev.text_rows) drm_dev.cursor_y = y;
}
void print_getpos(u8* x, u8* y) {
    if (x) *x = (u8)drm_dev.cursor_x;
    if (y) *y = (u8)drm_dev.cursor_y;
}

void vesa_putpixel  (u32 x, u32 y, u8 r, u8 g, u8 b) { drm_putpixel(x,y,r,g,b); }
void vesa_clear     (u8  r, u8 g, u8 b)              { drm_clear(r,g,b); }
void vesa_draw_char (char c, u32 x, u32 y, u8 r, u8 g, u8 b)
                                                     { drm_draw_char(c,x,y,r,g,b); }
void vesa_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b)
                                                     { drm_draw_string(s,x,y,r,g,b); }
u32  vesa_get_width (void)                          { return drm_get_width(); }
u32  vesa_get_height(void)                          { return drm_get_height(); }
int  vesa_is_active (void)                          { return drm_is_active(); }

// =====================================================================
// Linux DRM UAPI dispatcher (libdrm / Mesa)
//
// Implements the subset of ioctls that Mesa's KMS/DRM frontends call.
// The dispatcher matches ioctl numbers against the DRM_IOC() macros
// declared in drm.h. Each handler populates the caller-provided struct
// in place; the future VFS layer for /dev/dri/card0 is responsible
// for doing copy_to_user / copy_from_user on `arg`.
//
// At the moment the only "real" device is a single virtual CRTC and
// one connector advertising the boot framebuffer. Most handlers are
// just enough to make a libdrm + modeset program run without EINVAL;
// they're stubs that report what the device actually has.
// =====================================================================

// ---- Helpers ---------------------------------------------------------

// Convert an in-kernel drm_display_mode_t to the wire format that
// libdrm expects. The two structs have the same field order; the only
// thing to watch is that the wire struct has 16-bit fields where the
// in-kernel one has 32-bit fields.
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
    for (int i = 0; i < 32; i++) out->name[i] = in ? 0 : 0; // zero
    // A short human-readable label helps debugging from userspace.
    const char label[] = "UTMS7";
    for (int i = 0; i < 32 && label[i]; i++) out->name[i] = label[i];
}

// True if the driver is ready to service UAPI calls. Until then every
// ioctl returns -ENODEV so userspace knows to fall back.
static int drm_uapi_ready(void) {
    return drm_dev.initialized;
}

// ---- Individual ioctl handlers ---------------------------------------
//
// Each handler returns 0 on success, or a negative errno on failure.
// They all assume `arg` is a kernel-accessible pointer. The future VFS
// handler for /dev/dri/card0 will translate userspace pointers with
// copy_from_user / copy_to_user.

static int drm_uapi_version(struct drm_version* v) {
    if (!v) return -22; // -EINVAL
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
            c->value = 0;
            break;
        case DRM_CAP_CURSOR_WIDTH:
        case DRM_CAP_CURSOR_HEIGHT:
            c->value = 64;
            break;
        default:
            return -22; // -EINVAL
    }
    return 0;
}

static int drm_uapi_get_resources(struct drm_mode_card_res* r) {
    if (!r) return -22;
    // 1 CRTC, 1 connector, 0 encoders, 1 framebuffer (the primary).
    r->count_crtcs      = 1;
    r->count_connectors = 1;
    r->count_encoders   = 0;
    r->count_fbs        = 1;
    r->min_width        = 0;
    r->max_width        = drm_dev.primary_fb.width;
    r->min_height       = 0;
    r->max_height       = drm_dev.primary_fb.height;
    if (r->crtc_id_ptr) {
        u32 id = drm_dev.crtc.id;
        for (u32 i = 0; i < r->count_crtcs; i++) {
            ((u32*)r->crtc_id_ptr)[i] = id;
        }
    }
    if (r->connector_id_ptr) {
        u32 id = drm_dev.connector.id;
        for (u32 i = 0; i < r->count_connectors; i++) {
            ((u32*)r->connector_id_ptr)[i] = id;
        }
    }
    if (r->fb_id_ptr) {
        ((u32*)r->fb_id_ptr)[0] = 1;
    }
    return 0;
}

static int drm_uapi_get_connector(struct drm_mode_get_connector* c) {
    if (!c) return -22;
    // Translate our internal connector id (1) into the UAPI fields.
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
    c->count_encoders     = 0;
    // Fill the modes array.
    if (c->modes_ptr && c->count_modes > 0) {
        u32 n = c->count_modes;
        if (n > drm_dev.connector.mode_count) n = drm_dev.connector.mode_count;
        for (u32 i = 0; i < n; i++) {
            mode_to_uapi(&drm_dev.connector.modes[i],
                         ((struct drm_mode_modeinfo*)c->modes_ptr) + i);
        }
        c->count_modes = n;
    }
    return 0;
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
    return 0;
}

static int drm_uapi_get_fb(struct drm_mode_fb_cmd* f) {
    if (!f) return -22;
    // Only the primary framebuffer (id 1) is exposed.
    if (f->fb_id != 0 && f->fb_id != 1) return -2; // -ENOENT
    f->fb_id  = 1;
    f->width  = drm_dev.primary_fb.width;
    f->height = drm_dev.primary_fb.height;
    f->pitch  = drm_dev.primary_fb.pitch;
    f->bpp    = drm_dev.primary_fb.bpp;
    f->depth  = 24;
    f->handle = 1;
    return 0;
}

static int drm_uapi_add_fb(struct drm_mode_fb_cmd* f) {
    if (!f) return -22;
    // We don't yet support creating new framebuffers from userspace.
    // The primary one is id 1, with the boot parameters.
    if (f->width != 0 && f->height != 0) return -38; // -ENOSYS
    return drm_uapi_get_fb(f);
}

static int drm_uapi_rm_fb(struct drm_mode_fb_cmd* f) {
    if (!f) return -22;
    // The primary framebuffer is owned by the device and can't be
    // removed via this ioctl. Anything else is unsupported.
    if (f->fb_id == 0 || f->fb_id == 1) return -1; // -EPERM
    return -38; // -ENOSYS
}

static int drm_uapi_get_planes(struct drm_mode_get_plane_res* p) {
    if (!p) return -22;
    p->count_planes = 0;
    return 0;
}

static u32 next_gem_handle = 2; // 0 and 1 are reserved (primary fb)

static int drm_uapi_gem_create(struct drm_gem_create* g) {
    if (!g) return -22;
    g->handle = next_gem_handle++;
    g->pad    = 0;
    return 0;
}

static int drm_uapi_gem_close(struct drm_gem_close* g) {
    if (!g) return -22;
    // We don't actually track allocations, so this is a no-op.
    return 0;
}

static int drm_uapi_gem_mmap(struct drm_gem_mmap* m) {
    if (!m) return -22;
    // Fake offset that user-space would mmap(). The VFS layer is
    // responsible for resolving the offset to a real mapping.
    m->offset = (u64)m->handle * 0x1000000ULL;
    return 0;
}

// ---- Dispatcher ------------------------------------------------------

int drm_ioctl(unsigned int cmd, unsigned long arg) {
    if (!drm_uapi_ready()) return -19; // -ENODEV

    void* a = (void*)arg;
    switch (cmd) {
        case DRM_IOCTL_VERSION:               return drm_uapi_version((struct drm_version*)a);
        case DRM_IOCTL_GET_CAP:               return drm_uapi_get_cap((struct drm_get_cap*)a);
        case DRM_IOCTL_MODE_GETRESOURCES:     return drm_uapi_get_resources((struct drm_mode_card_res*)a);
        case DRM_IOCTL_MODE_GETCRTC:          return drm_uapi_get_crtc((struct drm_mode_crtc*)a);
        case DRM_IOCTL_MODE_GETCONNECTOR:     return drm_uapi_get_connector((struct drm_mode_get_connector*)a);
        case DRM_IOCTL_MODE_GETFB:            return drm_uapi_get_fb((struct drm_mode_fb_cmd*)a);
        case DRM_IOCTL_MODE_ADDFB:            return drm_uapi_add_fb((struct drm_mode_fb_cmd*)a);
        case DRM_IOCTL_MODE_RMFB:             return drm_uapi_rm_fb((struct drm_mode_fb_cmd*)a);
        case DRM_IOCTL_MODE_GETPLANERESOURCES:return drm_uapi_get_planes((struct drm_mode_get_plane_res*)a);
        case DRM_IOCTL_GEM_CREATE:            return drm_uapi_gem_create((struct drm_gem_create*)a);
        case DRM_IOCTL_GEM_CLOSE:             return drm_uapi_gem_close((struct drm_gem_close*)a);
        case DRM_IOCTL_GEM_MMAP:              return drm_uapi_gem_mmap((struct drm_gem_mmap*)a);
        default:                              return -25; // -ENOTTY
    }
}

u64 drm_mmap_fb(u64 offset, u64 size) {
    (void)size;
    if (offset == 0) {
        return drm_dev.primary_fb.paddr;
    }
    return 0;
}

// drivers/drm.c — добавить в конец перед module glue

// UEFI GOP GUID
#define EFI_GOP_GUID { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef struct {
    u32 data1; u16 data2; u16 data3; u8 data4[8];
} __attribute__((packed)) efi_guid_t;

typedef struct {
    u64 signature; u32 revision; u32 header_size; u32 crc32; u32 reserved;
} __attribute__((packed)) efi_table_header_t;

typedef struct {
    efi_table_header_t hdr;
    u64 get_memory_map; u64 allocate_pool; u64 free_pool; u64 create_event;
    u64 set_timer; u64 wait_for_event; u64 signal_event; u64 close_event;
    u64 check_event; u64 install_protocol_interface; u64 reinstall_protocol_interface;
    u64 uninstall_protocol_interface; u64 handle_protocol; u64 reserved2;
    u64 register_protocol_notify; u64 locate_handle; u64 locate_device_path;
    u64 install_configuration_table; u64 image_load; u64 image_start; u64 exit;
    u64 image_unload; u64 exit_boot_services; u64 get_next_monotonic_count;
    u64 stall; u64 set_watchdog_timer; u64 connect_controller; u64 disconnect_controller;
    u64 open_protocol; u64 close_protocol; u64 open_protocol_information;
    u64 protocols_per_handle; u64 locate_handle_buffer; u64 locate_protocol;
    u64 install_multiple_protocol_interfaces; u64 uninstall_multiple_protocol_interfaces;
    u64 calculate_crc32; u64 copy_mem; u64 set_mem; u64 create_event_ex;
} __attribute__((packed)) efi_boot_services_t;

typedef struct {
    u32 mode; u32 info_size; u64 info; u64 size_of_info;
} __attribute__((packed)) efi_gop_mode_t;

typedef struct {
    u32 version; u32 width; u32 height; u32 pixel_format; u32 pixels_per_scanline;
} __attribute__((packed)) efi_gop_mode_info_t;

typedef struct {
    u64 query_mode; u64 set_mode; u64 blt;
    efi_gop_mode_t *mode;
} __attribute__((packed)) efi_gop_t;

static int efi_guid_equals(efi_guid_t *a, efi_guid_t *b) {
    u32 *pa = (u32*)a, *pb = (u32*)b;
    return pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2] && pa[3] == pb[3];
}

static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void drm_parse_multiboot(u64 mb_info) {
    outb(0x3F8, 'D');
    outb(0x3F8, 'R');
    outb(0x3F8, 'M');
    outb(0x3F8, '\n');

    if (!mb_info) {
        outb(0x3F8, 'N');
        drm_set_framebuffer(0xFD000000, 1024, 768, 4096, 32);
        return;
    }

    efi_guid_t gop_guid = EFI_GOP_GUID;
    u8* ptr = (u8*)(mb_info + 8);
    efi_boot_services_t *bs = NULL;
    int found_fb = 0;

    while (1) {
        u32 type = *(u32*)ptr;
        u32 size = *(u32*)(ptr + 4);
        if (type == 0 || size == 0) break;

        if (type == 8) {
            // Multiboot2 framebuffer tag (BIOS/legacy)
            drm_set_framebuffer(*(u64*)(ptr + 8), *(u32*)(ptr + 20),
                                *(u32*)(ptr + 24), *(u32*)(ptr + 16),
                                *(u8*)(ptr + 28));
            found_fb = 1;
            break;
        }

        if (type == 12) {
            // EFI boot services pointer
            bs = (efi_boot_services_t*)*(u64*)(ptr + 8);
        }

        ptr += (size + 7) & ~7;
    }

    if (found_fb) return;

    if (bs) {
        // Try GOP via EFI
        efi_gop_t *gop = NULL;
        u64 gop_ptr = 0;

        // Call bs->locate_protocol(&gop_guid, NULL, &gop_ptr)
        // EFI calling convention: rcx=arg1, rdx=arg2, r8=arg3
        u64 fn = bs->locate_protocol;
        if (fn) {
            __asm__ volatile (
                "mov %1, %%rcx\n"
                "xor %%rdx, %%rdx\n"
                "lea %2, %%r8\n"
                "call *%3\n"
                : "=a"(gop_ptr)
                : "r"(&gop_guid), "m"(gop_ptr), "r"(fn)
                : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
            );
        }

        if (gop_ptr) {
            gop = *(efi_gop_t**)gop_ptr;
            if (gop && gop->mode && gop->mode->info) {
                efi_gop_mode_info_t *info = (efi_gop_mode_info_t*)gop->mode->info;
                // Get framebuffer base from mode info
                u64 fb_addr = *(u64*)((u8*)info + sizeof(efi_gop_mode_info_t));
                u32 fb_size = *(u32*)((u8*)info + sizeof(efi_gop_mode_info_t) + 8);
                u32 width = info->width;
                u32 height = info->height;
                u32 pixels_per_scanline = info->pixels_per_scanline;
                u32 pitch = pixels_per_scanline * 4;  // assume 32bpp

                drm_set_framebuffer(fb_addr, width, height, pitch, 32);
                return;
            }
        }
    }

    // Fallback
    drm_set_framebuffer(0xFD000000, 1024, 768, 4096, 32);
}

// =====================================================================
// Module glue (matches the convention in module.h / old vesa.c)
// =====================================================================

static const char __drm_name[] __attribute__((section(".module_name"))) = "drm";
static int (*__drm_entry)(void) __attribute__((section(".module_entry"))) = drm_init;
