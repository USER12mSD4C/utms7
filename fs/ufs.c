#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"

#define ENTRIES_PER_BLOCK (UFS_BLOCK_SIZE / sizeof(FSNode))

typedef struct {
    u32 magic;
    u32 total_blocks;
    u32 free_blocks;
    u32 root_dir;
} __attribute__((packed)) ufs_superblock_t;

static ufs_superblock_t sb;
static int mounted = 0;
static u32 part_start = 0;
static int current_disk = 0;
static char mounted_device[16] = "";

static int read_block(u32 b, u8* buf) {
    if (!buf) return -1;
    disk_set_disk(current_disk);
    return disk_read(part_start + b, buf);
}

static int write_block(u32 b, u8* buf) {
    if (!buf) return -1;
    disk_set_disk(current_disk);
    return disk_write(part_start + b, buf);
}

static int save_superblock(void) {
    u8 buf[UFS_BLOCK_SIZE] = {0};
    memcpy(buf, &sb, sizeof(sb));
    return write_block(SUPERBLOCK_BLOCK, buf);
}

static u32 find_free_block(void) {
    for (u32 i = 2; i < sb.total_blocks; i++) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(i, buf) != 0) continue;
        
        int empty = 1;
        for (int j = 0; j < UFS_BLOCK_SIZE; j++) {
            if (buf[j]) { empty = 0; break; }
        }
        
        if (empty) {
            sb.free_blocks--;
            save_superblock();
            return i;
        }
    }
    return 0;
}

static void free_block(u32 b) {
    if (b == 0 || b >= sb.total_blocks) return;
    u8 zero[UFS_BLOCK_SIZE] = {0};
    write_block(b, zero);
    sb.free_blocks++;
    save_superblock();
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
            if (e[1].is_dir && strcmp(e[1].name, "..") == 0) {
                current = e[1].first_block;
            }
            continue;
        }
        
        u32 found = 0;
        u32 block = current;
        while (block) {
            u8 buf[UFS_BLOCK_SIZE];
            if (read_block(block, buf) != 0) return 0;
            FSNode* e = (FSNode*)buf;
            for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
                if (e[i].name[0] && strcmp(e[i].name, comp) == 0) {
                    if (!e[i].is_dir) return 0;
                    current = e[i].first_block;
                    found = 1;
                    break;
                }
            }
            if (found) break;
            block = e[0].next_block;
        }
        if (!found) return 0;
    }
    return current;
}

static int get_parent(const char* path, u32* parent, char* name) {
    const char* p = path;
    const char* last_slash = NULL;
    while (*p) {
        if (*p == '/') last_slash = p;
        p++;
    }
    if (!last_slash) {
        strcpy(name, path);
        *parent = sb.root_dir;
        return 0;
    }
    int dir_len = last_slash - path;
    char dir[256];
    if (dir_len == 0) {
        strcpy(dir, "/");
    } else {
        strncpy(dir, path, dir_len);
        dir[dir_len] = '\0';
    }
    strcpy(name, last_slash + 1);
    *parent = resolve_path(dir);
    return (*parent == 0) ? -1 : 0;
}

static int find_in_dir(u32 dir_block, const char* name, FSNode* out) {
    if (!name || name[0] == '\0') return -1;
    
    u32 current = dir_block;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
                if (out) memcpy(out, &e[i], sizeof(FSNode));
                return 0;
            }
        }
        current = e[0].next_block;
    }
    return -1;
}

static int add_to_dir(u32 dir_block, FSNode* new_entry) {
    if (!new_entry || !new_entry->name[0]) return -1;
    
    u32 current = dir_block;
    u32 last = 0;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, new_entry->name) == 0) {
                return -1;
            }
        }
        
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] == 0) {
                memcpy(&e[i], new_entry, sizeof(FSNode));
                return write_block(current, buf);
            }
        }
        
        last = current;
        current = e[0].next_block;
    }
    
    u32 new_block = find_free_block();
    if (!new_block) return -1;
    
    u8 buf[UFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    FSNode* e = (FSNode*)buf;
    
    strcpy(e[0].name, ".");
    e[0].first_block = new_block;
    e[0].is_dir = 1;
    e[0].next_block = 0;
    
    strcpy(e[1].name, "..");
    e[1].first_block = dir_block;
    e[1].is_dir = 1;
    e[1].next_block = 0;
    
    memcpy(&e[2], new_entry, sizeof(FSNode));
    
    if (write_block(new_block, buf) != 0) {
        free_block(new_block);
        return -1;
    }
    
    if (last != 0) {
        u8 last_buf[UFS_BLOCK_SIZE];
        if (read_block(last, last_buf) != 0) return -1;
        FSNode* last_e = (FSNode*)last_buf;
        last_e[0].next_block = new_block;
        if (write_block(last, last_buf) != 0) return -1;
    }
    
    return 0;
}

static int remove_from_dir(u32 dir_block, const char* name) {
    u32 current = dir_block;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
                memset(&e[i], 0, sizeof(FSNode));
                return write_block(current, buf);
            }
        }
        
        current = e[0].next_block;
    }
    
    return -1;
}

static int update_in_dir(u32 dir_block, const char* name, FSNode* new_data) {
    u32 current = dir_block;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
                memcpy(&e[i], new_data, sizeof(FSNode));
                return write_block(current, buf);
            }
        }
        
        current = e[0].next_block;
    }
    
    return -1;
}

int ufs_mount(u32 start_lba, int disk) {
    part_start = start_lba;
    current_disk = disk;
    disk_set_disk(disk);
    
    u8 buf[UFS_BLOCK_SIZE];
    if (disk_read(start_lba, buf) != 0) return -1;
    
    memcpy(&sb, buf, sizeof(sb));
    
    if (sb.magic != UFS_MAGIC) return -1;
    
    mounted = 1;
    
    int len = strlen("/dev/sd");
    strcpy(mounted_device, "/dev/sd");
    mounted_device[len] = 'a' + disk;
    mounted_device[len + 1] = '\0';
    
    return 0;
}

int ufs_umount(void) {
    if (!mounted) return -1;
    mounted = 0;
    mounted_device[0] = '\0';
    return 0;
}

int ufs_ismounted(void) {
    return mounted;
}

const char* ufs_get_device(void) {
    return mounted_device;
}

int ufs_format(u32 start_lba, u32 blocks, int disk) {
    part_start = start_lba;
    current_disk = disk;
    disk_set_disk(disk);
    
    memset(&sb, 0, sizeof(sb));
    sb.magic = UFS_MAGIC;
    sb.total_blocks = blocks;
    sb.free_blocks = blocks - 2;
    sb.root_dir = ROOTDIR_BLOCK;
    
    u8 buf[UFS_BLOCK_SIZE];
    memcpy(buf, &sb, sizeof(sb));
    if (disk_write(start_lba, buf) != 0) return -1;
    
    memset(buf, 0, UFS_BLOCK_SIZE);
    FSNode* e = (FSNode*)buf;
    
    strcpy(e[0].name, ".");
    e[0].first_block = ROOTDIR_BLOCK;
    e[0].is_dir = 1;
    e[0].next_block = 0;
    
    strcpy(e[1].name, "..");
    e[1].first_block = ROOTDIR_BLOCK;
    e[1].is_dir = 1;
    e[1].next_block = 0;
    
    if (disk_write(start_lba + ROOTDIR_BLOCK, buf) != 0) return -1;
    
    mounted = 1;
    
    int len = strlen("/dev/sd");
    strcpy(mounted_device, "/dev/sd");
    mounted_device[len] = 'a' + disk;
    mounted_device[len + 1] = '\0';
    
    return 0;
}

int ufs_mkdir(const char* path) {
    if (!mounted) return -1;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return -1;
    if (name[0] == '\0') return -1;
    
    if (find_in_dir(parent_block, name, NULL) == 0) return -1;
    
    u32 new_block = find_free_block();
    if (!new_block) return -1;
    
    u8 new_buf[UFS_BLOCK_SIZE];
    memset(new_buf, 0, sizeof(new_buf));
    FSNode* new_e = (FSNode*)new_buf;
    
    strcpy(new_e[0].name, ".");
    new_e[0].first_block = new_block;
    new_e[0].is_dir = 1;
    new_e[0].next_block = 0;
    
    strcpy(new_e[1].name, "..");
    new_e[1].first_block = parent_block;
    new_e[1].is_dir = 1;
    new_e[1].next_block = 0;
    
    if (write_block(new_block, new_buf) != 0) {
        free_block(new_block);
        return -1;
    }
    
    FSNode entry;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, name);
    entry.first_block = new_block;
    entry.is_dir = 1;
    entry.next_block = 0;
    
    return add_to_dir(parent_block, &entry);
}

int ufs_write(const char* path, u8* data, u32 size) {
    if (!mounted) return -1;
    
    if (ufs_exists(path) && !ufs_isdir(path)) {
        ufs_delete(path);
    }
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return -1;
    if (name[0] == '\0') return -1;
    
    if (size == 0) {
        FSNode entry;
        memset(&entry, 0, sizeof(entry));
        strcpy(entry.name, name);
        entry.next_block = 0;
        return add_to_dir(parent_block, &entry);
    }
    
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    if (sb.free_blocks < blocks) return -1;
    
    u32 first_block = 0;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = find_free_block();
        if (!b) {
            for (u32 j = 0; j < i; j++) free_block(first_block + j);
            return -1;
        }
        
        if (i == 0) first_block = b;
        
        u8 block_buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        u32 chunk = size - offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        
        if (data) memcpy(block_buf, data + offset, chunk);
        
        if (write_block(b, block_buf) != 0) {
            for (u32 j = 0; j <= i; j++) free_block(first_block + j);
            return -1;
        }
    }
    
    FSNode entry;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, name);
    entry.size = size;
    entry.first_block = first_block;
    entry.next_block = 0;
    
    return add_to_dir(parent_block, &entry);
}

int ufs_rewrite(const char* path, u8* data, u32 size) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return -1;
    if (name[0] == '\0') return -1;
    
    FSNode e;
    if (find_in_dir(parent_block, name, &e) != 0) return -1;
    if (e.is_dir) return -1;
    
    if (e.first_block != 0) {
        u32 blocks = (e.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
        for (u32 i = 0; i < blocks; i++) {
            free_block(e.first_block + i);
        }
    }
    
    if (size == 0) {
        e.size = 0;
        e.first_block = 0;
        return update_in_dir(parent_block, name, &e);
    }
    
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    if (sb.free_blocks < blocks) return -1;
    
    u32 first_block = 0;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = find_free_block();
        if (!b) {
            for (u32 j = 0; j < i; j++) free_block(first_block + j);
            return -1;
        }
        
        if (i == 0) first_block = b;
        
        u8 block_buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        u32 chunk = size - offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        
        if (data) memcpy(block_buf, data + offset, chunk);
        
        if (write_block(b, block_buf) != 0) {
            for (u32 j = 0; j <= i; j++) free_block(first_block + j);
            return -1;
        }
    }
    
    e.size = size;
    e.first_block = first_block;
    
    return update_in_dir(parent_block, name, &e);
}

int ufs_write_at(const char* path, u8* data, u32 size, u32 offset) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return -1;
    if (name[0] == '\0') return -1;
    
    FSNode e;
    if (find_in_dir(parent_block, name, &e) != 0) {
        if (offset == 0) {
            return ufs_write(path, data, size);
        }
        return -1;
    }
    if (e.is_dir) return -1;
    
    u32 new_size = (offset + size > e.size) ? offset + size : e.size;
    u32 old_blocks = (e.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    u32 new_blocks = (new_size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    
    if (new_blocks > old_blocks) {
        if (sb.free_blocks < (new_blocks - old_blocks)) return -1;
    }
    
    u8 *full_data = kmalloc(new_size);
    if (!full_data) return -1;
    memset(full_data, 0, new_size);
    
    if (e.size > 0) {
        u32 block = e.first_block;
        u32 pos = 0;
        u32 left = e.size;
        
        while (left > 0 && block != 0) {
            u8 buf[UFS_BLOCK_SIZE];
            if (read_block(block, buf) != 0) {
                kfree(full_data);
                return -1;
            }
            
            u32 chunk = (left < UFS_BLOCK_SIZE) ? left : UFS_BLOCK_SIZE;
            memcpy(full_data + pos, buf, chunk);
            
            pos += chunk;
            left -= chunk;
            block++;
        }
    }
    
    memcpy(full_data + offset, data, size);
    
    for (u32 i = 0; i < old_blocks; i++) {
        free_block(e.first_block + i);
    }
    
    u32 first_block = 0;
    for (u32 i = 0; i < new_blocks; i++) {
        u32 b = find_free_block();
        if (!b) {
            for (u32 j = 0; j < i; j++) free_block(first_block + j);
            kfree(full_data);
            return -1;
        }
        
        if (i == 0) first_block = b;
        
        u8 block_buf[UFS_BLOCK_SIZE] = {0};
        u32 block_offset = i * UFS_BLOCK_SIZE;
        u32 chunk = new_size - block_offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        
        memcpy(block_buf, full_data + block_offset, chunk);
        
        if (write_block(b, block_buf) != 0) {
            for (u32 j = 0; j <= i; j++) free_block(first_block + j);
            kfree(full_data);
            return -1;
        }
    }
    
    kfree(full_data);
    
    e.size = new_size;
    e.first_block = first_block;
    
    if (update_in_dir(parent_block, name, &e) != 0) return -1;
    
    return size;
}

int ufs_read(const char* path, u8** data, u32* size) {
    if (!mounted) return -1;
    if (!path || path[0] == '\0') return -1;
    if (strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return -1;
    
    FSNode e;
    if (find_in_dir(parent_block, name, &e) != 0) return -1;
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
    
    u32 block = e.first_block;
    u32 left = e.size;
    u32 pos = 0;
    
    while (left > 0 && block != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(block, buf) != 0) {
            kfree(*data);
            return -1;
        }
        
        u32 chunk = (left < UFS_BLOCK_SIZE) ? left : UFS_BLOCK_SIZE;
        memcpy(*data + pos, buf, chunk);
        
        pos += chunk;
        left -= chunk;
        block++;
    }
    
    (*data)[e.size] = '\0';
    return 0;
}

int ufs_read_at(const char* path, u8* data, u32 size, u32 offset) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return -1;
    
    FSNode e;
    if (find_in_dir(parent_block, name, &e) != 0) return -1;
    if (e.is_dir) return -1;
    
    if (offset >= e.size) return 0;
    
    u32 to_read = (offset + size > e.size) ? e.size - offset : size;
    if (to_read == 0) return 0;
    
    u32 start_block = e.first_block + (offset / UFS_BLOCK_SIZE);
    u32 block_offset = offset % UFS_BLOCK_SIZE;
    u32 left = to_read;
    u32 pos = 0;
    u32 current_block = start_block;
    
    while (left > 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current_block, buf) != 0) return pos;
        
        u32 chunk = UFS_BLOCK_SIZE - block_offset;
        if (chunk > left) chunk = left;
        
        memcpy(data + pos, buf + block_offset, chunk);
        
        pos += chunk;
        left -= chunk;
        current_block++;
        block_offset = 0;
    }
    
    return to_read;
}

u32 ufs_file_size(const char* path) {
    if (!mounted) return 0;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) return 0;
    
    FSNode e;
    if (find_in_dir(parent_block, name, &e) != 0) return 0;
    if (e.is_dir) return 0;
    
    return e.size;
}

int ufs_readdir(const char* path, FSNode** entries, u32* count) {
    if (!mounted) return -1;
    
    u32 dir_block;
    
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
        dir_block = sb.root_dir;
    } else {
        dir_block = resolve_path(path);
        if (dir_block == 0) return -1;
    }
    
    u32 total = 0;
    u32 current = dir_block;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] != 0) total++;
        }
        current = e[0].next_block;
    }
    
    *entries = kmalloc(total * sizeof(FSNode));
    if (!*entries) return -1;
    
    u32 idx = 0;
    current = dir_block;
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) {
            kfree(*entries);
            return -1;
        }
        
        FSNode* e = (FSNode*)buf;
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] != 0) {
                memcpy(&(*entries)[idx++], &e[i], sizeof(FSNode));
            }
        }
        current = e[0].next_block;
    }
    
    *count = total;
    return 0;
}

int ufs_delete(const char* path) {
    if (!mounted) return -1;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    
    if (get_parent(path, &parent, name) != 0) return -1;
    
    FSNode e;
    if (find_in_dir(parent, name, &e) != 0) return -1;
    if (e.is_dir) return -1;
    
    if (e.first_block != 0) {
        u32 blocks = (e.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
        for (u32 i = 0; i < blocks; i++) {
            free_block(e.first_block + i);
        }
    }
    
    return remove_from_dir(parent, name);
}

int ufs_rmdir(const char* path) {
    if (!mounted) return -1;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    
    if (get_parent(path, &parent, name) != 0) return -1;
    
    FSNode e;
    if (find_in_dir(parent, name, &e) != 0) return -1;
    if (!e.is_dir) return -1;
    
    u32 current = e.first_block;
    while (current != 0) {
        u32 next = 0;
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) == 0) {
            FSNode* be = (FSNode*)buf;
            
            for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
                if (be[i].name[0] != 0) return -1;
            }
            
            next = be[0].next_block;
        }
        free_block(current);
        current = next;
    }
    
    return remove_from_dir(parent, name);
}

int ufs_rmdir_force(const char* path) {
    if (!mounted) return -1;
    
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir(path, &entries, &count) != 0) return -1;
    
    for (u32 i = 0; i < count; i++) {
        char full[UFS_MAX_PATH];
        
        if (strcmp(path, "/") == 0) {
            snprintf(full, sizeof(full), "/%s", entries[i].name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", path, entries[i].name);
        }
        
        if (entries[i].is_dir) {
            ufs_rmdir_force(full);
        } else {
            ufs_delete(full);
        }
    }
    
    if (entries) kfree(entries);
    return ufs_rmdir(path);
}

int ufs_exists(const char* path) {
    if (!mounted) return 0;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return 1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return 0;
    
    FSNode e;
    return (find_in_dir(parent, name, &e) == 0);
}

int ufs_isdir(const char* path) {
    if (!mounted) return 0;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return 1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return 0;
    
    FSNode e;
    if (find_in_dir(parent, name, &e) != 0) return 0;
    return e.is_dir;
}

int ufs_stat(u32* total, u32* used, u32* free) {
    if (!mounted) return -1;
    *total = sb.total_blocks * UFS_BLOCK_SIZE;
    *free = sb.free_blocks * UFS_BLOCK_SIZE;
    *used = *total - *free;
    return 0;
}

int ufs_cp(const char* src, const char* dst) {
    u8* data;
    u32 size;
    if (ufs_read(src, &data, &size) != 0) return -1;
    int ret = ufs_write(dst, data, size);
    kfree(data);
    return ret;
}

int ufs_mv(const char* src, const char* dst) {
    if (ufs_cp(src, dst) != 0) return -1;
    return ufs_delete(src);
}
