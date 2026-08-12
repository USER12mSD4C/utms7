// drivers/drm.h
#ifndef DRM_H
#define DRM_H

#include "../include/types.h"

typedef struct drm_display_mode {
    u32 hdisplay;
    u32 hsync_start;
    u32 hsync_end;
    u32 htotal;
    u32 vdisplay;
    u32 vsync_start;
    u32 vsync_end;
    u32 vtotal;
    u32 clock;
    u32 flags;
#define DRM_MODE_FLAG_PHSYNC  (1 << 0)
} drm_display_mode_t;

typedef struct drm_framebuffer {
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u64 paddr;
    u64 size;
    void* vaddr;
    u32 refcount;
} drm_framebuffer_t;

typedef struct drm_crtc {
    u32 id;
    int enabled;
    drm_framebuffer_t* fb;
    drm_display_mode_t mode;
    u32 x, y;
} drm_crtc_t;

typedef struct drm_connector {
    u32 id;
    int connected;
#define DRM_CONN_TYPE_VIRTUAL  1
    u32 type;
    u32 mode_count;
    drm_display_mode_t modes[8];
    drm_crtc_t* crtc;
} drm_connector_t;

typedef struct drm_device {
    int initialized;
    drm_framebuffer_t primary_fb;
    drm_crtc_t crtc;
    drm_connector_t connector;
    u32 text_cols;
    u32 text_rows;
    u32 cursor_x;
    u32 cursor_y;
    u32 text_fg;
    u32 text_bg;
} drm_device_t;

extern drm_device_t drm_dev;

void drm_set_framebuffer(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp);
int  drm_init(void);
int  drm_is_active(void);

int  drm_mode_set_crtc(u32 crtc_id,
                       drm_framebuffer_t* fb,
                       u32 x, u32 y,
                       const drm_display_mode_t* mode);

drm_framebuffer_t* drm_framebuffer_create(u64 paddr, u32 width,
                                          u32 height, u32 pitch, u32 bpp);

void drm_framebuffer_destroy(drm_framebuffer_t* fb);

u64 drm_mmap_fb(u64 offset, u64 size);

void drm_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b);
void drm_clear  (u8 r, u8 g, u8 b);
void drm_draw_char  (char c, u32 x, u32 y, u8 r, u8 g, u8 b);
void drm_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b);

u32  drm_get_width (void);
u32  drm_get_height(void);

int  vesa_init(void);
void vesa_set_framebuffer(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp);

void print(const char* s);
void println(const char* s);
void printnum(u64 num);
void printhex(u64 num);
void print_setcolor(u8 fg, u8 bg);
void print_clear(void);
void print_char(char c);
int  print_is_graphic(void);
void print_setpos(u8 x, u8 y);
void print_getpos(u8* x, u8* y);

void vesa_putpixel(u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_clear  (u8 r, u8 g, u8 b);
void vesa_draw_char  (char c, u32 x, u32 y, u8 r, u8 g, u8 b);
void vesa_draw_string(const char* s, u32 x, u32 y, u8 r, u8 g, u8 b);
u32  vesa_get_width (void);
u32  vesa_get_height(void);
int  vesa_is_active(void);

#define DRM_IOCTL_BASE                  'd'
#define DRM_IOC_NONE                    0U
#define DRM_IOC_WRITE                   1U
#define DRM_IOC_READ                    2U
#define DRM_IOC_RW                      (DRM_IOC_WRITE | DRM_IOC_READ)
#define DRM_IOC_SIZESHIFT               16
#define DRM_IOC_SIZEMASK                ((1U << 14) - 1)
#define DRM_IOC_NRSHIFT                 0
#define DRM_IOC_NRMASK                  ((1U << 8) - 1)
#define DRM_IOC_DIRSHIFT                30
#define DRM_IOC(dir, type, nr, size)    (((dir)   << DRM_IOC_DIRSHIFT) | \
                                         ((type)  << 8)                  | \
                                         ((nr)    << DRM_IOC_NRSHIFT)    | \
                                         ((size)  << DRM_IOC_SIZESHIFT))

#define DRM_IO(nr, size)                DRM_IOC(DRM_IOC_NONE,  DRM_IOCTL_BASE, (nr), (size))
#define DRM_IOR(nr, sz)                 DRM_IOC(DRM_IOC_READ,  DRM_IOCTL_BASE, (nr), sizeof(sz))
#define DRM_IOW(nr, sz)                 DRM_IOC(DRM_IOC_WRITE, DRM_IOCTL_BASE, (nr), sizeof(sz))
#define DRM_IOWR(nr, sz)                DRM_IOC(DRM_IOC_RW,    DRM_IOCTL_BASE, (nr), sizeof(sz))

#define DRM_CAP_DUMB_BUFFER             0x1
#define DRM_CAP_VBLANK_HIGH_CRTC        0x2
#define DRM_CAP_DUMB_PREFERRED_DEPTH    0x3
#define DRM_CAP_DUMB_PREFER_SHADOW      0x4
#define DRM_CAP_PRIME                   0x5
#define DRM_CAP_TIMESTAMP_MONOTONIC     0x6
#define DRM_CAP_ASYNC_PAGE_FLIP         0x7
#define DRM_CAP_CURSOR_WIDTH            0x8
#define DRM_CAP_CURSOR_HEIGHT           0x9

struct drm_gem_close {
    u32 handle;
    u32 pad;
};
struct drm_gem_flink {
    u32 handle;
    u32 name;
};
struct drm_gem_open {
    u32 name;
    u32 handle;
    u64 size;
};

struct drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    u32 name_len;
    char* name;
    u32 date_len;
    char* date;
    u32 desc_len;
    char* desc;
};

struct drm_get_cap {
    u64 capability;
    u64 value;
};

#define DRM_DISPLAY_MODE_LEN            32
#define DRM_CONNECTOR_NAME_LEN          32
#define DRM_PROP_NAME_LEN               32

#define DRM_MODE_TYPE_BUILTIN           (1 << 0)
#define DRM_MODE_TYPE_PREFERRED         (1 << 3)
#define DRM_MODE_TYPE_USERDEF           (1 << 5)
#define DRM_MODE_TYPE_DRIVER            (1 << 6)

#define DRM_MODE_FLAG_NHSYNC            (1 << 1)
#define DRM_MODE_FLAG_PVSYNC            (1 << 2)
#define DRM_MODE_FLAG_NVSYNC            (1 << 3)

#define DRM_MODE_CONNECTOR_Unknown      0
#define DRM_MODE_CONNECTOR_VGA          1
#define DRM_MODE_CONNECTOR_DVII         2
#define DRM_MODE_CONNECTOR_DVID         3
#define DRM_MODE_CONNECTOR_DVIA         4
#define DRM_MODE_CONNECTOR_Composite     5
#define DRM_MODE_CONNECTOR_SVIDEO       6
#define DRM_MODE_CONNECTOR_LVDS         7
#define DRM_MODE_CONNECTOR_Component    8
#define DRM_MODE_CONNECTOR_9PinDIN      9
#define DRM_MODE_CONNECTOR_DisplayPort  10
#define DRM_MODE_CONNECTOR_HDMIA        11
#define DRM_MODE_CONNECTOR_HDMIB        12
#define DRM_MODE_CONNECTOR_TV           13
#define DRM_MODE_CONNECTOR_eDP          14
#define DRM_MODE_CONNECTOR_VIRTUAL       15
#define DRM_MODE_CONNECTOR_DSI          16
#define DRM_MODE_CONNECTOR_DPI          17

#define DRM_MODE_CONNECTED              1
#define DRM_MODE_DISCONNECTED           2
#define DRM_MODE_UNKNOWNCONNECTION      3

struct drm_mode_modeinfo {
    u32 clock;
    u16 hdisplay;
    u16 hsync_start;
    u16 hsync_end;
    u16 htotal;
    u16 hskew;
    u16 vdisplay;
    u16 vsync_start;
    u16 vsync_end;
    u16 vtotal;
    u16 vscan;
    u32 vrefresh;
    u32 flags;
    u32 type;
    char name[DRM_DISPLAY_MODE_LEN];
};

struct drm_mode_card_res {
    u64 fb_id_ptr;
    u64 crtc_id_ptr;
    u64 connector_id_ptr;
    u64 encoder_id_ptr;
    u32 count_fbs;
    u32 count_crtcs;
    u32 count_connectors;
    u32 count_encoders;
    u32 min_width;
    u32 max_width;
    u32 min_height;
    u32 max_height;
};

struct drm_mode_crtc {
    u64 set_connectors_ptr;
    u32 count_connectors;
    u32 crtc_id;
    u32 fb_id;
    u32 x;
    u32 y;
    u32 gamma_size;
    u32 mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_get_connector {
    u64 encoders_ptr;
    u64 modes_ptr;
    u64 props_ptr;
    u64 prop_values_ptr;
    u32 count_modes;
    u32 count_props;
    u32 count_encoders;
    u32 encoder_id;
    u32 connector_id;
    u32 connector_type;
    u32 connector_type_id;
    u32 connection;
    u32 mm_width;
    u32 mm_height;
    u32 subpixel;
    u32 pad;
};

struct drm_mode_get_encoder {
    u32 encoder_id;
    u32 encoder_type;
    u32 crtc_id;
    u32 possible_crtcs;
    u32 possible_clones;
};

struct drm_mode_fb_cmd {
    u32 fb_id;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u32 depth;
    u32 handle;
};

struct drm_mode_fb_cmd2 {
    u32 fb_id;
    u32 width;
    u32 height;
    u32 pixel_format;
    u32 flags;
    u32 handles[4];
    u32 pitches[4];
    u32 offsets[4];
    u64 modifier[4];
};

struct drm_mode_get_plane {
    u32 plane_id;
    u32 crtc_id;
    u32 fb_id;
    u32 possible_crtcs;
    u32 gamma_size;
    u32 count_format_types;
    u64 format_type_ptr;
};

struct drm_mode_get_plane_res {
    u64 plane_id_ptr;
    u32 count_planes;
};

struct drm_mode_create_dumb {
    u32 height;
    u32 width;
    u32 bpp;
    u32 flags;
    u32 handle;
    u32 pitch;
    u64 size;
};

struct drm_mode_map_dumb {
    u32 handle;
    u32 pad;
    u64 offset;
};

struct drm_mode_destroy_dumb {
    u32 handle;
};

struct drm_gem_create {
    u64 size;
    u32 handle;
    u32 pad;
};

struct drm_gem_mmap {
    u32 handle;
    u32 pad;
    u64 offset;
};

#define DRM_FORMAT_XRGB8888             0x34325258
#define DRM_FORMAT_ARGB8888             0x34325241
#define DRM_FORMAT_RGB888               0x34324752

#define DRM_IOCTL_GEM_CREATE            DRM_IOWR(0x10, struct drm_gem_create)
#define DRM_IOCTL_GEM_MMAP              DRM_IOWR(0x11, struct drm_gem_mmap)
#define DRM_IOCTL_VERSION               DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE            DRM_IOWR(0x01, struct drm_version)
#define DRM_IOCTL_GET_MAGIC             DRM_IOR (0x02, struct drm_version)
#define DRM_IOCTL_GET_CAP               DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_GEM_CLOSE             DRM_IOW (0x09, struct drm_gem_close)
#define DRM_IOCTL_GEM_FLINK             DRM_IOWR(0x0a, struct drm_gem_flink)
#define DRM_IOCTL_GEM_OPEN              DRM_IOWR(0x0b, struct drm_gem_open)
#define DRM_IOCTL_MODE_GETRESOURCES     DRM_IOWR(0xa0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC          DRM_IOWR(0xa1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC          DRM_IOWR(0xa2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER       DRM_IOWR(0xa6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR     DRM_IOWR(0xa7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETPLANE         DRM_IOWR(0xb6, struct drm_mode_get_plane)
#define DRM_IOCTL_MODE_GETPLANERESOURCES DRM_IOWR(0xb5, struct drm_mode_get_plane_res)
#define DRM_IOCTL_MODE_GETFB            DRM_IOWR(0xad, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_ADDFB            DRM_IOWR(0xae, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB             DRM_IOWR(0xaf, unsigned int)
#define DRM_IOCTL_MODE_ADDFB2           DRM_IOWR(0xb8, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_CREATE_DUMB      DRM_IOWR(0xb2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB         DRM_IOWR(0xb3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB     DRM_IOWR(0xb4, struct drm_mode_destroy_dumb)

void drm_parse_multiboot(u64 mb_info);
int drm_ioctl(unsigned int cmd, unsigned long arg);

#endif
