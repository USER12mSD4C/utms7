// File: fs/ufs.c
#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"

extern u32 system_ticks;
static u32 get_tick(void) { return system_ticks; }

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
} __attribute__((packed)) utmsfs_superblock_t;

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
} __attribute__((packed)) utmsfs_inode_t;

typedef struct {
    u32 inode;
    u8 type;
    u8 name_len;
    u16 pad;
    char name[56];
} __attribute__((packed)) utmsfs_dirent_t;

static utmsfs_superblock_t sb;
static int mounted = 0;
static u32 part_start = 0;
static int current_disk = 0;
static char mounted_device[16] = "";
static char mount_point[256] = "/";

static int read_block(u32 b, u8* buf) {
    disk_set_disk(current_disk);
    u32 lba = part_start + b * 8;
    for (int i = 0; i < 8; i++) {
        if (disk_read(lba + i, buf + i * 512) != 0) return -1;
    }
    return 0;
}

static int write_block(u32 b, u8* buf) {
    disk_set_disk(current_disk);
    u32 lba = part_start + b * 8;
    for (int i = 0; i < 8; i++) {
        if (disk_write(lba + i, buf + i * 512) != 0) return -1;
    }
    return 0;
}

static int read_inode(u32 ino, utmsfs_inode_t* out) {
    if (ino == 0 || ino > sb.inode_count) return -1;
    u32 inodes_per_block = UTMSFS_BLOCK_SIZE / sizeof(utmsfs_inode_t);
    u32 block = sb.inode_table_start + (ino - 1) / inodes_per_block;
    u32 off = ((ino - 1) % inodes_per_block) * sizeof(utmsfs_inode_t);
    u8 buf[UTMSFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    memcpy(out, buf + off, sizeof(utmsfs_inode_t));
    return 0;
}

static int write_inode(u32 ino, utmsfs_inode_t* in) {
    if (ino == 0 || ino > sb.inode_count) return -1;
    u32 inodes_per_block = UTMSFS_BLOCK_SIZE / sizeof(utmsfs_inode_t);
    u32 block = sb.inode_table_start + (ino - 1) / inodes_per_block;
    u32 off = ((ino - 1) % inodes_per_block) * sizeof(utmsfs_inode_t);
    u8 buf[UTMSFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    memcpy(buf + off, in, sizeof(utmsfs_inode_t));
    return write_block(block, buf);
}

static int bitmap_get(u32 b) {
    if (b >= sb.total_blocks) return -1;
    u32 block = sb.bitmap_start + b / 32768;
    u32 off = (b % 32768) / 8;
    u32 bit = b % 8;
    u8 buf[UTMSFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    return (buf[off] >> bit) & 1;
}

static int bitmap_set(u32 b, int val) {
    if (b >= sb.total_blocks) return -1;
    u32 block = sb.bitmap_start + b / 32768;
    u32 off = (b % 32768) / 8;
    u32 bit = b % 8;
    u8 buf[UTMSFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    if (val) buf[off] |= (1 << bit);
    else buf[off] &= ~(1 << bit);
    return write_block(block, buf);
}

static u32 alloc_block(void) {
    for (u32 i = sb.data_start; i < sb.total_blocks; i++) {
        if (bitmap_get(i) == 0) {
            bitmap_set(i, 1);
            sb.free_blocks--;
            u8 zero[UTMSFS_BLOCK_SIZE] = {0};
            write_block(i, zero);
            return i;
        }
    }
    return 0;
}

static void free_block(u32 b) {
    if (b == 0 || b >= sb.total_blocks) return;
    bitmap_set(b, 0);
    sb.free_blocks++;
}

static u32 alloc_inode(void) {
    u32 inodes_per_block = UTMSFS_BLOCK_SIZE / sizeof(utmsfs_inode_t);
    u32 total_blocks = (sb.inode_count + inodes_per_block - 1) / inodes_per_block;
    for (u32 i = 0; i < total_blocks; i++) {
        u8 buf[UTMSFS_BLOCK_SIZE];
        if (read_block(sb.inode_table_start + i, buf) != 0) continue;
        utmsfs_inode_t* inodes = (utmsfs_inode_t*)buf;
        for (u32 j = 0; j < inodes_per_block; j++) {
            if (inodes[j].mode == 0) {
                u32 ino = i * inodes_per_block + j + 1;
                if (ino > sb.inode_count) break;
                memset(&inodes[j], 0, sizeof(utmsfs_inode_t));
                write_block(sb.inode_table_start + i, buf);
                sb.free_inodes--;
                return ino;
            }
        }
    }
    return 0;
}

static void free_inode(u32 ino) {
    if (ino == 0 || ino > sb.inode_count) return;
    utmsfs_inode_t in;
    if (read_inode(ino, &in) != 0) return;
    memset(&in, 0, sizeof(in));
    write_inode(ino, &in);
    sb.free_inodes++;
}

static u32 get_block(utmsfs_inode_t* in, u32 idx) {
    for (int i = 0; i < 8; i++) {
        if (in->extents[i].len == 0) break;
        if (idx < in->extents[i].len) return in->extents[i].start + idx;
        idx -= in->extents[i].len;
    }
    return 0;
}

static int append_block(utmsfs_inode_t* in, u32 b) {
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

static void free_inode_blocks(u32 ino) {
    utmsfs_inode_t in;
    if (read_inode(ino, &in) != 0) return;
    for (int i = 0; i < 8; i++) {
        for (u32 j = 0; j < in.extents[i].len; j++) {
            free_block(in.extents[i].start + j);
        }
    }
}

static int find_in_dir(u32 dir_ino, const char* name, u32* out_ino) {
    utmsfs_inode_t dir;
    if (read_inode(dir_ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UTMSFS_BLOCK_SIZE - 1) / UTMSFS_BLOCK_SIZE;
    u8 buf[UTMSFS_BLOCK_SIZE];
    for (u32 i = 0; i < blocks; i++) {
        u32 b = get_block(&dir, i);
        if (!b) continue;
        if (read_block(b, buf) != 0) continue;
        utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
        for (int j = 0; j < UTMSFS_BLOCK_SIZE / sizeof(utmsfs_dirent_t); j++) {
            if (ents[j].inode != 0 && strcmp(ents[j].name, name) == 0) {
                if (out_ino) *out_ino = ents[j].inode;
                return 0;
            }
        }
    }
    return -1;
}

static int add_to_dir(u32 dir_ino, u32 ino, const char* name, u8 type) {
    utmsfs_inode_t dir;
    if (read_inode(dir_ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UTMSFS_BLOCK_SIZE - 1) / UTMSFS_BLOCK_SIZE;
    u8 buf[UTMSFS_BLOCK_SIZE];
    for (u32 i = 0; i < blocks; i++) {
        u32 b = get_block(&dir, i);
        if (!b) continue;
        if (read_block(b, buf) != 0) continue;
        utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
        for (int j = 0; j < UTMSFS_BLOCK_SIZE / sizeof(utmsfs_dirent_t); j++) {
            if (ents[j].inode == 0) {
                ents[j].inode = ino;
                ents[j].type = type;
                strncpy(ents[j].name, name, 55);
                ents[j].name[55] = '\0';
                ents[j].name_len = strlen(ents[j].name);
                write_block(b, buf);
                dir.size += sizeof(utmsfs_dirent_t);
                write_inode(dir_ino, &dir);
                return 0;
            }
        }
    }
    u32 nb = alloc_block();
    if (!nb) return -1;
    memset(buf, 0, UTMSFS_BLOCK_SIZE);
    utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
    ents[0].inode = ino;
    ents[0].type = type;
    strncpy(ents[0].name, name, 55);
    ents[0].name[55] = '\0';
    ents[0].name_len = strlen(ents[0].name);
    if (write_block(nb, buf) != 0 || append_block(&dir, nb) != 0) {
        free_block(nb);
        return -1;
    }
    dir.size += sizeof(utmsfs_dirent_t);
    dir.blocks = (dir.size + 511) / 512;
    write_inode(dir_ino, &dir);
    return 0;
}

static int remove_from_dir(u32 dir_ino, const char* name) {
    utmsfs_inode_t dir;
    if (read_inode(dir_ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UTMSFS_BLOCK_SIZE - 1) / UTMSFS_BLOCK_SIZE;
    u8 buf[UTMSFS_BLOCK_SIZE];
    for (u32 i = 0; i < blocks; i++) {
        u32 b = get_block(&dir, i);
        if (!b) continue;
        if (read_block(b, buf) != 0) continue;
        utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
        for (int j = 0; j < UTMSFS_BLOCK_SIZE / sizeof(utmsfs_dirent_t); j++) {
            if (ents[j].inode != 0 && strcmp(ents[j].name, name) == 0) {
                ents[j].inode = 0;
                ents[j].name[0] = '\0';
                write_block(b, buf);
                return 0;
            }
        }
    }
    return -1;
}

static int get_parent(const char* path, u32* parent_ino, char* name) {
    const char* last_slash = strrchr(path, '/');
    if (!last_slash || last_slash == path) {
        *parent_ino = sb.root_inode;
        strcpy(name, last_slash ? last_slash + 1 : path);
        return 0;
    }
    char dir[256];
    int len = last_slash - path;
    strncpy(dir, path, len);
    dir[len] = '\0';

    u32 ino = sb.root_inode;
    char comp[64];
    const char* p = dir;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = '\0';
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) continue;
        if (find_in_dir(ino, comp, &ino) != 0) return -1;
    }
    *parent_ino = ino;
    strcpy(name, last_slash + 1);
    return 0;
}

int ufs_format(u32 start_lba, u32 total_blocks, int disk) {
    part_start = start_lba;
    current_disk = disk;

    u32 bitmap_blocks = (total_blocks + 32767) / 32768;
    u32 inode_count = 1024;
    u32 inodes_per_block = UTMSFS_BLOCK_SIZE / sizeof(utmsfs_inode_t);
    u32 inode_blocks = (inode_count + inodes_per_block - 1) / inodes_per_block;

    sb.magic = UTMSFS_MAGIC;
    sb.version = UTMSFS_VERSION;
    sb.total_blocks = total_blocks;
    sb.inode_count = inode_count;
    sb.bitmap_start = 1;
    sb.inode_table_start = sb.bitmap_start + bitmap_blocks;
    sb.data_start = sb.inode_table_start + inode_blocks;
    sb.root_inode = 1;

    u8 zero[UTMSFS_BLOCK_SIZE] = {0};
    disk_set_disk(disk);
    for (u32 i = 0; i < sb.data_start; i++) {
        write_block(i, zero);
    }

    for (u32 i = 0; i < sb.data_start; i++) bitmap_set(i, 1);

    utmsfs_inode_t root = {0};
    root.mode = UTMSFS_INODE_DIR | 0755;
    root.nlink = 2;
    root.atime = root.mtime = root.ctime = get_tick();
    write_inode(1, &root);

    sb.free_blocks = total_blocks - sb.data_start;
    sb.free_inodes = inode_count - 1;

    u8 sb_buf[UTMSFS_BLOCK_SIZE] = {0};
    memcpy(sb_buf, &sb, sizeof(sb));
    write_block(0, sb_buf);

    return 0;
}

int ufs_mount_with_point(u32 start_lba, int disk, const char* point) {
    part_start = start_lba;
    current_disk = disk;
    u8 buf[UTMSFS_BLOCK_SIZE];
    if (read_block(0, buf) != 0) return -1;
    memcpy(&sb, buf, sizeof(sb));
    if (sb.magic != UTMSFS_MAGIC) return -1;
    mounted = 1;
    snprintf(mounted_device, sizeof(mounted_device), "/dev/sd%c", 'a' + disk);
    if (point && point[0]) strcpy(mount_point, point);
    else strcpy(mount_point, "/");
    return 0;
}

int ufs_mount(u32 start_lba, int disk) { return ufs_mount_with_point(start_lba, disk, "/"); }
int ufs_umount(void) { if (!mounted) return -1; mounted = 0; return 0; }
int ufs_ismounted(void) { return mounted; }
const char* ufs_get_device(void) { return mounted_device; }
const char* ufs_get_mount_point(void) { return mount_point; }

int ufs_write(const char* path, u8* data, u32 size) {
    if (!mounted) return -1;
    char name[64];
    u32 parent_ino;
    if (get_parent(path, &parent_ino, name) != 0) return -1;

    u32 ino = 0;
    if (find_in_dir(parent_ino, name, &ino) == 0) {
        free_inode_blocks(ino);
        free_inode(ino);
        remove_from_dir(parent_ino, name);
    }

    ino = alloc_inode();
    if (!ino) return -1;

    utmsfs_inode_t in = {0};
    in.mode = UTMSFS_INODE_FILE | 0644;
    in.size = size;
    in.blocks = (size + 511) / 512;
    in.atime = in.mtime = in.ctime = get_tick();

    u32 blocks = (size + UTMSFS_BLOCK_SIZE - 1) / UTMSFS_BLOCK_SIZE;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = alloc_block();
        if (!b) { free_inode(ino); return -1; }
        append_block(&in, b);
        u8 buf[UTMSFS_BLOCK_SIZE] = {0};
        u32 chunk = size - i * UTMSFS_BLOCK_SIZE;
        if (chunk > UTMSFS_BLOCK_SIZE) chunk = UTMSFS_BLOCK_SIZE;
        if (data) memcpy(buf, data + i * UTMSFS_BLOCK_SIZE, chunk);
        write_block(b, buf);
    }

    write_inode(ino, &in);
    return add_to_dir(parent_ino, ino, name, 1);
}

int ufs_rewrite(const char* path, u8* data, u32 size) { return ufs_write(path, data, size); }

int ufs_read(const char* path, u8** data, u32* size) {
    if (!mounted) return -1;
    char name[64];
    u32 parent_ino;
    if (get_parent(path, &parent_ino, name) != 0) return -1;
    u32 ino = 0;
    if (find_in_dir(parent_ino, name, &ino) != 0) return -1;

    utmsfs_inode_t in;
    if (read_inode(ino, &in) != 0) return -1;
    if (in.mode & UTMSFS_INODE_DIR) return -1;

    *size = in.size;
    *data = kmalloc(in.size + 1);
    if (!*data) return -1;

    u32 blocks = (in.size + UTMSFS_BLOCK_SIZE - 1) / UTMSFS_BLOCK_SIZE;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = get_block(&in, i);
        if (!b) break;
        u8 buf[UTMSFS_BLOCK_SIZE];
        if (read_block(b, buf) != 0) { kfree(*data); return -1; }
        u32 chunk = in.size - i * UTMSFS_BLOCK_SIZE;
        if (chunk > UTMSFS_BLOCK_SIZE) chunk = UTMSFS_BLOCK_SIZE;
        memcpy(*data + i * UTMSFS_BLOCK_SIZE, buf, chunk);
    }
    (*data)[in.size] = '\0';
    return 0;
}

int ufs_mkdir(const char* path) {
    if (!mounted) return -1;
    char name[64];
    u32 parent_ino;
    if (get_parent(path, &parent_ino, name) != 0) return -1;
    if (find_in_dir(parent_ino, name, NULL) == 0) return -1;

    u32 ino = alloc_inode();
    if (!ino) return -1;

    utmsfs_inode_t in = {0};
    in.mode = UTMSFS_INODE_DIR | 0755;
    in.nlink = 2;
    in.atime = in.mtime = in.ctime = get_tick();

    u32 b = alloc_block();
    if (!b) { free_inode(ino); return -1; }
    append_block(&in, b);
    in.size = 2 * sizeof(utmsfs_dirent_t);
    in.blocks = (in.size + 511) / 512;

    u8 buf[UTMSFS_BLOCK_SIZE] = {0};
    utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
    ents[0].inode = ino; ents[0].type = 2; strcpy(ents[0].name, "."); ents[0].name_len = 1;
    ents[1].inode = parent_ino; ents[1].type = 2; strcpy(ents[1].name, ".."); ents[1].name_len = 2;
    write_block(b, buf);

    write_inode(ino, &in);
    return add_to_dir(parent_ino, ino, name, 2);
}

int ufs_readdir(const char* path, FSNode** entries, u32* count) {
    if (!mounted) return -1;
    u32 ino = sb.root_inode;
    if (path && path[0] && strcmp(path, "/") != 0) {
        char comp[64];
        const char* p = path;
        while (*p) {
            while (*p == '/') p++;
            if (!*p) break;
            int i = 0;
            while (*p && *p != '/' && i < 63) comp[i++] = *p++;
            comp[i] = '\0';
            if (find_in_dir(ino, comp, &ino) != 0) return -1;
        }
    }

    utmsfs_inode_t dir;
    if (read_inode(ino, &dir) != 0) return -1;
    u32 blocks = (dir.size + UTMSFS_BLOCK_SIZE - 1) / UTMSFS_BLOCK_SIZE;

    u32 total = 0;
    u8 buf[UTMSFS_BLOCK_SIZE];
    for (u32 i = 0; i < blocks; i++) {
        u32 b = get_block(&dir, i);
        if (!b) continue;
        if (read_block(b, buf) != 0) continue;
        utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
        for (int j = 0; j < UTMSFS_BLOCK_SIZE / sizeof(utmsfs_dirent_t); j++) {
            if (ents[j].inode != 0) total++;
        }
    }

    *entries = kmalloc(total * sizeof(FSNode));
    if (!*entries) return -1;

    u32 idx = 0;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = get_block(&dir, i);
        if (!b) continue;
        if (read_block(b, buf) != 0) continue;
        utmsfs_dirent_t* ents = (utmsfs_dirent_t*)buf;
        for (int j = 0; j < UTMSFS_BLOCK_SIZE / sizeof(utmsfs_dirent_t); j++) {
            if (ents[j].inode != 0) {
                utmsfs_inode_t in;
                read_inode(ents[j].inode, &in);
                FSNode* n = &(*entries)[idx++];
                n->mode = in.mode;
                n->size = in.size;
                n->blocks = in.blocks;
                n->is_dir = (in.mode & UTMSFS_INODE_DIR) ? 1 : 0;
                strncpy(n->name, ents[j].name, 27);
                n->name[27] = '\0';
            }
        }
    }
    *count = total;
    return 0;
}

int ufs_delete(const char* path) {
    if (!mounted) return -1;
    char name[64];
    u32 parent_ino;
    if (get_parent(path, &parent_ino, name) != 0) return -1;
    u32 ino = 0;
    if (find_in_dir(parent_ino, name, &ino) != 0) return -1;
    free_inode_blocks(ino);
    free_inode(ino);
    return remove_from_dir(parent_ino, name);
}

int ufs_rmdir(const char* path) { return ufs_delete(path); }
int ufs_rmdir_force(const char* path) { return ufs_delete(path); }
int ufs_exists(const char* path) {
    if (!mounted) return 0;
    if (!path || strcmp(path, "/") == 0) return 1;
    char name[64]; u32 p;
    if (get_parent(path, &p, name) != 0) return 0;
    return find_in_dir(p, name, NULL) == 0;
}
int ufs_isdir(const char* path) {
    if (!mounted) return 0;
    if (!path || strcmp(path, "/") == 0) return 1;
    char name[64]; u32 p, ino;
    if (get_parent(path, &p, name) != 0) return 0;
    if (find_in_dir(p, name, &ino) != 0) return 0;
    utmsfs_inode_t in;
    read_inode(ino, &in);
    return (in.mode & UTMSFS_INODE_DIR) != 0;
}
u32 ufs_file_size(const char* path) {
    if (!mounted) return 0;
    char name[64]; u32 p, ino;
    if (get_parent(path, &p, name) != 0) return 0;
    if (find_in_dir(p, name, &ino) != 0) return 0;
    utmsfs_inode_t in;
    read_inode(ino, &in);
    return in.size;
}
int ufs_stat(u32* total, u32* used, u32* free) {
    if (!mounted) return -1;
    *total = sb.total_blocks * UTMSFS_BLOCK_SIZE;
    *free = sb.free_blocks * UTMSFS_BLOCK_SIZE;
    *used = *total - *free;
    return 0;
}
int ufs_cp(const char* src, const char* dst) {
    u8* data; u32 size;
    if (ufs_read(src, &data, &size) != 0) return -1;
    int ret = ufs_write(dst, data, size);
    kfree(data);
    return ret;
}
int ufs_mv(const char* src, const char* dst) {
    if (ufs_cp(src, dst) != 0) return -1;
    return ufs_delete(src);
}
