#include "../../include/types.h"
#include "../core/vfs.h"
#include "../core/memory.h"
#include "../../include/string.h"
#include "../../include/udisk.h"
#include "../drivers/storage/disk.h"
#include "../drivers/gpu/drm.h"

typedef struct {
    int disk_num;
    u64 start_lba;
    u64 sectors;
} devfs_block_private_t;

static vfs_fs_ops_t devfs_ops;

static int devfs_block_read_block(void* private, u64 lba, u32 count, void* buf) {
    devfs_block_private_t* p = (devfs_block_private_t*)private;
    if (!p) return -1;
    disk_set_disk(p->disk_num);
    return disk_read((u32)(p->start_lba + lba), count, (u8*)buf);
}

static int devfs_block_write_block(void* private, u64 lba, u32 count, const void* buf) {
    devfs_block_private_t* p = (devfs_block_private_t*)private;
    if (!p) return -1;
    disk_set_disk(p->disk_num);
    return disk_write((u32)(p->start_lba + lba), count, (u8*)buf);
}

static u64 devfs_block_get_size(void* private) {
    devfs_block_private_t* p = (devfs_block_private_t*)private;
    if (!p) return 0;
    return p->sectors * 512;
}

static vfs_block_ops_t block_ops = {
    .read = devfs_block_read_block,
    .write = devfs_block_write_block,
    .get_size = devfs_block_get_size
};

static int devfs_block_read(vfs_node_t* node, void* buf, u64 size, u64 offset) {
    if (!node || node->type != VFS_BLOCK_DEVICE || !node->block_ops) return -1;

    u64 lba = offset / 512;
    u64 off = offset % 512;
    u64 copied = 0;

    if (off != 0 || (size % 512) != 0) {
        u8 sector[512] __attribute__((aligned(16)));
        while (copied < size) {
            if (node->block_ops->read(node->private, lba, 1, sector) != 0) break;
            u64 chunk = 512 - off;
            if (chunk > size - copied) chunk = size - copied;
            memcpy((u8*)buf + copied, sector + off, chunk);
            copied += chunk;
            lba++;
            off = 0;
        }
    } else {
        u32 count = size / 512;
        if (node->block_ops->read(node->private, lba, count, buf) != 0) return 0;
        copied = size;
    }

    return (int)copied;
}

static int devfs_block_write(vfs_node_t* node, const void* buf, u64 size, u64 offset) {
    if (!node || node->type != VFS_BLOCK_DEVICE || !node->block_ops) return -1;

    u64 lba = offset / 512;
    u64 off = offset % 512;
    u64 written = 0;

    if (off != 0 || (size % 512) != 0) {
        u8 sector[512] __attribute__((aligned(16)));
        while (written < size) {
            if (off != 0 || (size - written) < 512) {
                if (node->block_ops->read(node->private, lba, 1, sector) != 0) break;
            }
            u64 chunk = 512 - off;
            if (chunk > size - written) chunk = size - written;
            memcpy(sector + off, (const u8*)buf + written, chunk);
            if (node->block_ops->write(node->private, lba, 1, sector) != 0) break;
            written += chunk;
            lba++;
            off = 0;
        }
    } else {
        u32 count = size / 512;
        if (node->block_ops->write(node->private, lba, count, (void*)buf) != 0) return 0;
        written = size;
    }

    return (int)written;
}

static int devfs_lookup(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    if (!dir || dir->type != VFS_DIR || !name || !out) return -1;

    vfs_node_t* c = dir->children;
    while (c) {
        if (strcmp(c->name, name) == 0) {
            *out = c;
            return 0;
        }
        c = c->next;
    }

    if (name[0] == 's' && name[1] == 'd' && name[2] >= 'a' && name[2] <= 'd') {
        int disk_num = name[2] - 'a';

        if (name[3] == '\0') {
            u8 drive = 0x80 + disk_num;
            u64 sectors = disk_get_sectors(drive);
            if (sectors == 0) return -1;

            vfs_node_t* node = vfs_create_node(name, VFS_BLOCK_DEVICE);
            if (!node) return -1;

            node->mode = 0660;
            node->size = sectors * 512;
            node->fs = &devfs_ops;
            node->block_ops = &block_ops;
            node->parent = dir;

            devfs_block_private_t* p = kmalloc(sizeof(devfs_block_private_t));
            if (!p) {
                kfree(node);
                return -1;
            }
            p->disk_num = disk_num;
            p->start_lba = 0;
            p->sectors = sectors;
            node->private = p;

            node->next = dir->children;
            dir->children = node;

            *out = node;
            return 0;
        }

        if (name[3] >= '1' && name[3] <= '9') {
            int part_num = name[3] - '0';

            if (name[4] >= '0' && name[4] <= '9' && name[5] == '\0') {
                part_num = (name[3] - '0') * 10 + (name[4] - '0');
            } else if (name[4] != '\0') {
                return -1;
            }

            disk_info_t* d = udisk_get_info(disk_num);
            if (!d || !d->present) return -1;

            partition_t* part = NULL;
            for (int i = 0; i < d->partition_count; i++) {
                if (d->partitions[i].present && d->partitions[i].partition_num == part_num) {
                    part = &d->partitions[i];
                    break;
                }
            }

            if (!part) return -1;

            vfs_node_t* node = vfs_create_node(name, VFS_BLOCK_DEVICE);
            if (!node) return -1;

            node->mode = 0660;
            node->size = (part->end_lba - part->start_lba + 1) * 512;
            node->fs = &devfs_ops;
            node->block_ops = &block_ops;
            node->parent = dir;

            devfs_block_private_t* p = kmalloc(sizeof(devfs_block_private_t));
            if (!p) {
                kfree(node);
                return -1;
            }
            p->disk_num = disk_num;
            p->start_lba = part->start_lba;
            p->sectors = part->end_lba - part->start_lba + 1;
            node->private = p;

            node->next = dir->children;
            dir->children = node;

            *out = node;
            return 0;
        }
    }

    return -1;
}

static int devfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count) {
    if (!dir || dir->type != VFS_DIR || !entries || !count) return -1;

    u32 max = *count;
    u32 n = 0;

    udisk_scan();

    for (int i = 0; i < 4 && n < max; i++) {
        u8 drive = 0x80 + i;
        u64 sectors = disk_get_sectors(drive);
        if (sectors == 0) continue;

        char name[8];
        name[0] = 's';
        name[1] = 'd';
        name[2] = 'a' + i;
        name[3] = '\0';

        strncpy(entries[n].name, name, VFS_MAX_NAME - 1);
        entries[n].name[VFS_MAX_NAME - 1] = '\0';
        entries[n].type = VFS_BLOCK_DEVICE;
        entries[n].size = sectors * 512;
        n++;

        disk_info_t* d = udisk_get_info(i);
        if (d) {
            for (int j = 0; j < d->partition_count && n < max; j++) {
                partition_t* p = &d->partitions[j];
                if (!p->present) continue;

                char pname[16];
                snprintf(pname, sizeof(pname), "sd%c%d", 'a' + i, p->partition_num);

                strncpy(entries[n].name, pname, VFS_MAX_NAME - 1);
                entries[n].name[VFS_MAX_NAME - 1] = '\0';
                entries[n].type = VFS_BLOCK_DEVICE;
                entries[n].size = p->size;
                n++;
            }
        }
    }

    *count = n;
    return 0;
}

static int devfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir) {
    if (!node) return -1;

    if (size) *size = node->size;
    if (mode) *mode = node->mode;
    if (is_dir) *is_dir = (node->type == VFS_DIR) ? 1 : 0;

    return 0;
}

static int devfs_mount(vfs_node_t** root, const char* dev) {
    (void)dev;
    if (!root) return -1;

    vfs_node_t* n = vfs_create_node("", VFS_DIR);
    if (!n) return -1;

    n->mode = 0755;
    n->fs = &devfs_ops;
    *root = n;

    return 0;
}

static int devfs_unmount(vfs_node_t* root) {
    if (!root) return -1;

    vfs_node_t* c = root->children;
    while (c) {
        vfs_node_t* next = c->next;
        if (c->private) kfree(c->private);
        kfree(c);
        c = next;
    }
    kfree(root);

    return 0;
}

static vfs_fs_ops_t devfs_ops = {
    .name = "devfs",
    .mount = devfs_mount,
    .unmount = devfs_unmount,
    .lookup = devfs_lookup,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .read = devfs_block_read,
    .write = devfs_block_write,
    .readdir = devfs_readdir,
    .stat = devfs_stat,
    .rename = NULL,
    .format = NULL
};

int devfs_register(void) {
    return vfs_register_fs(&devfs_ops);
}
