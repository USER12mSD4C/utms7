#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/namei.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/timekeeping.h>

#define UFS_MAGIC 0x55544D53
#define UFS_BLOCK_SIZE 4096

typedef struct { u32 start; u32 len; } extent_t;

typedef struct {
    u32 magic; u32 version; u32 total_blocks; u32 free_blocks;
    u32 inode_count; u32 free_inodes; u32 bitmap_start;
    u32 inode_table_start; u32 data_start; u32 root_inode;
    u32 pad[54]; u32 checksum;
} __attribute__((packed)) ufs_superblock_t;

typedef struct {
    u16 mode; u16 uid; u16 gid; u16 nlink;
    u32 size; u32 blocks; u32 atime; u32 mtime; u32 ctime;
    extent_t extents[8]; u32 pad[10]; u32 checksum;
} __attribute__((packed)) ufs_inode_t;

typedef struct {
    u32 inode; u8 type; u8 name_len; u16 pad; char name[56];
} __attribute__((packed)) ufs_dirent_t;

struct ufs_sb_info {
    ufs_superblock_t sb;
    u32 inodes_per_block;
    u32 dirents_per_block;
    struct buffer_head *sb_bh;
    struct mutex s_lock;
};

struct ufs_inode_info {
    ufs_inode_t disk_inode;
    u32 ino;
    struct inode vfs_inode;
};

static inline struct ufs_inode_info *UFS_I(struct inode *inode) {
    return container_of(inode, struct ufs_inode_info, vfs_inode);
}

static const struct address_space_operations ufs_aops;
static const struct inode_operations ufs_dir_inode_operations;
static const struct file_operations ufs_dir_operations;

static u32 ufs_calc_sb_checksum(ufs_superblock_t *sb) {
    u32 *ptr = (u32 *)sb;
    u32 sum = 0;
    u32 old = sb->checksum;
    sb->checksum = 0;
    for (u32 i = 0; i < sizeof(ufs_superblock_t) / 4; i++) {
        sum ^= ptr[i];
    }
    sb->checksum = old;
    return sum;
}

static u32 ufs_calc_inode_checksum(ufs_inode_t *in) {
    u32 *ptr = (u32 *)in;
    u32 sum = 0;
    u32 old = in->checksum;
    in->checksum = 0;
    for (u32 i = 0; i < sizeof(ufs_inode_t) / 4; i++) {
        sum ^= ptr[i];
    }
    in->checksum = old;
    return sum;
}

static void ufs_write_superblock(struct super_block *sb) {
    struct ufs_sb_info *sbi = sb->s_fs_info;
    sbi->sb.checksum = ufs_calc_sb_checksum(&sbi->sb);
    lock_buffer(sbi->sb_bh);
    memcpy(sbi->sb_bh->b_data, &sbi->sb, sizeof(ufs_superblock_t));
    set_buffer_uptodate(sbi->sb_bh);
    mark_buffer_dirty(sbi->sb_bh);
    unlock_buffer(sbi->sb_bh);
    sync_dirty_buffer(sbi->sb_bh);
}

static u32 ufs_alloc_block(struct super_block *sb) {
    struct ufs_sb_info *sbi = sb->s_fs_info;
    u32 b = 0;
    mutex_lock(&sbi->s_lock);
    for (u32 i = sbi->sb.data_start; i < sbi->sb.total_blocks; i++) {
        u32 block = sbi->sb.bitmap_start + i / (UFS_BLOCK_SIZE * 8);
        u32 off = (i % (UFS_BLOCK_SIZE * 8)) / 8;
        u32 bit = i % 8;
        struct buffer_head *bh = sb_bread(sb, block);
        if (!bh) continue;
        if (!(bh->b_data[off] & (1 << bit))) {
            bh->b_data[off] |= (1 << bit);
            mark_buffer_dirty(bh);
            sync_dirty_buffer(bh);
            brelse(bh);
            b = i;
            break;
        }
        brelse(bh);
    }
    if (b) {
        sbi->sb.free_blocks--;
        ufs_write_superblock(sb);
    }
    mutex_unlock(&sbi->s_lock);
    return b;
}

static void ufs_free_block(struct super_block *sb, u32 b) {
    struct ufs_sb_info *sbi = sb->s_fs_info;
    mutex_lock(&sbi->s_lock);
    u32 block = sbi->sb.bitmap_start + b / (UFS_BLOCK_SIZE * 8);
    u32 off = (b % (UFS_BLOCK_SIZE * 8)) / 8;
    u32 bit = b % 8;
    struct buffer_head *bh = sb_bread(sb, block);
    if (bh) {
        bh->b_data[off] &= ~(1 << bit);
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
        brelse(bh);
    }
    sbi->sb.free_blocks++;
    ufs_write_superblock(sb);
    mutex_unlock(&sbi->s_lock);
}

static struct inode *ufs_alloc_inode(struct super_block *sb) {
    struct ufs_inode_info *ui = kzalloc(sizeof(*ui), GFP_KERNEL);
    if (!ui) return NULL;
    inode_init_once(&ui->vfs_inode);
    return &ui->vfs_inode;
}

static void ufs_free_inode(struct inode *inode) {
    kfree(UFS_I(inode));
}

static int ufs_write_inode(struct inode *inode, struct writeback_control *wbc) {
    struct super_block *sb = inode->i_sb;
    struct ufs_sb_info *sbi = sb->s_fs_info;
    struct ufs_inode_info *ui = UFS_I(inode);
    struct timespec64 ts;

    ui->disk_inode.mode = inode->i_mode;
    ui->disk_inode.uid = i_uid_read(inode);
    ui->disk_inode.gid = i_gid_read(inode);
    ui->disk_inode.nlink = inode->i_nlink;
    ui->disk_inode.size = inode->i_size;

    ts = inode_get_atime(inode); ui->disk_inode.atime = ts.tv_sec;
    ts = inode_get_mtime(inode); ui->disk_inode.mtime = ts.tv_sec;
    ts = inode_get_ctime(inode); ui->disk_inode.ctime = ts.tv_sec;

    ui->disk_inode.checksum = ufs_calc_inode_checksum(&ui->disk_inode);

    u32 block = sbi->sb.inode_table_start + (ui->ino - 1) / sbi->inodes_per_block;
    u32 off = ((ui->ino - 1) % sbi->inodes_per_block) * sizeof(ufs_inode_t);
    struct buffer_head *bh = sb_bread(sb, block);
    if (!bh) return -EIO;
    memcpy(bh->b_data + off, &ui->disk_inode, sizeof(ufs_inode_t));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    return 0;
}

static void ufs_evict_inode(struct inode *inode) {
    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);
    if (!inode->i_nlink) {
        struct ufs_inode_info *ui = UFS_I(inode);
        for (int i = 0; i < 8; i++) {
            for (u32 j = 0; j < ui->disk_inode.extents[i].len; j++) {
                ufs_free_block(inode->i_sb, ui->disk_inode.extents[i].start + j);
            }
        }
        ufs_free_block(inode->i_sb, ui->ino);
    }
}

static void ufs_put_super(struct super_block *sb) {
    struct ufs_sb_info *sbi = sb->s_fs_info;
    if (sbi->sb_bh) brelse(sbi->sb_bh);
    kfree(sbi);
}

static int ufs_statfs(struct dentry *dentry, struct kstatfs *buf) {
    struct super_block *sb = dentry->d_sb;
    struct ufs_sb_info *sbi = sb->s_fs_info;
    buf->f_type = UFS_MAGIC;
    buf->f_bsize = UFS_BLOCK_SIZE;
    buf->f_blocks = sbi->sb.total_blocks;
    buf->f_bfree = sbi->sb.free_blocks;
    buf->f_bavail = sbi->sb.free_blocks;
    buf->f_files = sbi->sb.inode_count;
    buf->f_ffree = sbi->sb.free_inodes;
    buf->f_namelen = 55;
    return 0;
}

static const struct super_operations ufs_super_ops = {
    .alloc_inode = ufs_alloc_inode,
    .free_inode = ufs_free_inode,
    .write_inode = ufs_write_inode,
    .evict_inode = ufs_evict_inode,
    .put_super = ufs_put_super,
    .statfs = ufs_statfs,
};

static int ufs_read_inode(struct inode *inode) {
    struct super_block *sb = inode->i_sb;
    struct ufs_sb_info *sbi = sb->s_fs_info;
    struct ufs_inode_info *ui = UFS_I(inode);
    struct timespec64 ts;

    u32 block = sbi->sb.inode_table_start + (ui->ino - 1) / sbi->inodes_per_block;
    u32 off = ((ui->ino - 1) % sbi->inodes_per_block) * sizeof(ufs_inode_t);
    struct buffer_head *bh = sb_bread(sb, block);
    if (!bh) return -EIO;
    memcpy(&ui->disk_inode, bh->b_data + off, sizeof(ufs_inode_t));
    brelse(bh);

    if (ui->disk_inode.checksum != ufs_calc_inode_checksum(&ui->disk_inode)) {
        return -EIO;
    }

    inode->i_mode = ui->disk_inode.mode;
    i_uid_write(inode, ui->disk_inode.uid);
    i_gid_write(inode, ui->disk_inode.gid);
    set_nlink(inode, ui->disk_inode.nlink);
    inode->i_size = ui->disk_inode.size;

    ts.tv_sec = ui->disk_inode.atime; ts.tv_nsec = 0; inode_set_atime_to_ts(inode, ts);
    ts.tv_sec = ui->disk_inode.mtime; ts.tv_nsec = 0; inode_set_mtime_to_ts(inode, ts);
    ts.tv_sec = ui->disk_inode.ctime; ts.tv_nsec = 0; inode_set_ctime_to_ts(inode, ts);
    return 0;
}

static const struct file_operations ufs_file_operations = {
    .llseek = generic_file_llseek,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap = generic_file_mmap,
};

static struct inode *ufs_iget(struct super_block *sb, unsigned long ino) {
    struct inode *inode = iget_locked(sb, ino);
    if (!inode) return ERR_PTR(-ENOMEM);
    struct ufs_inode_info *ui = UFS_I(inode);
    if (ui->ino != 0) {
        unlock_new_inode(inode);
        return inode;
    }
    ui->ino = ino;
    if (ufs_read_inode(inode) != 0) {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &ufs_dir_inode_operations;
        inode->i_fop = &ufs_dir_operations;
    } else {
        inode->i_mapping->a_ops = &ufs_aops;
        inode->i_fop = &ufs_file_operations;
    }
    unlock_new_inode(inode);
    return inode;
}

static int ufs_read_folio(struct file *file, struct folio *folio) {
    struct inode *inode = folio->mapping->host;
    struct ufs_inode_info *ui = UFS_I(inode);
    u32 page_idx = folio->index;
    u32 phys_block = 0;

    for (int i = 0; i < 8; i++) {
        if (ui->disk_inode.extents[i].len == 0) break;
        if (page_idx < ui->disk_inode.extents[i].len) {
            phys_block = ui->disk_inode.extents[i].start + page_idx;
            break;
        }
        page_idx -= ui->disk_inode.extents[i].len;
    }

    char *kaddr = kmap_local_folio(folio, 0);
    if (phys_block == 0) {
        memset(kaddr, 0, UFS_BLOCK_SIZE);
    } else {
        struct buffer_head *bh = sb_bread(inode->i_sb, phys_block);
        if (bh) {
            memcpy(kaddr, bh->b_data, UFS_BLOCK_SIZE);
            brelse(bh);
        } else {
            memset(kaddr, 0, UFS_BLOCK_SIZE);
        }
    }
    kunmap_local(kaddr);
    folio_mark_uptodate(folio);
    folio_unlock(folio);
    return 0;
}

static int ufs_write_begin(const struct kiocb *iocb, struct address_space *mapping,
                           loff_t pos, unsigned len,
                           struct folio **foliop, void **fsdata) {
    struct super_block *sb = mapping->host->i_sb;
    struct ufs_inode_info *ui = UFS_I(mapping->host);
    u32 page_idx = pos >> PAGE_SHIFT;
    u32 phys_block = 0;

    for (int i = 0; i < 8; i++) {
        if (ui->disk_inode.extents[i].len == 0) break;
        if (page_idx < ui->disk_inode.extents[i].len) {
            phys_block = ui->disk_inode.extents[i].start + page_idx;
            break;
        }
        page_idx -= ui->disk_inode.extents[i].len;
    }

    if (phys_block == 0) {
        phys_block = ufs_alloc_block(sb);
        if (phys_block == 0) return -ENOSPC;
        for (int i = 0; i < 8; i++) {
            if (ui->disk_inode.extents[i].len == 0) {
                ui->disk_inode.extents[i].start = phys_block;
                ui->disk_inode.extents[i].len = 1;
                break;
            }
        }
        ufs_write_inode(mapping->host, NULL);
    }

    struct folio *folio = __filemap_get_folio(mapping, pos >> PAGE_SHIFT, FGP_WRITEBEGIN, mapping_gfp_mask(mapping));
    if (IS_ERR(folio)) return PTR_ERR(folio);
    *foliop = folio;

    if (!folio_test_uptodate(folio)) {
        char *kaddr = kmap_local_folio(folio, 0);
        struct buffer_head *bh = sb_bread(sb, phys_block);
        if (bh) {
            memcpy(kaddr, bh->b_data, UFS_BLOCK_SIZE);
            brelse(bh);
        } else {
            memset(kaddr, 0, UFS_BLOCK_SIZE);
        }
        kunmap_local(kaddr);
        folio_mark_uptodate(folio);
    }

    return 0;
}

static int ufs_write_end(const struct kiocb *iocb, struct address_space *mapping,
                         loff_t pos, unsigned len, unsigned copied,
                         struct folio *folio, void *fsdata) {
    struct inode *inode = mapping->host;
    struct super_block *sb = inode->i_sb;
    struct ufs_inode_info *ui = UFS_I(inode);

    u32 phys_block = 0;
    u32 page_idx = pos >> PAGE_SHIFT;
    for (int i = 0; i < 8; i++) {
        if (ui->disk_inode.extents[i].len == 0) break;
        if (page_idx < ui->disk_inode.extents[i].len) {
            phys_block = ui->disk_inode.extents[i].start + page_idx;
            break;
        }
        page_idx -= ui->disk_inode.extents[i].len;
    }

    if (phys_block > 0) {
        char *kaddr = kmap_local_folio(folio, 0);
        struct buffer_head *bh = sb_getblk(sb, phys_block);
        if (bh) {
            memcpy(bh->b_data, kaddr, UFS_BLOCK_SIZE);
            mark_buffer_dirty(bh);
            sync_dirty_buffer(bh);
            brelse(bh);
        }
        kunmap_local(kaddr);
    }

    if (pos + copied > inode->i_size) {
        inode->i_size = pos + copied;
        ufs_write_inode(inode, NULL);
    }

    folio_unlock(folio);
    folio_put(folio);
    return copied;
}

static const struct address_space_operations ufs_aops = {
    .read_folio = ufs_read_folio,
    .write_begin = ufs_write_begin,
    .write_end = ufs_write_end,
};

static int ufs_add_entry(struct inode *dir, const char *name, u32 ino, u8 type) {
    struct super_block *sb = dir->i_sb;
    struct ufs_inode_info *dir_ui = UFS_I(dir);
    ufs_dirent_t new_ent = {0};
    new_ent.inode = ino;
    new_ent.type = type;
    strscpy(new_ent.name, name, 56);
    new_ent.name_len = strlen(new_ent.name);

    u32 total_blocks = (dir->i_size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    for (u32 b = 0; b < total_blocks; b++) {
        u32 block_idx = b;
        u32 phys_block = 0;
        for (int i = 0; i < 8; i++) {
            if (dir_ui->disk_inode.extents[i].len == 0) break;
            if (block_idx < dir_ui->disk_inode.extents[i].len) {
                phys_block = dir_ui->disk_inode.extents[i].start + block_idx;
                break;
            }
            block_idx -= dir_ui->disk_inode.extents[i].len;
        }
        if (phys_block == 0) break;

        struct buffer_head *bh = sb_bread(sb, phys_block);
        if (!bh) continue;

        ufs_dirent_t *ents = (ufs_dirent_t *)bh->b_data;
        int ents_per_block = UFS_BLOCK_SIZE / sizeof(ufs_dirent_t);

        for (int j = 0; j < ents_per_block; j++) {
            if (ents[j].inode == 0) {
                ents[j] = new_ent;
                mark_buffer_dirty(bh);
                sync_dirty_buffer(bh);
                brelse(bh);
                return 0;
            }
        }
        brelse(bh);
    }

    u32 nb = ufs_alloc_block(sb);
    if (nb == 0) return -ENOSPC;

    struct buffer_head *bh = sb_getblk(sb, nb);
    if (!bh) { ufs_free_block(sb, nb); return -EIO; }
    memset(bh->b_data, 0, UFS_BLOCK_SIZE);
    ufs_dirent_t *ents = (ufs_dirent_t *)bh->b_data;
    ents[0] = new_ent;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    for (int i = 0; i < 8; i++) {
        if (dir_ui->disk_inode.extents[i].len == 0) {
            dir_ui->disk_inode.extents[i].start = nb;
            dir_ui->disk_inode.extents[i].len = 1;
            break;
        }
    }
    dir->i_size = (total_blocks + 1) * UFS_BLOCK_SIZE;
    ufs_write_inode(dir, NULL);
    return 0;
}

static struct inode *ufs_new_inode(struct super_block *sb, umode_t mode) {
    struct ufs_sb_info *sbi = sb->s_fs_info;
    mutex_lock(&sbi->s_lock);
    u32 ino = 0;
    u32 inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    for (u32 i = 1; i <= sbi->sb.inode_count; i++) {
        u32 block = sbi->sb.inode_table_start + (i - 1) / inodes_per_block;
        u32 off = ((i - 1) % inodes_per_block) * sizeof(ufs_inode_t);
        struct buffer_head *bh = sb_bread(sb, block);
        if (!bh) continue;
        ufs_inode_t *di = (ufs_inode_t *)(bh->b_data + off);
        if (di->mode == 0) {
            memset(di, 0, sizeof(ufs_inode_t));
            di->mode = mode;
            di->nlink = 1;
            di->atime = di->mtime = di->ctime = (u32)ktime_get_real_seconds();
            di->checksum = ufs_calc_inode_checksum(di);
            mark_buffer_dirty(bh);
            sync_dirty_buffer(bh);
            brelse(bh);
            ino = i;
            sbi->sb.free_inodes--;
            ufs_write_superblock(sb);
            break;
        }
        brelse(bh);
    }
    mutex_unlock(&sbi->s_lock);
    if (ino == 0) return ERR_PTR(-ENOSPC);
    return ufs_iget(sb, ino);
}

static int ufs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct inode *inode = ufs_new_inode(dir->i_sb, mode | S_IFREG);
    if (IS_ERR(inode)) return PTR_ERR(inode);
    int err = ufs_add_entry(dir, dentry->d_name.name, UFS_I(inode)->ino, 1);
    if (err) { iput(inode); return err; }
    d_instantiate(dentry, inode);
    return 0;
}

static struct dentry *ufs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
    struct inode *inode = ufs_new_inode(dir->i_sb, mode | S_IFDIR);
    if (IS_ERR(inode)) return ERR_CAST(inode);

    ufs_add_entry(inode, ".", UFS_I(inode)->ino, 2);
    ufs_add_entry(inode, "..", UFS_I(dir)->ino, 2);

    int err = ufs_add_entry(dir, dentry->d_name.name, UFS_I(inode)->ino, 2);
    if (err) { iput(inode); return ERR_PTR(err); }

    inode->i_size = 2 * sizeof(ufs_dirent_t);
    ufs_write_inode(inode, NULL);

    d_instantiate(dentry, inode);
    inc_nlink(dir);
    return NULL;
}

static int ufs_remove_entry(struct inode *dir, const char *name) {
    struct super_block *sb = dir->i_sb;
    struct ufs_inode_info *dir_ui = UFS_I(dir);
    u32 total_blocks = (dir->i_size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    for (u32 b = 0; b < total_blocks; b++) {
        u32 block_idx = b;
        u32 phys_block = 0;
        for (int i = 0; i < 8; i++) {
            if (dir_ui->disk_inode.extents[i].len == 0) break;
            if (block_idx < dir_ui->disk_inode.extents[i].len) {
                phys_block = dir_ui->disk_inode.extents[i].start + block_idx;
                break;
            }
            block_idx -= dir_ui->disk_inode.extents[i].len;
        }
        if (phys_block == 0) break;

        struct buffer_head *bh = sb_bread(sb, phys_block);
        if (!bh) continue;

        ufs_dirent_t *ents = (ufs_dirent_t *)bh->b_data;
        int ents_per_block = UFS_BLOCK_SIZE / sizeof(ufs_dirent_t);

        for (int j = 0; j < ents_per_block; j++) {
            if (ents[j].inode != 0 && strncmp(ents[j].name, name, 56) == 0) {
                ents[j].inode = 0;
                ents[j].name[0] = '\0';
                mark_buffer_dirty(bh);
                sync_dirty_buffer(bh);
                brelse(bh);
                return 0;
            }
        }
        brelse(bh);
    }
    return -ENOENT;
}

static int ufs_unlink(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = d_inode(dentry);
    int err = ufs_remove_entry(dir, dentry->d_name.name);
    if (err) return err;
    drop_nlink(inode);
    ufs_write_inode(inode, NULL);
    return 0;
}

static int ufs_rmdir(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = d_inode(dentry);
    if (inode->i_size > 2 * sizeof(ufs_dirent_t)) return -ENOTEMPTY;
    int err = ufs_unlink(dir, dentry);
    if (!err) drop_nlink(dir);
    return err;
}

static struct dentry *ufs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    struct super_block *sb = dir->i_sb;
    struct ufs_inode_info *dir_ui = UFS_I(dir);
    u32 total_blocks = (dir->i_size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    for (u32 b = 0; b < total_blocks; b++) {
        u32 block_idx = b;
        u32 phys_block = 0;
        for (int i = 0; i < 8; i++) {
            if (dir_ui->disk_inode.extents[i].len == 0) break;
            if (block_idx < dir_ui->disk_inode.extents[i].len) {
                phys_block = dir_ui->disk_inode.extents[i].start + block_idx;
                break;
            }
            block_idx -= dir_ui->disk_inode.extents[i].len;
        }
        if (phys_block == 0) break;

        struct buffer_head *bh = sb_bread(sb, phys_block);
        if (!bh) continue;

        ufs_dirent_t *ents = (ufs_dirent_t *)bh->b_data;
        int ents_per_block = UFS_BLOCK_SIZE / sizeof(ufs_dirent_t);

        for (int j = 0; j < ents_per_block; j++) {
            if (ents[j].inode != 0 && strncmp(ents[j].name, dentry->d_name.name, 56) == 0) {
                brelse(bh);
                struct inode *inode = ufs_iget(sb, ents[j].inode);
                if (IS_ERR(inode)) return ERR_CAST(inode);
                return d_splice_alias(inode, dentry);
            }
        }
        brelse(bh);
    }
    return NULL;
}

static const struct inode_operations ufs_dir_inode_operations = {
    .lookup = ufs_lookup,
    .create = ufs_create,
    .mkdir = ufs_mkdir,
    .unlink = ufs_unlink,
    .rmdir = ufs_rmdir,
};

static int ufs_iterate(struct file *file, struct dir_context *ctx) {
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct ufs_inode_info *ui = UFS_I(inode);

    if (ctx->pos >= inode->i_size) return 0;
    u32 start_block = ctx->pos / UFS_BLOCK_SIZE;
    u32 total_blocks = (inode->i_size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    for (u32 b = start_block; b < total_blocks; b++) {
        u32 block_idx = b;
        u32 phys_block = 0;
        for (int i = 0; i < 8; i++) {
            if (ui->disk_inode.extents[i].len == 0) break;
            if (block_idx < ui->disk_inode.extents[i].len) {
                phys_block = ui->disk_inode.extents[i].start + block_idx;
                break;
            }
            block_idx -= ui->disk_inode.extents[i].len;
        }
        if (phys_block == 0) break;
        struct buffer_head *bh = sb_bread(sb, phys_block);
        if (!bh) return -EIO;
        ufs_dirent_t *ents = (ufs_dirent_t *)bh->b_data;
        int ents_per_block = UFS_BLOCK_SIZE / sizeof(ufs_dirent_t);
        int start_j = (b == start_block) ? (ctx->pos % UFS_BLOCK_SIZE) / sizeof(ufs_dirent_t) : 0;
        for (int j = start_j; j < ents_per_block; j++) {
            if (ents[j].inode != 0) {
                char name[57] = {0};
                memcpy(name, ents[j].name, 56);
                if (!dir_emit(ctx, name, strlen(name), ents[j].inode, DT_UNKNOWN)) {
                    brelse(bh);
                    return 0;
                }
            }
            ctx->pos += sizeof(ufs_dirent_t);
        }
        brelse(bh);
    }
    return 0;
}

static const struct file_operations ufs_dir_operations = {
    .iterate_shared = ufs_iterate,
    .llseek = generic_file_llseek,
};

static int ufs_fill_super(struct super_block *sb, struct fs_context *fc) {
    struct buffer_head *bh;
    struct ufs_sb_info *sbi;
    struct inode *root;

    sb_set_blocksize(sb, UFS_BLOCK_SIZE);
    bh = sb_bread(sb, 0);
    if (!bh) return -EIO;
    ufs_superblock_t *dsb = (ufs_superblock_t *)bh->b_data;
    if (dsb->magic != UFS_MAGIC) { brelse(bh); return -EINVAL; }
    if (dsb->checksum != ufs_calc_sb_checksum(dsb)) { brelse(bh); return -EIO; }

    sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
    if (!sbi) { brelse(bh); return -ENOMEM; }
    sbi->sb = *dsb;
    sbi->sb_bh = bh;
    sbi->inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    sbi->dirents_per_block = UFS_BLOCK_SIZE / sizeof(ufs_dirent_t);
    mutex_init(&sbi->s_lock);
    sb->s_fs_info = sbi;
    sb->s_magic = UFS_MAGIC;
    sb->s_maxbytes = MAX_LFS_FILESIZE;
    sb->s_op = &ufs_super_ops;

    root = ufs_iget(sb, sbi->sb.root_inode);
    if (IS_ERR(root)) { brelse(sbi->sb_bh); kfree(sbi); return PTR_ERR(root); }
    sb->s_root = d_make_root(root);
    if (!sb->s_root) { brelse(sbi->sb_bh); kfree(sbi); return -ENOMEM; }
    return 0;
}

static int ufs_get_tree(struct fs_context *fc) {
    return get_tree_bdev(fc, ufs_fill_super);
}

static const struct fs_context_operations ufs_context_ops = {
    .get_tree = ufs_get_tree,
};

static int ufs_init_fs_context(struct fs_context *fc) {
    fc->ops = &ufs_context_ops;
    return 0;
}

static struct file_system_type ufs_fs_type = {
    .owner = THIS_MODULE,
    .name = "utmsfs",
    .init_fs_context = ufs_init_fs_context,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};

static int __init init_ufs(void) { return register_filesystem(&ufs_fs_type); }
static void __exit exit_ufs(void) { unregister_filesystem(&ufs_fs_type); }

module_init(init_ufs)
module_exit(exit_ufs)
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UFS Read/Write Filesystem Driver");
MODULE_AUTHOR("UIT");
