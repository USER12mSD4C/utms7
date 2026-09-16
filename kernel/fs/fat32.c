#include "../../include/types.h"
#include "../../include/string.h"
#include "../drivers/storage/disk.h"
#include "../core/memory.h"
#include "../core/vfs.h"
#include "../../include/print.h"

typedef struct {
    u8  jmp[3];
    u8  oem[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  fat_count;
    u16 root_entries;
    u16 total_sectors_16;
    u8  media;
    u16 fat_size_16;
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 total_sectors_32;
    u32 fat_size_32;
    u16 flags;
    u16 version;
    u32 root_cluster;
    u16 fsinfo_sector;
    u16 backup_boot;
    u8  reserved[12];
    u8  drive_number;
    u8  reserved1;
    u8  signature;
    u32 volume_id;
    u8  volume_label[11];
    u8  system_id[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    u8  name[11];
    u8  attr;
    u8  nt_res;
    u8  create_time_tenth;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 modify_time;
    u16 modify_date;
    u16 cluster_low;
    u32 size;
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct {
    fat32_bpb_t bpb;
    u32 first_data_sector;
    u32 fat_start;
    u32 root_cluster;
    vfs_node_t* dev_node;
    int mounted;
} fat32_mount_t;

static vfs_fs_ops_t fat32_ops;

static int fat32_strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1, c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

static u32 fat32_cluster_to_lba(fat32_mount_t* mnt, u32 cluster) {
    return mnt->first_data_sector + (cluster - 2) * mnt->bpb.sectors_per_cluster;
}

static u32 fat32_next_cluster(fat32_mount_t* mnt, u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = mnt->fat_start + (fat_offset / 512);
    u32 ent_offset = fat_offset % 512;
    u8 buf[512];
    if (vfs_block_read(mnt->dev_node, buf, 512, (u64)fat_sector * 512) != 512) return 0;
    u32 next = *(u32*)(buf + ent_offset) & 0x0FFFFFFF;
    if (next >= 0x0FFFFFF8) return 0;
    return next;
}

static void fat32_format_name(const u8* raw, char* out) {
    int i = 0, j = 0;
    while (i < 8 && raw[i] != ' ') out[j++] = raw[i++];
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (int k = 8; k < 11 && raw[k] != ' '; k++) out[j++] = raw[k];
    }
    out[j] = '\0';
}

static int fat32_find_entry(fat32_mount_t* mnt, u32 dir_cluster, const char* name, fat32_dir_entry_t* out) {
    u8 buf[512];
    u32 cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = fat32_cluster_to_lba(mnt, cluster);
        for (u32 s = 0; s < mnt->bpb.sectors_per_cluster; s++) {
            if (vfs_block_read(mnt->dev_node, buf, 512, (u64)(lba + s) * 512) != 512) return -1;
            fat32_dir_entry_t* ents = (fat32_dir_entry_t*)buf;
            for (int i = 0; i < 16; i++) {
                if (ents[i].name[0] == 0x00) return -1;
                if ((u8)ents[i].name[0] == 0xE5) continue;
                if (ents[i].attr == 0x0F) continue;
                char formatted[13];
                fat32_format_name(ents[i].name, formatted);
                if (fat32_strcasecmp(formatted, name) == 0) {
                    *out = ents[i];
                    return 0;
                }
            }
        }
        cluster = fat32_next_cluster(mnt, cluster);
    }
    return -1;
}

static int fat32_vfs_lookup(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    fat32_mount_t* mnt = (fat32_mount_t*)dir->private;
    if (!mnt || !mnt->mounted) return -1;
    u32 dir_cluster = (u32)(u64)dir->fs_data;
    fat32_dir_entry_t ent;
    if (fat32_find_entry(mnt, dir_cluster, name, &ent) != 0) return -1;
    u32 cluster = ((u32)ent.cluster_high << 16) | ent.cluster_low;
    u32 type = (ent.attr & 0x10) ? VFS_DIR : VFS_FILE;
    vfs_node_t* node = vfs_create_node(name, type);
    if (!node) return -1;
    node->size = ent.size;
    node->private = mnt;
    node->fs_data = (void*)(u64)cluster;
    node->fs = &fat32_ops;
    node->parent = dir;
    *out = node;
    return 0;
}

static int fat32_vfs_read(vfs_node_t* node, void* buf, u64 size, u64 offset) {
    fat32_mount_t* mnt = (fat32_mount_t*)node->private;
    if (!mnt || !mnt->mounted || node->type != VFS_FILE) return -1;
    if (offset >= node->size) return 0;
    u64 to_copy = node->size - offset;
    if (size < to_copy) to_copy = size;
    u32 cluster = (u32)(u64)node->fs_data;
    u32 cluster_size = mnt->bpb.sectors_per_cluster * 512;
    u32 skip_clusters = offset / cluster_size;
    u32 skip_bytes = offset % cluster_size;
    for (u32 i = 0; i < skip_clusters; i++) {
        cluster = fat32_next_cluster(mnt, cluster);
        if (cluster < 2) return 0;
    }
    u8 sec_buf[512];
    u64 copied = 0;
    while (copied < to_copy && cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = fat32_cluster_to_lba(mnt, cluster);
        u32 bytes_in_cluster = cluster_size - skip_bytes;
        u64 chunk = to_copy - copied;
        if (chunk > bytes_in_cluster) chunk = bytes_in_cluster;
        u32 sec_off = skip_bytes / 512;
        u32 byte_off = skip_bytes % 512;
        u32 secs_to_read = (skip_bytes + chunk + 511) / 512;
        for (u32 s = 0; s < secs_to_read && copied < to_copy; s++) {
            if (vfs_block_read(mnt->dev_node, sec_buf, 512, (u64)(lba + sec_off + s) * 512) != 512) return copied;
            u32 start = (s == 0) ? byte_off : 0;
            u32 end = 512;
            if (copied + (end - start) > to_copy) end = start + (to_copy - copied);
            memcpy((u8*)buf + copied, sec_buf + start, end - start);
            copied += end - start;
        }
        skip_bytes = 0;
        cluster = fat32_next_cluster(mnt, cluster);
    }
    return copied;
}

static int fat32_vfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count) {
    fat32_mount_t* mnt = (fat32_mount_t*)dir->private;
    if (!mnt || !mnt->mounted || dir->type != VFS_DIR) return -1;
    u32 cluster = (u32)(u64)dir->fs_data;
    u32 n = 0, max = *count;
    u8 buf[512];
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && n < max) {
        u32 lba = fat32_cluster_to_lba(mnt, cluster);
        for (u32 s = 0; s < mnt->bpb.sectors_per_cluster && n < max; s++) {
            if (vfs_block_read(mnt->dev_node, buf, 512, (u64)(lba + s) * 512) != 512) break;
            fat32_dir_entry_t* ents = (fat32_dir_entry_t*)buf;
            for (int i = 0; i < 16 && n < max; i++) {
                if (ents[i].name[0] == 0x00) goto done;
                if ((u8)ents[i].name[0] == 0xE5) continue;
                if (ents[i].attr == 0x0F) continue;
                fat32_format_name(ents[i].name, entries[n].name);
                entries[n].type = (ents[i].attr & 0x10) ? VFS_DIR : VFS_FILE;
                entries[n].size = ents[i].size;
                n++;
            }
        }
        cluster = fat32_next_cluster(mnt, cluster);
    }
done:
    *count = n;
    return 0;
}

static int fat32_vfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir) {
    if (size) *size = node->size;
    if (mode) *mode = 0755;
    if (is_dir) *is_dir = (node->type == VFS_DIR) ? 1 : 0;
    return 0;
}

static int fat32_vfs_mount(vfs_node_t** root, const char* dev) {
    vfs_node_t* dev_node = vfs_resolve_path(dev);
    if (!dev_node || dev_node->type != VFS_BLOCK_DEVICE) return -1;
    fat32_mount_t* mnt = kmalloc(sizeof(fat32_mount_t));
    if (!mnt) return -1;
    memset(mnt, 0, sizeof(fat32_mount_t));
    mnt->dev_node = dev_node;
    u8 buf[512];
    if (vfs_block_read(dev_node, buf, 512, 0) != 512) { kfree(mnt); return -1; }
    memcpy(&mnt->bpb, buf, sizeof(fat32_bpb_t));
    if (mnt->bpb.bytes_per_sector != 512 || mnt->bpb.sectors_per_cluster == 0) { kfree(mnt); return -1; }
    u32 root_dir_sectors = ((mnt->bpb.root_entries * 32) + 511) / 512;
    mnt->fat_start = mnt->bpb.reserved_sectors;
    mnt->first_data_sector = mnt->fat_start + (mnt->bpb.fat_count * mnt->bpb.fat_size_32) + root_dir_sectors;
    mnt->root_cluster = mnt->bpb.root_cluster;
    mnt->mounted = 1;
    vfs_node_t* root_node = vfs_create_node("", VFS_DIR);
    if (!root_node) { kfree(mnt); return -1; }
    root_node->mode = 0755;
    root_node->private = mnt;
    root_node->fs_data = (void*)(u64)mnt->root_cluster;
    root_node->fs = &fat32_ops;
    *root = root_node;
    return 0;
}

static int fat32_vfs_unmount(vfs_node_t* root) {
    if (!root || !root->private) return -1;
    kfree(root->private);
    return 0;
}

static vfs_fs_ops_t fat32_ops = {
    .name = "fat32",
    .mount = fat32_vfs_mount,
    .unmount = fat32_vfs_unmount,
    .lookup = fat32_vfs_lookup,
    .read = fat32_vfs_read,
    .readdir = fat32_vfs_readdir,
    .stat = fat32_vfs_stat,
};

int fat32_register(void) {
    return vfs_register_fs(&fat32_ops);
}

#include "../../include/fs_auto.h"
FS_REGISTER("fat32", fat32_register);
