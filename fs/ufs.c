#include "../kernel/vfs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../include/udisk.h"
#include "../include/udisk.h"
#include "ufs.h"

extern u32 system_ticks;

typedef struct { u32 start; u32 len; } extent_t;

typedef struct {
    u32 magic;
    u32 version;
    u32 total_blocks;
    u32 free_blocks;
    u32 inode_count;
    u32 free_inodes;
    u32 bitmap_start;
    u32 inode_table_start;
    u32 data_start;
    u32 root_inode;
    u32 pad[54];
    u32 checksum;
} __attribute__((packed)) ufs_superblock_t;

typedef struct {
    u16 mode;
    u16 uid;
    u16 gid;
    u16 nlink;
    u32 size;
    u32 blocks;
    u32 atime;
    u32 mtime;
    u32 ctime;
    extent_t extents[8];
    u32 pad[10];
    u32 checksum;
} __attribute__((packed)) ufs_inode_t;

typedef struct {
    u32 inode;
    u8 type;
    u8 name_len;
    u16 pad;
    char name[56];
} __attribute__((packed)) ufs_dirent_t;

typedef struct {
    ufs_superblock_t sb;
    int mounted;
    u32 part_start;
    int current_disk;
    u8 blk_buf[UFS_BLOCK_SIZE] __attribute__((aligned(16)));
} ufs_mount_t;

static vfs_fs_ops_t ufs_ops;

static int ufs_read_block(ufs_mount_t* mnt, u32 b, u8* buf) {
    disk_set_disk(mnt->current_disk);
    u32 lba = mnt->part_start + b * 8;
    for (int i = 0; i < 8; i++) {
        if (disk_read(lba + i, buf + i * 512) != 0) return -1;
    }
    return 0;
}

static int ufs_write_block(ufs_mount_t* mnt, u32 b, u8* buf) {
    disk_set_disk(mnt->current_disk);
    u32 lba = mnt->part_start + b * 8;
    for (int i = 0; i < 8; i++) {
        if (disk_write(lba + i, buf + i * 512) != 0) return -1;
    }
    return 0;
}

static int ufs_read_inode(ufs_mount_t* mnt, u32 ino, ufs_inode_t* out) {
    if (ino == 0 || ino > mnt->sb.inode_count) return -1;
    u32 inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    u32 block = mnt->sb.inode_table_start + (ino - 1) / inodes_per_block;
    u32 off = ((ino - 1) % inodes_per_block) * sizeof(ufs_inode_t);
    if (ufs_read_block(mnt, block, mnt->blk_buf) != 0) return -1;
    memcpy(out, mnt->blk_buf + off, sizeof(ufs_inode_t));
    return 0;
}

static int ufs_write_inode(ufs_mount_t* mnt, u32 ino, ufs_inode_t* in) {
    if (ino == 0 || ino > mnt->sb.inode_count) return -1;
    u32 inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    u32 block = mnt->sb.inode_table_start + (ino - 1) / inodes_per_block;
    u32 off = ((ino - 1) % inodes_per_block) * sizeof(ufs_inode_t);
    if (ufs_read_block(mnt, block, mnt->blk_buf) != 0) return -1;
    memcpy(mnt->blk_buf + off, in, sizeof(ufs_inode_t));
    return ufs_write_block(mnt, block, mnt->blk_buf);
}

static int ufs_bitmap_get(ufs_mount_t* mnt, u32 b) {
    if (b >= mnt->sb.total_blocks) return -1;
    u32 block = mnt->sb.bitmap_start + b / 32768;
    u32 off = (b % 32768) / 8;
    u32 bit = b % 8;
    if (ufs_read_block(mnt, block, mnt->blk_buf) != 0) return -1;
    return (mnt->blk_buf[off] >> bit) & 1;
}

static int ufs_bitmap_set(ufs_mount_t* mnt, u32 b, int val) {
    if (b >= mnt->sb.total_blocks) return -1;
    u32 block = mnt->sb.bitmap_start + b / 32768;
    u32 off = (b % 32768) / 8;
    u32 bit = b % 8;
    if (ufs_read_block(mnt, block, mnt->blk_buf) != 0) return -1;
    if (val) mnt->blk_buf[off] |= (1 << bit);
    else mnt->blk_buf[off] &= ~(1 << bit);
    return ufs_write_block(mnt, block, mnt->blk_buf);
}

static u32 ufs_alloc_block(ufs_mount_t* mnt) {
    for (u32 i = mnt->sb.data_start; i < mnt->sb.total_blocks; i++) {
        if (ufs_bitmap_get(mnt, i) == 0) {
            ufs_bitmap_set(mnt, i, 1);
            mnt->sb.free_blocks--;
            memset(mnt->blk_buf, 0, UFS_BLOCK_SIZE);
            ufs_write_block(mnt, i, mnt->blk_buf);
            return i;
        }
    }
    return 0;
}

static void ufs_free_block(ufs_mount_t* mnt, u32 b) {
    if (b == 0 || b >= mnt->sb.total_blocks) return;
    ufs_bitmap_set(mnt, b, 0);
    mnt->sb.free_blocks++;
}

static u32 ufs_alloc_inode(ufs_mount_t* mnt) {
    u32 inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    u32 total_blocks = (mnt->sb.inode_count + inodes_per_block - 1) / inodes_per_block;
    for (u32 i = 0; i < total_blocks; i++) {
        if (ufs_read_block(mnt, mnt->sb.inode_table_start + i, mnt->blk_buf) != 0) continue;
        ufs_inode_t* inodes = (ufs_inode_t*)mnt->blk_buf;
        for (u32 j = 0; j < inodes_per_block; j++) {
            if (inodes[j].mode == 0) {
                u32 ino = i * inodes_per_block + j + 1;
                if (ino > mnt->sb.inode_count) break;
                memset(&inodes[j], 0, sizeof(ufs_inode_t));
                ufs_write_block(mnt, mnt->sb.inode_table_start + i, mnt->blk_buf);
                mnt->sb.free_inodes--;
                return ino;
            }
        }
    }
    return 0;
}

static void ufs_free_inode(ufs_mount_t* mnt, u32 ino) {
    if (ino == 0 || ino > mnt->sb.inode_count) return;
    ufs_inode_t in;
    if (ufs_read_inode(mnt, ino, &in) != 0) return;
    memset(&in, 0, sizeof(in));
    ufs_write_inode(mnt, ino, &in);
    mnt->sb.free_inodes++;
}

static u32 ufs_get_block(ufs_inode_t* in, u32 idx) {
    for (int i = 0; i < 8; i++) {
        if (in->extents[i].len == 0) break;
        if (idx < in->extents[i].len) return in->extents[i].start + idx;
        idx -= in->extents[i].len;
    }
    return 0;
}

static int ufs_append_block(ufs_inode_t* in, u32 b) {
    for (int i = 0; i < 8; i++) {
        if (in->extents[i].len == 0) {
            in->extents[i].start = b;
            in->extents[i].len = 1;
            return 0;
        }
        if (in->extents[i].start + in->extents[i].len == b) {
            in->extents[i].len++;
            return 0;
        }
    }
    return -1;
}

static void ufs_free_inode_blocks(ufs_mount_t* mnt, u32 ino) {
    ufs_inode_t in;
    if (ufs_read_inode(mnt, ino, &in) != 0) return;
    for (int i = 0; i < 8; i++) {
        for (u32 j = 0; j < in.extents[i].len; j++) {
            ufs_free_block(mnt, in.extents[i].start + j);
        }
    }
}

static int ufs_find_in_dir(ufs_mount_t* mnt, u32 dir_ino, const char* name, u32* out_ino) {
    ufs_inode_t dir;
    if (ufs_read_inode(mnt, dir_ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = ufs_get_block(&dir, i);
        if (!b) continue;
        if (ufs_read_block(mnt, b, mnt->blk_buf) != 0) continue;
        ufs_dirent_t* ents = (ufs_dirent_t*)mnt->blk_buf;
        for (int j = 0; j < UFS_BLOCK_SIZE / sizeof(ufs_dirent_t); j++) {
            if (ents[j].inode != 0 && strcmp(ents[j].name, name) == 0) {
                if (out_ino) *out_ino = ents[j].inode;
                return 0;
            }
        }
    }
    return -1;
}

static int ufs_add_to_dir(ufs_mount_t* mnt, u32 dir_ino, u32 ino, const char* name, u8 type) {
    ufs_inode_t dir;
    if (ufs_read_inode(mnt, dir_ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = ufs_get_block(&dir, i);
        if (!b) continue;
        if (ufs_read_block(mnt, b, mnt->blk_buf) != 0) continue;
        ufs_dirent_t* ents = (ufs_dirent_t*)mnt->blk_buf;
        for (int j = 0; j < UFS_BLOCK_SIZE / sizeof(ufs_dirent_t); j++) {
            if (ents[j].inode == 0) {
                ents[j].inode = ino;
                ents[j].type = type;
                strncpy(ents[j].name, name, 55);
                ents[j].name[55] = '\0';
                ents[j].name_len = strlen(ents[j].name);
                ufs_write_block(mnt, b, mnt->blk_buf);
                dir.size += sizeof(ufs_dirent_t);
                ufs_write_inode(mnt, dir_ino, &dir);
                return 0;
            }
        }
    }
    u32 nb = ufs_alloc_block(mnt);
    if (!nb) return -1;
    memset(mnt->blk_buf, 0, UFS_BLOCK_SIZE);
    ufs_dirent_t* ents = (ufs_dirent_t*)mnt->blk_buf;
    ents[0].inode = ino;
    ents[0].type = type;
    strncpy(ents[0].name, name, 55);
    ents[0].name[55] = '\0';
    ents[0].name_len = strlen(ents[0].name);
    if (ufs_write_block(mnt, nb, mnt->blk_buf) != 0 || ufs_append_block(&dir, nb) != 0) {
        ufs_free_block(mnt, nb);
        return -1;
    }
    dir.size += sizeof(ufs_dirent_t);
    dir.blocks = (dir.size + 511) / 512;
    ufs_write_inode(mnt, dir_ino, &dir);
    return 0;
}

static int ufs_remove_from_dir(ufs_mount_t* mnt, u32 dir_ino, const char* name) {
    ufs_inode_t dir;
    if (ufs_read_inode(mnt, dir_ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = ufs_get_block(&dir, i);
        if (!b) continue;
        if (ufs_read_block(mnt, b, mnt->blk_buf) != 0) continue;
        ufs_dirent_t* ents = (ufs_dirent_t*)mnt->blk_buf;
        for (int j = 0; j < UFS_BLOCK_SIZE / sizeof(ufs_dirent_t); j++) {
            if (ents[j].inode != 0 && strcmp(ents[j].name, name) == 0) {
                ents[j].inode = 0;
                ents[j].name[0] = '\0';
                ufs_write_block(mnt, b, mnt->blk_buf);
                return 0;
            }
        }
    }
    return -1;
}

static int ufs_vfs_lookup(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    ufs_mount_t* mnt = (ufs_mount_t*)dir->private;
    if (!mnt || !mnt->mounted) return -1;

    u32 dir_ino = (dir->type == VFS_DIR) ? (u32)(u64)dir->fs_data : mnt->sb.root_inode;
    u32 ino;
    if (ufs_find_in_dir(mnt, dir_ino, name, &ino) != 0) return -1;

    ufs_inode_t inode;
    if (ufs_read_inode(mnt, ino, &inode) != 0) return -1;

    vfs_node_t* node = vfs_create_node(name, (inode.mode & UFS_INODE_DIR) ? VFS_DIR : VFS_FILE);
    if (!node) return -1;

    node->mode = inode.mode;
    node->size = inode.size;
    node->private = mnt;
    node->fs_data = (void*)(u64)ino;
    node->fs = &ufs_ops;

    *out = node;
    return 0;
}

static int ufs_vfs_create(vfs_node_t* dir, const char* name, u32 mode) {
    ufs_mount_t* mnt = (ufs_mount_t*)dir->private;
    if (!mnt || !mnt->mounted) return -1;

    u32 dir_ino = (u32)(u64)dir->fs_data;
    if (ufs_find_in_dir(mnt, dir_ino, name, NULL) == 0) return -1;

    u32 ino = ufs_alloc_inode(mnt);
    if (!ino) return -1;

    ufs_inode_t inode = {0};
    inode.mode = UFS_INODE_FILE | (mode & 0777);
    inode.nlink = 1;
    inode.atime = inode.mtime = inode.ctime = system_ticks;

    ufs_write_inode(mnt, ino, &inode);
    return ufs_add_to_dir(mnt, dir_ino, ino, name, 1);
}

static int ufs_vfs_mkdir(vfs_node_t* dir, const char* name, u32 mode) {
    ufs_mount_t* mnt = (ufs_mount_t*)dir->private;
    if (!mnt || !mnt->mounted) return -1;

    u32 dir_ino = (u32)(u64)dir->fs_data;
    if (ufs_find_in_dir(mnt, dir_ino, name, NULL) == 0) return -1;

    u32 ino = ufs_alloc_inode(mnt);
    if (!ino) return -1;

    ufs_inode_t inode = {0};
    inode.mode = UFS_INODE_DIR | (mode & 0777);
    inode.nlink = 2;
    inode.atime = inode.mtime = inode.ctime = system_ticks;

    u32 b = ufs_alloc_block(mnt);
    if (!b) { ufs_free_inode(mnt, ino); return -1; }

    ufs_append_block(&inode, b);
    inode.size = 2 * sizeof(ufs_dirent_t);
    inode.blocks = (inode.size + 511) / 512;

    memset(mnt->blk_buf, 0, UFS_BLOCK_SIZE);
    ufs_dirent_t* ents = (ufs_dirent_t*)mnt->blk_buf;
    ents[0].inode = ino; ents[0].type = 2; strcpy(ents[0].name, "."); ents[0].name_len = 1;
    ents[1].inode = dir_ino; ents[1].type = 2; strcpy(ents[1].name, ".."); ents[1].name_len = 2;

    ufs_write_block(mnt, b, mnt->blk_buf);
    ufs_write_inode(mnt, ino, &inode);

    return ufs_add_to_dir(mnt, dir_ino, ino, name, 2);
}

static int ufs_vfs_unlink(vfs_node_t* dir, const char* name) {
    ufs_mount_t* mnt = (ufs_mount_t*)dir->private;
    if (!mnt || !mnt->mounted) return -1;

    u32 dir_ino = (u32)(u64)dir->fs_data;
    u32 ino;
    if (ufs_find_in_dir(mnt, dir_ino, name, &ino) != 0) return -1;

    ufs_inode_t inode;
    if (ufs_read_inode(mnt, ino, &inode) != 0) return -1;

    if (inode.mode & UFS_INODE_DIR) {
        if (inode.size > 2 * sizeof(ufs_dirent_t)) return -1;
    }

    ufs_free_inode_blocks(mnt, ino);
    ufs_free_inode(mnt, ino);
    return ufs_remove_from_dir(mnt, dir_ino, name);
}

static int ufs_vfs_read(vfs_node_t* node, void* buf, u64 size, u64 offset) {
    ufs_mount_t* mnt = (ufs_mount_t*)node->private;
    if (!mnt || !mnt->mounted || node->type != VFS_FILE) return -1;

    u32 ino = (u32)(u64)node->fs_data;
    ufs_inode_t inode;
    if (ufs_read_inode(mnt, ino, &inode) != 0) return -1;

    if (offset >= inode.size) return 0;

    u64 to_copy = inode.size - offset;
    if (size < to_copy) to_copy = size;

    u32 start_block = offset / UFS_BLOCK_SIZE;
    u32 start_off = offset % UFS_BLOCK_SIZE;
    u64 copied = 0;

    while (copied < to_copy) {
        u32 b = ufs_get_block(&inode, start_block);
        if (!b) break;

        if (ufs_read_block(mnt, b, mnt->blk_buf) != 0) break;

        u64 chunk = UFS_BLOCK_SIZE - start_off;
        if (chunk > to_copy - copied) chunk = to_copy - copied;

        memcpy((u8*)buf + copied, mnt->blk_buf + start_off, chunk);

        copied += chunk;
        start_block++;
        start_off = 0;
    }

    return (int)copied;
}

static int ufs_vfs_write(vfs_node_t* node, const void* buf, u64 size, u64 offset) {
    ufs_mount_t* mnt = (ufs_mount_t*)node->private;
    if (!mnt || !mnt->mounted || node->type != VFS_FILE) return -1;

    u32 ino = (u32)(u64)node->fs_data;
    ufs_inode_t inode;
    if (ufs_read_inode(mnt, ino, &inode) != 0) return -1;

    u32 start_block = offset / UFS_BLOCK_SIZE;
    u32 start_off = offset % UFS_BLOCK_SIZE;
    u64 written = 0;

    while (written < size) {
        u32 b = ufs_get_block(&inode, start_block);
        if (!b) {
            b = ufs_alloc_block(mnt);
            if (!b) break;
            if (ufs_append_block(&inode, b) != 0) {
                ufs_free_block(mnt, b);
                break;
            }
        }

        if (ufs_read_block(mnt, b, mnt->blk_buf) != 0) break;

        u64 chunk = UFS_BLOCK_SIZE - start_off;
        if (chunk > size - written) chunk = size - written;

        memcpy(mnt->blk_buf + start_off, (const u8*)buf + written, chunk);

        if (ufs_write_block(mnt, b, mnt->blk_buf) != 0) break;

        written += chunk;
        start_block++;
        start_off = 0;
    }

    if (offset + written > inode.size) {
        inode.size = offset + written;
        inode.blocks = (inode.size + 511) / 512;
        inode.mtime = system_ticks;
        ufs_write_inode(mnt, ino, &inode);
        node->size = inode.size;
    }

    return (int)written;
}

static int ufs_vfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count) {
    ufs_mount_t* mnt = (ufs_mount_t*)dir->private;
    if (!mnt || !mnt->mounted || dir->type != VFS_DIR) return -1;

    u32 dir_ino = (u32)(u64)dir->fs_data;
    ufs_inode_t inode;
    if (ufs_read_inode(mnt, dir_ino, &inode) != 0) return -1;

    u32 blocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    u32 n = 0;
    u32 max = *count;

    for (u32 i = 0; i < blocks && n < max; i++) {
        u32 b = ufs_get_block(&inode, i);
        if (!b) continue;
        if (ufs_read_block(mnt, b, mnt->blk_buf) != 0) continue;

        ufs_dirent_t* ents = (ufs_dirent_t*)mnt->blk_buf;
        for (int j = 0; j < UFS_BLOCK_SIZE / sizeof(ufs_dirent_t) && n < max; j++) {
            if (ents[j].inode != 0) {
                strncpy(entries[n].name, ents[j].name, VFS_MAX_NAME - 1);
                entries[n].name[VFS_MAX_NAME - 1] = '\0';

                ufs_inode_t child;
                if (ufs_read_inode(mnt, ents[j].inode, &child) == 0) {
                    entries[n].type = (child.mode & UFS_INODE_DIR) ? VFS_DIR : VFS_FILE;
                    entries[n].size = child.size;
                } else {
                    entries[n].type = VFS_FILE;
                    entries[n].size = 0;
                }
                n++;
            }
        }
    }

    *count = n;
    return 0;
}

static int ufs_vfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir) {
    ufs_mount_t* mnt = (ufs_mount_t*)node->private;
    if (!mnt || !mnt->mounted) return -1;

    u32 ino = (u32)(u64)node->fs_data;
    ufs_inode_t inode;
    if (ufs_read_inode(mnt, ino, &inode) != 0) return -1;

    if (size) *size = inode.size;
    if (mode) *mode = inode.mode;
    if (is_dir) *is_dir = (inode.mode & UFS_INODE_DIR) ? 1 : 0;

    return 0;
}

static int ufs_vfs_mount(vfs_node_t** root, const char* dev) {
    int disk = 0, part = 0;
    if (parse_devname(dev, &disk, &part) != 0) return -1;

    partition_t* p = udisk_get_partition(dev);
    if (!p) return -1;

    ufs_mount_t* mnt = kmalloc(sizeof(ufs_mount_t));
    if (!mnt) return -1;

    memset(mnt, 0, sizeof(ufs_mount_t));
    mnt->part_start = p->start_lba;
    mnt->current_disk = p->disk_num;

    if (ufs_read_block(mnt, 0, mnt->blk_buf) != 0) {
        kfree(mnt);
        return -1;
    }

    memcpy(&mnt->sb, mnt->blk_buf, sizeof(ufs_superblock_t));
    if (mnt->sb.magic != UFS_MAGIC) {
        kfree(mnt);
        return -1;
    }

    mnt->mounted = 1;

    vfs_node_t* root_node = vfs_create_node("", VFS_DIR);
    if (!root_node) {
        kfree(mnt);
        return -1;
    }

    root_node->mode = 0755;
    root_node->private = mnt;
    root_node->fs_data = (void*)(u64)mnt->sb.root_inode;
    root_node->fs = &ufs_ops;

    *root = root_node;
    return 0;
}

static int ufs_vfs_unmount(vfs_node_t* root) {
    if (!root || !root->private) return -1;
    ufs_mount_t* mnt = (ufs_mount_t*)root->private;
    mnt->mounted = 0;
    kfree(mnt);
    return 0;
}

static int ufs_vfs_format(const char* dev) {
    int disk = 0;
    int part = 0;

    if (parse_devname(dev, &disk, &part) != 0 || part == 0) return -1;

    partition_t* p = udisk_get_partition(dev);
    if (!p) return -1;

    u64 sector_count = p->end_lba - p->start_lba + 1;
    if (sector_count < 8) return -1;
    if (sector_count % 8 != 0) {
        sector_count = (sector_count / 8) * 8;
    }

    u32 blocks = (u32)(sector_count / 8);
    return ufs_format((u32)p->start_lba, blocks, p->disk_num);
}

static vfs_fs_ops_t ufs_ops = {
    .name = "ufs",
    .mount = ufs_vfs_mount,
    .unmount = ufs_vfs_unmount,
    .lookup = ufs_vfs_lookup,
    .create = ufs_vfs_create,
    .mkdir = ufs_vfs_mkdir,
    .unlink = ufs_vfs_unlink,
    .symlink = NULL,
    .readlink = NULL,
    .read = ufs_vfs_read,
    .write = ufs_vfs_write,
    .readdir = ufs_vfs_readdir,
    .stat = ufs_vfs_stat,
    .rename = NULL,
    .format = ufs_vfs_format
};

int ufs_format(u32 start_lba, u32 total_blocks, int disk) {
    ufs_mount_t mnt;
    memset(&mnt, 0, sizeof(mnt));
    mnt.part_start = start_lba;
    mnt.current_disk = disk;

    u32 bitmap_blocks = (total_blocks + 32767) / 32768;
    u32 inode_count = 1024;
    u32 inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    u32 inode_blocks = (inode_count + inodes_per_block - 1) / inodes_per_block;

    mnt.sb.magic = UFS_MAGIC;
    mnt.sb.version = UFS_VERSION;
    mnt.sb.total_blocks = total_blocks;
    mnt.sb.inode_count = inode_count;
    mnt.sb.bitmap_start = 1;
    mnt.sb.inode_table_start = mnt.sb.bitmap_start + bitmap_blocks;
    mnt.sb.data_start = mnt.sb.inode_table_start + inode_blocks;
    mnt.sb.root_inode = 1;
    mnt.sb.free_blocks = total_blocks - mnt.sb.data_start;
    mnt.sb.free_inodes = inode_count;

    memset(mnt.blk_buf, 0, UFS_BLOCK_SIZE);
    disk_set_disk(disk);
    for (u32 i = 0; i < mnt.sb.data_start; i++) {
        ufs_write_block(&mnt, i, mnt.blk_buf);
    }

    for (u32 i = 0; i < mnt.sb.data_start; i++) ufs_bitmap_set(&mnt, i, 1);

    u32 root_block = ufs_alloc_block(&mnt);
    if (!root_block) return -1;

    ufs_inode_t root = {0};
    root.mode = UFS_INODE_DIR | 0755;
    root.nlink = 2;
    root.atime = root.mtime = root.ctime = system_ticks;
    root.size = 2 * sizeof(ufs_dirent_t);
    root.blocks = (root.size + 511) / 512;
    root.extents[0].start = root_block;
    root.extents[0].len = 1;
    ufs_write_inode(&mnt, 1, &root);

    memset(mnt.blk_buf, 0, UFS_BLOCK_SIZE);
    ufs_dirent_t* root_ents = (ufs_dirent_t*)mnt.blk_buf;
    root_ents[0].inode = 1;
    root_ents[0].type = 2;
    root_ents[0].name_len = 1;
    root_ents[0].name[0] = '.';
    root_ents[0].name[1] = '\0';

    root_ents[1].inode = 1;
    root_ents[1].type = 2;
    root_ents[1].name_len = 2;
    root_ents[1].name[0] = '.';
    root_ents[1].name[1] = '.';
    root_ents[1].name[2] = '\0';

    if (ufs_write_block(&mnt, root_block, mnt.blk_buf) != 0) return -1;

    mnt.sb.free_inodes = inode_count - 1;

    memset(mnt.blk_buf, 0, UFS_BLOCK_SIZE);
    memcpy(mnt.blk_buf, &mnt.sb, sizeof(mnt.sb));
    ufs_write_block(&mnt, 0, mnt.blk_buf);

    return 0;
}

int ufs_register(void) {
    return vfs_register_fs(&ufs_ops);
}
//TODO блять нахуй блять пизда
/*1. Добавить синхронизацию - блокировки для защиты от гонок при многозадачности
2. Реализовать запись суперблока - сейчас изменения free_blocks/free_inodes не сохраняются на диск
3. Добавить журналирование - для восстановления после сбоев питания
4. Реализовать проверку контрольных сумм - они объявлены, но не используются
5. Добавить восстановление после сбоев - проверка целостности при монтировании
6. Реализовать rename - переименование файлов/директорий
7. Реализовать symlink - символические ссылки
8. Реализовать readlink - чтение символических ссылок
9. Добавить поддержку жёстких ссылок - несколько имён для одного inode
10. Добавить chmod/chown - изменение прав доступа
11. Добавить проверку прав доступа - при всех операциях
12. Добавить поддержку нескольких пользователей - UID/GID уже есть, но не используются
13. Добавить кэш inode'ов - избежать постоянного чтения с диска
14. Добавить кэш блоков - для ускорения операций чтения/записи
15. Оптимизировать поиск свободных блоков - хранить указатель последнего выделенного блока
16. Добавить отложенную запись - накапливать изменения и записывать пакетами
17. Добавить дефрагментацию - объединение смежных экстентов
18. Добавить детальные коды ошибок - разные ошибки для разных ситуаций
19. Добавить проверку на переполнение - максимальный размер файла, количество файлов
20. Добавить обработку повреждённых данных - битые блоки, неверные указатели
21. Добавить резервные копии суперблока - на случай повреждения основного
22. Добавить проверку целостности при записи - верификация после записи
23. Добавить поддержку ACL - расширенные списки контроля доступа
24. Добавить квоты - ограничения на использование диска пользователями
25. Добавить сжатие - прозрачное сжатие данных
26. Добавить шифрование - шифрование на уровне файловой системы
27. Добавить снапшоты - моментальные снимки состояния FS
28. Добавить дедупликацию - устранение дублирующихся данных */
