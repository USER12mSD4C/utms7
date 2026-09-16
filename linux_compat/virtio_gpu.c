#include "types.h"
#include "pci.h"
#include "dma.h"
#include "../kernel/core/memory.h"
#include "../kernel/arch/x86_64/paging.h"
#include "../kernel/core/sched.h"
#include "../kernel/drivers/gpu/drm.h"
#include "../include/string.h"
#include <linux/virtio_gpu.h>

extern u32 get_ticks(void);
extern void (*kernel_idle_hook)(void);
void sched_set_deferred_irq_cb(void (*cb)(void));
extern void mmio_register(u64 base, u64 len);

#define VG_QUEUE_SIZE 64
#define VG_MAX_REQ    32
#define VG_SLOT_STRIDE 1024
#define VG_SLOT_OUT   0
#define VG_SLOT_IN    512
#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

struct vg_desc { u64 addr; u32 len; u16 flags; u16 next; } __attribute__((packed));
struct vg_avail { u16 flags; u16 idx; u16 ring[VG_QUEUE_SIZE]; u16 used_event; } __attribute__((packed));
struct vg_used_elem { u32 id; u32 len; } __attribute__((packed));
struct vg_used { u16 flags; u16 idx; struct vg_used_elem ring[VG_QUEUE_SIZE]; u16 avail_event; } __attribute__((packed));

#define VC_DEV_FEATURE_SEL 0
#define VC_DEV_FEATURE     4
#define VC_DRV_FEATURE_SEL 8
#define VC_DRV_FEATURE     12
#define VC_STATUS          20
#define VC_NUM_QUEUES      18
#define VC_Q_SELECT        22
#define VC_Q_SIZE          24
#define VC_Q_ENABLE        28
#define VC_Q_NOTIFY_OFF    30
#define VC_Q_DESC          32
#define VC_Q_AVAIL         40
#define VC_Q_USED          48

static volatile u8* vg_common;
static volatile u8* vg_notify;
static u32 vg_notify_mult = 4;
static u16 vg_notify_off_ctrl;

static struct vg_desc* vg_desc;
static struct vg_avail* vg_avail;
static struct vg_used* vg_used;
static u64 vg_desc_phys, vg_avail_phys, vg_used_phys;
static u16 vg_used_idx;

static u8* vg_slots;
static u64 vg_slots_phys;
static volatile u8 slot_busy[VG_MAX_REQ];
static volatile u8 slot_done[VG_MAX_REQ];
static volatile u32 slot_resp[VG_MAX_REQ];

static u8* vg_fb;
static u64 vg_fb_phys;
static u32 vg_fb_w, vg_fb_h, vg_fb_pitch;

static volatile int vg_in_flush;
static u32 vg_last_flush_tick;
static int vg_flush_ok;
static int vg_flush_err;

static u8 vc8(u32 off) { return vg_common[off]; }
static void vcw8(u32 off, u8 v) { vg_common[off] = v; }
static u16 vc16(u32 off) { return *(volatile u16*)(vg_common + off); }
static void vcw16(u32 off, u16 v) { *(volatile u16*)(vg_common + off) = v; }
static u32 vc32(u32 off) { return *(volatile u32*)(vg_common + off); }
static void vcw32(u32 off, u32 v) { *(volatile u32*)(vg_common + off) = v; }
static void vcw64(u32 off, u64 v) { *(volatile u64*)(vg_common + off) = v; }

static void* vg_map_bar(struct pci_dev* dev, int bar) {
    u64 base = pci_resource_start(dev, bar);
    u64 len = pci_resource_len(dev, bar);
    if (base == 0 || len == 0) return NULL;
    mmio_register(base, len);
    for (u64 o = 0; o < len; o += 4096) {
        paging_map(base + o, base + o, PAGE_PRESENT | PAGE_WRITABLE | (1 << 4));
    }
    return (void*)base;
}

static int vg_find_caps(struct pci_dev* dev) {
    u16 status = pci_read_config_word(dev, PCI_STATUS);
    if (!(status & 0x10)) return -1;
    u8 ptr = (u8)(pci_read_config_byte(dev, 0x34) & 0xFC);
    while (ptr) {
        u8 id = pci_read_config_byte(dev, ptr);
        u8 next = (u8)(pci_read_config_byte(dev, ptr + 1) & 0xFC);
        if (id == 0x09) {
            u8 cfg_type = pci_read_config_byte(dev, ptr + 3);
            u8 bar = pci_read_config_byte(dev, ptr + 4);
            u32 off = pci_read_config_dword(dev, ptr + 8);
            void* base = vg_map_bar(dev, bar);
            if (!base) { ptr = next; continue; }
            if (cfg_type == 1) {
                vg_common = (volatile u8*)base + off;
            } else if (cfg_type == 2) {
                vg_notify = (volatile u8*)base + off;
                vg_notify_mult = pci_read_config_dword(dev, ptr + 16);
                if (vg_notify_mult == 0) vg_notify_mult = 4;
            }
        }
        ptr = next;
    }
    if (!vg_common || !vg_notify) return -1;
    return 0;
}

static int vg_setup_queue(u16 idx) {
    vcw16(VC_Q_SELECT, idx);
    if (vc16(VC_Q_SIZE) == 0) return -1;
    vg_desc = dma_alloc_coherent(NULL, 4096, &vg_desc_phys, 0);
    vg_avail = dma_alloc_coherent(NULL, 4096, &vg_avail_phys, 0);
    vg_used = dma_alloc_coherent(NULL, 4096, &vg_used_phys, 0);
    vg_slots = dma_alloc_coherent(NULL, VG_MAX_REQ * VG_SLOT_STRIDE, &vg_slots_phys, 0);
    if (!vg_desc || !vg_avail || !vg_used || !vg_slots) return -1;
    vcw64(VC_Q_DESC, vg_desc_phys);
    vcw64(VC_Q_AVAIL, vg_avail_phys);
    vcw64(VC_Q_USED, vg_used_phys);
    vcw16(VC_Q_ENABLE, 1);
    vg_notify_off_ctrl = vc16(VC_Q_NOTIFY_OFF);
    vg_used_idx = 0;
    return 0;
}

static void vg_reset_queue(void) {
    vcw16(VC_Q_SELECT, 0);
    vcw16(VC_Q_ENABLE, 0);
    for (u32 i = 0; i < VG_QUEUE_SIZE; i++) {
        vg_desc[i].addr = 0;
        vg_desc[i].len = 0;
        vg_desc[i].flags = 0;
        vg_desc[i].next = 0;
    }
    vg_avail->idx = 0;
    vg_used->idx = 0;
    vg_used_idx = 0;
    for (u32 s = 0; s < VG_MAX_REQ; s++) {
        slot_busy[s] = 0;
        slot_done[s] = 0;
    }
    vcw64(VC_Q_DESC, vg_desc_phys);
    vcw64(VC_Q_AVAIL, vg_avail_phys);
    vcw64(VC_Q_USED, vg_used_phys);
    vcw16(VC_Q_ENABLE, 1);
}

static void vg_poll_used(void) {
    u16 ui = vg_used->idx;
    __asm__ volatile("" ::: "memory");
    while (vg_used_idx != ui) {
        u32 id = vg_used->ring[vg_used_idx & (VG_QUEUE_SIZE - 1)].id;
        u32 slot = id / 2;
        if (slot < VG_MAX_REQ) {
            slot_resp[slot] = *(volatile u32*)(vg_slots + slot * VG_SLOT_STRIDE + VG_SLOT_IN);
            __asm__ volatile("" ::: "memory");
            slot_done[slot] = 1;
        }
        vg_used_idx++;
        ui = vg_used->idx;
    }
}

static int vg_ctrl(const void* out, u32 out_len, u32 in_len, void* resp_out) {
    int slot = -1;
    for (u32 i = 0; i < VG_MAX_REQ; i++) {
        if (!slot_busy[i]) { slot = (int)i; break; }
    }
    if (slot < 0) return -1;
    slot_busy[slot] = 1;
    slot_done[slot] = 0;
    slot_resp[slot] = 0;

    memcpy((void*)(vg_slots + slot * VG_SLOT_STRIDE + VG_SLOT_OUT), out, out_len);

    u16 h = (u16)(slot * 2);
    vg_desc[h].addr = vg_slots_phys + slot * VG_SLOT_STRIDE + VG_SLOT_OUT;
    vg_desc[h].len = out_len;
    vg_desc[h].flags = VRING_DESC_F_NEXT;
    vg_desc[h].next = (u16)(h + 1);
    vg_desc[h + 1].addr = vg_slots_phys + slot * VG_SLOT_STRIDE + VG_SLOT_IN;
    vg_desc[h + 1].len = in_len;
    vg_desc[h + 1].flags = VRING_DESC_F_WRITE;
    vg_desc[h + 1].next = 0;

    __asm__ volatile("" ::: "memory");
    vg_avail->ring[vg_avail->idx & (VG_QUEUE_SIZE - 1)] = h;
    __asm__ volatile("" ::: "memory");
    vg_avail->idx++;
    *(volatile u16*)(vg_notify + (u64)vg_notify_off_ctrl * vg_notify_mult) = 0;

    u32 deadline = get_ticks() + 50;
    u64 spins = 0;
    while (!slot_done[slot]) {
        vg_poll_used();
        if (slot_done[slot]) break;
        if (++spins > 200000000ULL) break;
        if ((i32)(get_ticks() - deadline) > 0) break;
        __asm__ volatile("pause");
    }

    if (!slot_done[slot]) {
        slot_busy[slot] = 0;
        vg_reset_queue();
        return -1;
    }

    if (resp_out && in_len) {
        memcpy(resp_out, (void*)(vg_slots + slot * VG_SLOT_STRIDE + VG_SLOT_IN), in_len);
    }
    u32 resp = slot_resp[slot];
    slot_busy[slot] = 0;
    if (in_len >= 4) return (int)resp;
    return 0;
}

static int vg_cmd_simple(u32 type, void* payload, u32 payload_len, u32 resp_type, void* resp, u32 resp_len) {
    u8 buf[512];
    if (sizeof(struct virtio_gpu_ctrl_hdr) + payload_len > sizeof(buf)) return -1;
    memset(buf, 0, sizeof(buf));
    struct virtio_gpu_ctrl_hdr* h = (struct virtio_gpu_ctrl_hdr*)buf;
    h->type = type;
    if (payload_len) memcpy(buf + sizeof(*h), payload, payload_len);
    u8 rbuf[512];
    memset(rbuf, 0, sizeof(rbuf));
    int r = vg_ctrl(buf, sizeof(*h) + payload_len, resp_len, rbuf);
    if (r != (int)resp_type) return -1;
    if (resp && resp_len) memcpy(resp, rbuf, resp_len);
    return 0;
}

static int vg_transfer_flush(u32 x, u32 y, u32 w, u32 h) {
    struct virtio_gpu_transfer_to_host_2d tr;
    memset(&tr, 0, sizeof(tr));
    tr.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    tr.r.x = x; tr.r.y = y; tr.r.width = w; tr.r.height = h;
    tr.offset = 0;
    tr.resource_id = 1;
    if (vg_ctrl(&tr, sizeof(tr), sizeof(struct virtio_gpu_ctrl_hdr), NULL) != VIRTIO_GPU_RESP_OK_NODATA) return -1;
    struct virtio_gpu_resource_flush fl;
    memset(&fl, 0, sizeof(fl));
    fl.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    fl.r.x = x; fl.r.y = y; fl.r.width = w; fl.r.height = h;
    fl.resource_id = 1;
    if (vg_ctrl(&fl, sizeof(fl), sizeof(struct virtio_gpu_ctrl_hdr), NULL) != VIRTIO_GPU_RESP_OK_NODATA) return -1;
    return 0;
}

static int vg_flush_now(void) {
    if (__sync_lock_test_and_set(&vg_in_flush, 1)) return -2;
    if (drm_dirty_x1 < drm_dirty_x0 || drm_dirty_y1 < drm_dirty_y0) {
        __sync_lock_release(&vg_in_flush);
        return 0;
    }
    int rc = vg_transfer_flush(0, 0, vg_fb_w, vg_fb_h);
    if (rc == 0) {
        drm_dirty_x0 = 0xFFFFFFFFu;
        drm_dirty_y0 = 0xFFFFFFFFu;
        drm_dirty_x1 = 0;
        drm_dirty_y1 = 0;
        vg_last_flush_tick = get_ticks();
        vg_flush_ok++;
    } else {
        vg_flush_err++;
    }
    __sync_lock_release(&vg_in_flush);
    if (rc == 0 && vg_flush_ok == 1) {
        print("[vgpu-flush] first hook flush OK\n");
    }
    if (rc != 0 && vg_flush_err == 1) {
        print("[vgpu-flush] first hook flush ERR code=");
        printnum((u64)(u32)rc);
        print("\n");
    }
    return rc;
}

static void vg_deferred(void) {
    if (vg_in_flush) return;
    if (drm_dirty_x1 < drm_dirty_x0 || drm_dirty_y1 < drm_dirty_y0) return;
    if (get_ticks() - vg_last_flush_tick < 16) return;
    vg_flush_now();
}

static void vg_flush_hook_wrapper(void) {
    vg_flush_now();
}

static int vg_probe(struct pci_dev* dev, const struct pci_device_id* id) {
    (void)id;
    if (pci_enable_device(dev) != 0) return -1;
    pci_set_master(dev);
    if (vg_find_caps(dev) != 0) return -1;

    vcw8(VC_STATUS, 1);
    vcw8(VC_STATUS, 3);
    vcw32(VC_DEV_FEATURE_SEL, 1);
    u32 f1 = vc32(VC_DEV_FEATURE);
    if (!(f1 & 1)) return -1;
    vcw32(VC_DRV_FEATURE_SEL, 1);
    vcw32(VC_DRV_FEATURE, 1);
    vcw8(VC_STATUS, 11);
    if (!(vc8(VC_STATUS) & 8)) return -1;
    if (vc16(VC_NUM_QUEUES) < 1) return -1;
    if (vg_setup_queue(0) != 0) return -1;
    vcw8(VC_STATUS, 15);

    u32 w = 1920;
    u32 h = 1080;
    u32 pitch = w * 4;
    u64 size = (u64)pitch * h;
    vg_fb = kmalloc_aligned(size, 4096);
    if (!vg_fb) { w = 640; h = 480; pitch = w * 4; size = (u64)pitch * h; vg_fb = kmalloc_aligned(size, 4096); }
    if (!vg_fb) return -1;
    memset(vg_fb, 0, size);
    vg_fb_phys = (u64)vg_fb;
    vg_fb_w = w; vg_fb_h = h; vg_fb_pitch = pitch;

    struct virtio_gpu_resource_create_2d cr;
    memset(&cr, 0, sizeof(cr));
    cr.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cr.resource_id = 1;
    cr.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    cr.width = w;
    cr.height = h;
    if (vg_ctrl(&cr, sizeof(cr), sizeof(struct virtio_gpu_ctrl_hdr), NULL) != VIRTIO_GPU_RESP_OK_NODATA) return -1;

    struct {
        struct virtio_gpu_resource_attach_backing ab;
        struct virtio_gpu_mem_entry ent;
    } ab;
    memset(&ab, 0, sizeof(ab));
    ab.ab.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    ab.ab.resource_id = 1;
    ab.ab.nr_entries = 1;
    ab.ent.addr = vg_fb_phys;
    ab.ent.length = (u32)size;
    if (vg_ctrl(&ab, sizeof(ab), sizeof(struct virtio_gpu_ctrl_hdr), NULL) != VIRTIO_GPU_RESP_OK_NODATA) return -1;

    struct virtio_gpu_set_scanout sc;
    memset(&sc, 0, sizeof(sc));
    sc.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    sc.r.x = 0; sc.r.y = 0; sc.r.width = w; sc.r.height = h;
    sc.scanout_id = 0;
    sc.resource_id = 1;
    if (vg_ctrl(&sc, sizeof(sc), sizeof(struct virtio_gpu_ctrl_hdr), NULL) != VIRTIO_GPU_RESP_OK_NODATA) return -1;

    drm_switch_fb(vg_fb_phys, vg_fb, w, h, pitch);
    print("[vgpu] mode ");
    printnum(w);
    print("x");
    printnum(h);
    print(" fb_phys=");
    printhex(vg_fb_phys);
    print("\n");
    drm_flush_hook = vg_flush_hook_wrapper;
    kernel_idle_hook = vg_deferred;
    sched_set_deferred_irq_cb(vg_deferred);
    vg_last_flush_tick = get_ticks();
    vg_flush_now();
    print("[vgpu] post-switch flush OK\n");
    drm_flush_sync();
    return 0;
}

static const struct pci_device_id vg_ids[] = {
    { 0x1AF4, 0x1050, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0 }
};

static struct pci_driver vg_driver = {
    .name = "virtio-gpu",
    .id_table = vg_ids,
    .probe = vg_probe,
    .remove = NULL,
};

int vgpu_init(void) {
    return pci_register_driver(&vg_driver);
}
