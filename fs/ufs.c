// File: fs/ufs.c
#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"

#define UFS_MAGIC 0x55465302
#define SUPERBLOCK_BLOCK 0

typedef struct {
    u32 magic;
    u32 total_blocks;
    u32 free_blocks;
    u32 root_dir;
    u32 bitmap_start;
    u32 bitmap_blocks;
    u32 version;
} __attribute__((packed)) ufs_superblock_t;

typedef struct {
    u32 block;
    u32 last_used;
    u8  data[UFS_BLOCK_SIZE];
    u8  dirty;
    u8  valid;
} cache_entry_t;

static ufs_superblock_t sb;
static int mounted = 0;
static u32 part_start = 0;
static int current_disk = 0;
static char mounted_device[16] = "";
static char mount_point[256] = "/";

static cache_entry_t cache[UFS_CACHE_SIZE];

extern u32 system_ticks;

static u32 get_tick(void) { return system_ticks; }

static int read_block(u32 b, u8* buf);
static int write_block(u32 b, u8* buf);

static cache_entry_t* cache_find(u32 b) {
    for (int i = 0; i < UFS_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].block == b) return &cache[i];
    }
    return NULL;
}

static cache_entry_t* cache_alloc(void) {
    u32 oldest = 0xFFFFFFFF;
    int oldest_idx = -1;
    for (int i = 0; i < UFS_CACHE_SIZE; i++) {
        if (!cache[i].valid) return &cache[i];
        if (cache[i].last_used < oldest) {
            oldest = cache[i].last_used;
            oldest_idx = i;
        }
    }
    if (oldest_idx < 0) return NULL;
    cache_entry_t* e = &cache[oldest_idx];
    if (e->dirty && e->valid) {
        disk_set_disk(current_disk);
        disk_write(part_start + e->block, e->data);
    }
    return e;
}

static void cache_flush(void) {
    for (int i = 0; i < UFS_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dirty) {
            disk_set_disk(current_disk);
            disk_write(part_start + cache[i].block, cache[i].data);
            cache[i].dirty = 0;
        }
    }
}

static int read_block(u32 b, u8* buf) {
    if (!buf) return -1;
    cache_entry_t* e = cache_find(b);
    if (e) {
        e->last_used = get_tick();
        memcpy(buf, e->data, UFS_BLOCK_SIZE);
        return 0;
    }
    e = cache_alloc();
    if (!e) return -1;
    disk_set_disk(current_disk);
    if (disk_read(part_start + b, e->data) != 0) return -1;
    e->valid = 1;
    e->dirty = 0;
    e->block = b;
    e->last_used = get_tick();
    memcpy(buf, e->data, UFS_BLOCK_SIZE);
    return 0;
}

static int write_block(u32 b, u8* buf) {
    if (!buf) return -1;
    cache_entry_t* e = cache_find(b);
    if (!e) {
        e = cache_alloc();
        if (!e) return -1;
        e->valid = 1;
        e->block = b;
    }
    memcpy(e->data, buf, UFS_BLOCK_SIZE);
    e->dirty = 1;
    e->last_used = get_tick();
    return 0;
}

static int save_superblock(void) {
    u8 buf[UFS_BLOCK_SIZE] = {0};
    memcpy(buf, &sb, sizeof(sb));
    return write_block(SUPERBLOCK_BLOCK, buf);
}

static int load_superblock(void) {
    u8 buf[UFS_BLOCK_SIZE] = {0};
    if (read_block(SUPERBLOCK_BLOCK, buf) != 0) return -1;
    memcpy(&sb, buf, sizeof(sb));
    if (sb.magic != UFS_MAGIC) return -1;
    return 0;
}

static int bitmap_get(u32 b) {
    if (b >= sb.total_blocks) return -1;
    u32 byte_in_bitmap = b / 8;
    u32 bit = b % 8;
    u32 bitmap_block = sb.bitmap_start + byte_in_bitmap / UFS_BLOCK_SIZE;
    u32 offset = byte_in_bitmap % UFS_BLOCK_SIZE;
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(bitmap_block, buf) != 0) return -1;
    return (buf[offset] >> bit) & 1;
}

static int bitmap_set(u32 b, int val) {
    if (b >= sb.total_blocks) return -1;
    u32 byte_in_bitmap = b / 8;
    u32 bit = b % 8;
    u32 bitmap_block = sb.bitmap_start + byte_in_bitmap / UFS_BLOCK_SIZE;
    u32 offset = byte_in_bitmap % UFS_BLOCK_SIZE;
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(bitmap_block, buf) != 0) return -1;
    if (val) buf[offset] |= (1 << bit);
    else buf[offset] &= ~(1 << bit);
    return write_block(bitmap_block, buf);
}

static u32 find_free_block(void) {
    for (u32 i = 2; i < sb.total_blocks; i++) {
        if (bitmap_get(i) == 0) {
            bitmap_set(i, 1);
            sb.free_blocks--;
            save_superblock();
            u8 zero[UFS_BLOCK_SIZE] = {0};
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
    save_superblock();
}

static u32 get_block_for_file(FSNode* e, u32 idx) {
    if (idx < UFS_DIRECT_BLOCKS) return e->direct[idx];
    if (!e->indirect) return 0;
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(e->indirect, buf) != 0) return 0;
    u32* ptrs = (u32*)buf;
    u32 ind_idx = idx - UFS_DIRECT_BLOCKS;
    if (ind_idx >= UFS_BLOCK_SIZE / 4) return 0;
    return ptrs[ind_idx];
}

static int set_block_for_file(FSNode* e, u32 idx, u32 block) {
    if (idx < UFS_DIRECT_BLOCKS) {
        e->direct[idx] = block;
        return 0;
    }
    if (!e->indirect) {
        e->indirect = find_free_block();
        if (!e->indirect) return -1;
    }
    u8 buf[UFS_BLOCK_SIZE] = {0};
    read_block(e->indirect, buf);
    u32* ptrs = (u32*)buf;
    u32 ind_idx = idx - UFS_DIRECT_BLOCKS;
    if (ind_idx >= UFS_BLOCK_SIZE / 4) return -1;
    ptrs[ind_idx] = block;
    return write_block(e->indirect, buf);
}

static void free_file_blocks(FSNode* e) {
    for (u32 i = 0; i < e->blocks; i++) {
        u32 b = get_block_for_file(e, i);
        if (b) free_block(b);
    }
    if (e->indirect) {
        free_block(e->indirect);
        e->indirect = 0;
    }
}

static int get_path_component(const char **path, char *comp) {
    while (**path == '/') (*path)++;
    if (**path == '\0') return 0;
    int i = 0;
    while (**path && **path != '/' && i < UFS_MAX_NAME-1) {
        comp[i++] = **path;
        (*path)++;
    }
    comp[i] = '\0';
    return 1;
}

static u32 resolve_path(const char* path);

static int find_in_dir(u32 dir_block, const char* name, FSNode* out, u32* out_block) {
    if (!name || name[0] == '\0') return -1;
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    FSNode* e0 = (FSNode*)buf;
    u32 total_blocks = e0[0].blocks;
    int entries_per = UFS_BLOCK_SIZE / sizeof(FSNode);

    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e0[0], b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) continue;
        FSNode* entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) {
            if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
                if (out) memcpy(out, &entries[i], sizeof(FSNode));
                if (out_block) *out_block = b;
                return 0;
            }
        }
    }
    return -1;
}

static u32 resolve_path(const char* path) {
    if (!path || path[0] == '\0') return 0;
    if (strcmp(path, "/") == 0) return sb.root_dir;
    const char* p = path;
    u32 current = sb.root_dir;
    char comp[UFS_MAX_NAME];
    while (get_path_component(&p, comp)) {
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            u8 buf[UFS_BLOCK_SIZE];
            if (read_block(current, buf) != 0) return 0;
            FSNode* e = (FSNode*)buf;
            current = e[1].direct[0];
            continue;
        }
        FSNode found;
        if (find_in_dir(current, comp, &found, NULL) != 0) return 0;
        if (!found.is_dir) return 0;
        current = found.direct[0];
    }
    return current;
}

static int get_parent(const char* path, u32* parent, char* name) {
    const char* p = path;
    const char* last_slash = NULL;
    while (*p) { if (*p == '/') last_slash = p; p++; }
    if (!last_slash) {
        strcpy(name, path);
        *parent = sb.root_dir;
        return 0;
    }
    int dir_len = last_slash - path;
    char dir[256];
    if (dir_len == 0) strcpy(dir, "/");
    else { strncpy(dir, path, dir_len); dir[dir_len] = '\0'; }
    strcpy(name, last_slash + 1);
    *parent = resolve_path(dir);
    return (*parent == 0) ? -1 : 0;
}

static int add_to_dir(u32 dir_block, FSNode* new_entry) {
    if (!new_entry || !new_entry->name[0]) return -1;
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    FSNode* e0 = (FSNode*)buf;
    u32 total_blocks = e0[0].blocks;
    int entries_per = UFS_BLOCK_SIZE / sizeof(FSNode);

    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e0[0], b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) continue;
        FSNode* entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) {
            if (entries[i].name[0] && strcmp(entries[i].name, new_entry->name) == 0) return -1;
        }
        for (int i = 0; i < entries_per; i++) {
            if (entries[i].name[0] == 0) {
                memcpy(&entries[i], new_entry, sizeof(FSNode));
                return write_block(b, entry_buf);
            }
        }
    }

    u32 new_b = find_free_block();
    if (!new_b) return -1;
    u8 new_buf[UFS_BLOCK_SIZE];
    memset(new_buf, 0, sizeof(new_buf));
    FSNode* new_entries = (FSNode*)new_buf;
    memcpy(&new_entries[0], new_entry, sizeof(FSNode));
    if (write_block(new_b, new_buf) != 0) { free_block(new_b); return -1; }

    if (set_block_for_file(&e0[0], total_blocks, new_b) != 0) { free_block(new_b); return -1; }
    e0[0].blocks = total_blocks + 1;
    e0[0].mtime = get_tick();
    return write_block(dir_block, buf);
}

static int remove_from_dir(u32 dir_block, const char* name) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    FSNode* e0 = (FSNode*)buf;
    u32 total_blocks = e0[0].blocks;
    int entries_per = UFS_BLOCK_SIZE / sizeof(FSNode);

    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e0[0], b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) continue;
        FSNode* entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) {
            if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
                memset(&entries[i], 0, sizeof(FSNode));
                return write_block(b, entry_buf);
            }
        }
    }
    return -1;
}

static int update_in_dir(u32 dir_block, const char* name, FSNode* new_data) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    FSNode* e0 = (FSNode*)buf;
    u32 total_blocks = e0[0].blocks;
    int entries_per = UFS_BLOCK_SIZE / sizeof(FSNode);

    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e0[0], b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) continue;
        FSNode* entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) {
            if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
                memcpy(&entries[i], new_data, sizeof(FSNode));
                return write_block(b, entry_buf);
            }
        }
    }
    return -1;
}

int ufs_mount(u32 start_lba, int disk) {
    return ufs_mount_with_point(start_lba, disk, "/");
}

int ufs_mount_with_point(u32 start_lba, int disk, const char* point) {
    part_start = start_lba;
    current_disk = disk;
    memset(cache, 0, sizeof(cache));
    if (load_superblock() != 0) return -1;
    mounted = 1;
    snprintf(mounted_device, sizeof(mounted_device), "/dev/sd%c", 'a' + disk);
    if (point && point[0]) strcpy(mount_point, point);
    else strcpy(mount_point, "/");
    return 0;
}

int ufs_umount(void) {
    if (!mounted) return -1;
    cache_flush();
    mounted = 0;
    return 0;
}

int ufs_ismounted(void) { return mounted; }
const char* ufs_get_device(void) { return mounted_device; }
const char* ufs_get_mount_point(void) { return mount_point; }

int ufs_format(u32 start_lba, u32 blocks, int disk) {
    part_start = start_lba;
    current_disk = disk;
    memset(cache, 0, sizeof(cache));
    u8 zero[UFS_BLOCK_SIZE] = {0};
    for (u32 i = 0; i < 10 && i < blocks; i++) {
        disk_set_disk(disk);
        disk_write(start_lba + i, zero);
    }
    memset(&sb, 0, sizeof(sb));
    sb.magic = UFS_MAGIC;
    sb.total_blocks = blocks;
    sb.version = 2;
    u32 bitmap_blocks = (blocks + 8 * UFS_BLOCK_SIZE - 1) / (8 * UFS_BLOCK_SIZE);
    sb.bitmap_start = 2;
    sb.bitmap_blocks = bitmap_blocks;
    sb.root_dir = sb.bitmap_start + bitmap_blocks;
    sb.free_blocks = blocks - 2 - bitmap_blocks - 1;
    if (disk_write(start_lba + SUPERBLOCK_BLOCK, (u8*)&sb) != 0) return -1;
    for (u32 i = 0; i < bitmap_blocks; i++) {
        disk_write(start_lba + sb.bitmap_start + i, zero);
    }
    for (u32 i = 0; i < 2 + bitmap_blocks + 1; i++) bitmap_set(i, 1);
    u8 buf[UFS_BLOCK_SIZE] = {0};
    FSNode* e = (FSNode*)buf;
    strcpy(e[0].name, ".");
    e[0].direct[0] = sb.root_dir;
    e[0].is_dir = 1;
    e[0].mode = UFS_MODE_DIR | UFS_MODE_RW;
    e[0].blocks = 1;
    e[0].mtime = e[0].ctime = e[0].atime = get_tick();
    strcpy(e[1].name, "..");
    e[1].direct[0] = sb.root_dir;
    e[1].is_dir = 1;
    e[1].mode = UFS_MODE_DIR | UFS_MODE_RW;
    disk_write(start_lba + sb.root_dir, buf);
    return 0;
}

int ufs_mkdir(const char* path) {
    if (!mounted) return -1;
    char name[UFS_MAX_NAME];
    u32 parent_block;
    if (get_parent(path, &parent_block, name) != 0) return -1;
    if (name[0] == '\0') return -1;
    if (find_in_dir(parent_block, name, NULL, NULL) == 0) return -1;
    u32 new_block = find_free_block();
    if (!new_block) return -1;
    u8 new_buf[UFS_BLOCK_SIZE];
    memset(new_buf, 0, sizeof(new_buf));
    FSNode* new_e = (FSNode*)new_buf;
    strcpy(new_e[0].name, ".");
    new_e[0].direct[0] = new_block;
    new_e[0].is_dir = 1;
    new_e[0].mode = UFS_MODE_DIR | UFS_MODE_RW;
    new_e[0].blocks = 1;
    new_e[0].mtime = new_e[0].ctime = new_e[0].atime = get_tick();
    strcpy(new_e[1].name, "..");
    new_e[1].direct[0] = parent_block;
    new_e[1].is_dir = 1;
    new_e[1].mode = UFS_MODE_DIR | UFS_MODE_RW;
    if (write_block(new_block, new_buf) != 0) {
        free_block(new_block);
        return -1;
    }
    FSNode entry;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, name);
    entry.direct[0] = new_block;
    entry.is_dir = 1;
    entry.mode = UFS_MODE_DIR | UFS_MODE_RW;
    entry.blocks = 1;
    entry.mtime = entry.ctime = entry.atime = get_tick();
    return add_to_dir(parent_block, &entry);
}

int ufs_write(const char* path, u8* data, u32 size) {
    if (!mounted) return -1;
    if (ufs_exists(path) && !ufs_isdir(path)) ufs_delete(path);
    char name[UFS_MAX_NAME];
    u32 parent_block;
    if (get_parent(path, &parent_block, name) != 0) return -1;
    if (name[0] == '\0') return -1;
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    FSNode entry;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, name);
    entry.is_dir = 0;
    entry.mode = UFS_MODE_FILE | UFS_MODE_RW;
    entry.size = size;
    entry.blocks = blocks;
    entry.mtime = entry.ctime = entry.atime = get_tick();
    for (u32 i = 0; i < blocks; i++) {
        u32 b = find_free_block();
        if (!b) {
            for (u32 j = 0; j < i; j++) free_block(get_block_for_file(&entry, j));
            return -1;
        }
        set_block_for_file(&entry, i, b);
        u8 block_buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        u32 chunk = size - offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        if (data) memcpy(block_buf, data + offset, chunk);
        if (write_block(b, block_buf) != 0) {
            for (u32 j = 0; j <= i; j++) free_block(get_block_for_file(&entry, j));
            return -1;
        }
    }
    return add_to_dir(parent_block, &entry);
}

int ufs_rewrite(const char* path, u8* data, u32 size) {
    if (!mounted) return -1;
    char name[UFS_MAX_NAME];
    u32 parent_block;
    if (get_parent(path, &parent_block, name) != 0) return -1;
    FSNode e;
    if (find_in_dir(parent_block, name, &e, NULL) != 0) return -1;
    if (e.is_dir) return -1;
    free_file_blocks(&e);
    memset(e.direct, 0, sizeof(e.direct));
    e.indirect = 0;
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = find_free_block();
        if (!b) {
            for (u32 j = 0; j < i; j++) free_block(get_block_for_file(&e, j));
            return -1;
        }
        set_block_for_file(&e, i, b);
        u8 block_buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        u32 chunk = size - offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        if (data) memcpy(block_buf, data + offset, chunk);
        if (write_block(b, block_buf) != 0) {
            for (u32 j = 0; j <= i; j++) free_block(get_block_for_file(&e, j));
            return -1;
        }
    }
    e.size = size;
    e.blocks = blocks;
    e.mtime = get_tick();
    return update_in_dir(parent_block, name, &e);
}

int ufs_read(const char* path, u8** data, u32* size) {
    if (!mounted) return -1;
    char name[UFS_MAX_NAME];
    u32 parent_block;
    if (get_parent(path, &parent_block, name) != 0) return -1;
    FSNode e;
    if (find_in_dir(parent_block, name, &e, NULL) != 0) return -1;
    if (e.is_dir) return -1;
    *size = e.size;
    if (e.size == 0) {
        *data = kmalloc(1);
        if (!*data) return -1;
        (*data)[0] = '\0';
        return 0;
    }
    *data = kmalloc(e.size + 1);
    if (!*data) return -1;
    u32 left = e.size;
    u32 pos = 0;
    u32 idx = 0;
    while (left > 0) {
        u32 b = get_block_for_file(&e, idx++);
        if (!b) break;
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(b, buf) != 0) { kfree(*data); return -1; }
        u32 chunk = (left < UFS_BLOCK_SIZE) ? left : UFS_BLOCK_SIZE;
        memcpy(*data + pos, buf, chunk);
        pos += chunk;
        left -= chunk;
    }
    (*data)[e.size] = '\0';
    return 0;
}

int ufs_delete(const char* path) {
    if (!mounted) return -1;
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    FSNode e;
    if (find_in_dir(parent, name, &e, NULL) != 0) return -1;
    if (e.is_dir) return -1;
    free_file_blocks(&e);
    return remove_from_dir(parent, name);
}

int ufs_readdir(const char* path, FSNode** entries, u32* count) {
    if (!mounted) return -1;
    u32 dir_block = (path && path[0] && strcmp(path, "/") != 0) ? resolve_path(path) : sb.root_dir;
    if (dir_block == 0) return -1;
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    FSNode* e0 = (FSNode*)buf;
    u32 total_blocks = e0[0].blocks;
    int entries_per = UFS_BLOCK_SIZE / sizeof(FSNode);
    u32 total = 0;
    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e0[0], b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) continue;
        FSNode* block_entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) if (block_entries[i].name[0]) total++;
    }
    *entries = kmalloc(total * sizeof(FSNode));
    if (!*entries) return -1;
    u32 idx = 0;
    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e0[0], b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) { kfree(*entries); return -1; }
        FSNode* block_entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) {
            if (block_entries[i].name[0]) memcpy(&(*entries)[idx++], &block_entries[i], sizeof(FSNode));
        }
    }
    *count = total;
    return 0;
}

int ufs_rmdir(const char* path) {
    if (!mounted) return -1;
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    FSNode e;
    if (find_in_dir(parent, name, &e, NULL) != 0) return -1;
    if (!e.is_dir) return -1;
    int entries_per = UFS_BLOCK_SIZE / sizeof(FSNode);
    u32 total_blocks = e.blocks;
    for (u32 b_idx = 0; b_idx < total_blocks; b_idx++) {
        u32 b = get_block_for_file(&e, b_idx);
        if (!b) continue;
        u8 entry_buf[UFS_BLOCK_SIZE];
        if (read_block(b, entry_buf) != 0) return -1;
        FSNode* entries = (FSNode*)entry_buf;
        for (int i = 0; i < entries_per; i++) if (entries[i].name[0] != 0) return -1;
    }
    free_file_blocks(&e);
    return remove_from_dir(parent, name);
}

int ufs_rmdir_force(const char* path) {
    if (!mounted) return -1;
    FSNode* entries; u32 count;
    if (ufs_readdir(path, &entries, &count) != 0) return -1;
    for (u32 i = 0; i < count; i++) {
        char full[UFS_MAX_PATH];
        if (strcmp(path, "/") == 0) snprintf(full, sizeof(full), "/%s", entries[i].name);
        else snprintf(full, sizeof(full), "%s/%s", path, entries[i].name);
        if (entries[i].is_dir) ufs_rmdir_force(full);
        else ufs_delete(full);
    }
    if (entries) kfree(entries);
    return ufs_rmdir(path);
}

int ufs_exists(const char* path) {
    if (!mounted) return 0;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return 1;
    char name[UFS_MAX_NAME]; u32 parent;
    if (get_parent(path, &parent, name) != 0) return 0;
    FSNode e;
    return (find_in_dir(parent, name, &e, NULL) == 0);
}

int ufs_isdir(const char* path) {
    if (!mounted) return 0;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return 1;
    char name[UFS_MAX_NAME]; u32 parent;
    if (get_parent(path, &parent, name) != 0) return 0;
    FSNode e;
    if (find_in_dir(parent, name, &e, NULL) != 0) return 0;
    return e.is_dir;
}

u32 ufs_file_size(const char* path) {
    if (!mounted) return 0;
    char name[UFS_MAX_NAME]; u32 parent_block;
    if (get_parent(path, &parent_block, name) != 0) return 0;
    FSNode e;
    if (find_in_dir(parent_block, name, &e, NULL) != 0) return 0;
    return e.size;
}

int ufs_stat(u32* total, u32* used, u32* free) {
    if (!mounted) return -1;
    *total = sb.total_blocks * UFS_BLOCK_SIZE;
    *free = sb.free_blocks * UFS_BLOCK_SIZE;
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
