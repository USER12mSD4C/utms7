#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"

#define UFS_MAGIC 0x55465300
#define UFS_BLOCK_SIZE 512

typedef struct {
    u32 magic;
    u32 total_blocks;
    u32 free_blocks;
    u32 root_dir;
} __attribute__((packed)) ufs_superblock_t;

typedef FSNode ufs_entry_t;

static ufs_superblock_t sb;
static int mounted = 0;
static u32 part_start = 0;

static int read_block(u32 b, u8* buf) {
    return disk_read(part_start + b, buf);
}

static int write_block(u32 b, u8* buf) {
    return disk_write(part_start + b, buf);
}

static int save_superblock(void) {
    u8 buf[UFS_BLOCK_SIZE] = {0};
    memcpy(buf, &sb, sizeof(sb));
    return write_block(0, buf);
}

static u32 find_free_block(void) {
    for (u32 i = 2; i < sb.total_blocks; i++) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(i, buf) != 0) continue;
        
        int empty = 1;
        for (int j = 0; j < UFS_BLOCK_SIZE; j++) {
            if (buf[j]) { 
                empty = 0; 
                break; 
            }
        }
        
        if (empty) {
            sb.free_blocks--;
            save_superblock();
            return i;
        }
    }
    return 0;
}

static void split_path(const char* full, char* dir, char* name) {
    const char* last = strrchr(full, '/');
    if (!last) {
        strcpy(dir, "/");
        strcpy(name, full);
        return;
    }
    int dlen = last - full;
    if (dlen == 0) {
        strcpy(dir, "/");
    } else {
        strncpy(dir, full, dlen);
        dir[dlen] = '\0';
    }
    strcpy(name, last + 1);
}

static int find_in_dir(u32 block, const char* name, ufs_entry_t* out) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    
    ufs_entry_t* e = (ufs_entry_t*)buf;
    int count = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    for (int i = 0; i < count; i++) {
        if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
            if (out) *out = e[i];
            return i;
        }
    }
    return -1;
}

static int add_to_dir(u32 block, ufs_entry_t* e) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) {
        memset(buf, 0, UFS_BLOCK_SIZE);
    }
    
    ufs_entry_t* entries = (ufs_entry_t*)buf;
    int count = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    for (int i = 2; i < count; i++) {
        if (!entries[i].name[0]) {
            entries[i] = *e;
            return write_block(block, buf);
        }
    }
    return -1;
}

static int remove_from_dir(u32 block, const char* name) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    
    ufs_entry_t* entries = (ufs_entry_t*)buf;
    int count = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    for (int i = 2; i < count; i++) {
        if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
            memset(&entries[i], 0, sizeof(ufs_entry_t));
            return write_block(block, buf);
        }
    }
    return -1;
}

static int get_parent(const char* path, u32* block, char* name) {
    if (strcmp(path, "/") == 0) {
        return -1;
    }
    
    char dir[256];
    split_path(path, dir, name);
    
    if (strcmp(dir, "/") == 0) {
        *block = sb.root_dir;
        return 0;
    }
    
    char temp[256];
    strcpy(temp, dir);
    char* part = strtok(temp, "/");
    u32 current = sb.root_dir;
    
    while (part) {
        ufs_entry_t e;
        if (find_in_dir(current, part, &e) != 0) return -1;
        if (!e.is_dir) return -1;
        current = e.first_block;
        part = strtok(NULL, "/");
    }
    
    *block = current;
    return 0;
}

int ufs_mount(u32 start_lba) {
    part_start = start_lba;
    u8 buf[UFS_BLOCK_SIZE];
    
    if (disk_read(start_lba, buf) != 0) return -1;
    memcpy(&sb, buf, sizeof(sb));
    
    if (sb.magic != UFS_MAGIC) return -1;
    
    mounted = 1;
    return 0;
}

int ufs_format(u32 start_lba, u32 blocks) {
    part_start = start_lba;
    
    memset(&sb, 0, sizeof(sb));
    sb.magic = UFS_MAGIC;
    sb.total_blocks = blocks;
    sb.free_blocks = blocks - 2;
    sb.root_dir = 1;
    
    u8 buf[UFS_BLOCK_SIZE];
    memcpy(buf, &sb, sizeof(sb));
    if (disk_write(start_lba, buf) != 0) return -1;
    
    memset(buf, 0, UFS_BLOCK_SIZE);
    ufs_entry_t* e = (ufs_entry_t*)buf;
    
    strcpy(e[0].name, ".");
    e[0].size = 0;
    e[0].first_block = 1;
    e[0].is_dir = 1;
    
    strcpy(e[1].name, "..");
    e[1].size = 0;
    e[1].first_block = 1;
    e[1].is_dir = 1;
    
    if (disk_write(start_lba + 1, buf) != 0) return -1;
    
    mounted = 1;
    return 0;
}

int ufs_read(const char* path, u8** data, u32* size) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    
    ufs_entry_t e;
    if (find_in_dir(parent, name, &e) != 0) return -1;
    if (e.is_dir) return -1;
    
    *size = e.size;
    *data = kmalloc(e.size + 1);
    if (!*data) return -1;
    
    if (e.size > 0) {
        u32 block = e.first_block;
        u32 left = e.size;
        u32 pos = 0;
        
        while (left > 0) {
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
    }
    
    (*data)[e.size] = '\0';
    return 0;
}

int ufs_write(const char* path, u8* data, u32 size) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    
    ufs_entry_t parent_check;
    if (find_in_dir(parent, ".", &parent_check) != 0) return -1;
    
    ufs_entry_t old;
    if (find_in_dir(parent, name, &old) == 0) {
        if (old.is_dir) return -1;
        
        u32 block = old.first_block;
        u32 blocks = (old.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
        for (u32 i = 0; i < blocks; i++) {
            u8 zero[UFS_BLOCK_SIZE] = {0};
            write_block(block + i, zero);
            sb.free_blocks++;
        }
        
        remove_from_dir(parent, name);
        save_superblock();
    }
    
    if (size == 0) {
        ufs_entry_t ne;
        memset(&ne, 0, sizeof(ne));
        strcpy(ne.name, name);
        ne.size = 0;
        ne.first_block = 0;
        ne.is_dir = 0;
        return add_to_dir(parent, &ne);
    }
    
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    if (sb.free_blocks < blocks) return -1;
    
    u32 first = 0;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = find_free_block();
        if (!b) return -1;
        
        if (i == 0) first = b;
        
        u8 buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        u32 chunk = size - offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        
        if (data) {
            memcpy(buf, data + offset, chunk);
        }
        
        if (write_block(b, buf) != 0) return -1;
    }
    
    ufs_entry_t ne;
    memset(&ne, 0, sizeof(ne));
    strcpy(ne.name, name);
    ne.size = size;
    ne.first_block = first;
    ne.is_dir = 0;
    
    return add_to_dir(parent, &ne);
}

int ufs_mkdir(const char* path) {
    if (!mounted) return -1;
    if (strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    
    if (find_in_dir(parent, name, NULL) >= 0) return -1;
    
    u32 nb = find_free_block();
    if (!nb) return -1;
    
    u8 buf[UFS_BLOCK_SIZE];
    memset(buf, 0, UFS_BLOCK_SIZE);
    ufs_entry_t* e = (ufs_entry_t*)buf;
    
    strcpy(e[0].name, ".");
    e[0].size = 0;
    e[0].first_block = nb;
    e[0].is_dir = 1;
    
    strcpy(e[1].name, "..");
    e[1].size = 0;
    e[1].first_block = parent;
    e[1].is_dir = 1;
    
    if (write_block(nb, buf) != 0) return -1;
    
    // Проверяем что записалось
    u8 check[UFS_BLOCK_SIZE];
    if (read_block(nb, check) != 0) return -1;
    
    ufs_entry_t* ce = (ufs_entry_t*)check;
    if (ce[0].name[0] != '.' || ce[1].name[0] != '.') return -1;
    
    ufs_entry_t ne;
    memset(&ne, 0, sizeof(ne));
    strcpy(ne.name, name);
    ne.size = 0;
    ne.first_block = nb;
    ne.is_dir = 1;
    
    return add_to_dir(parent, &ne);
}

int ufs_readdir(const char* path, FSNode** entries, u32* count) {
    if (!mounted) return -1;
    
    u32 block;
    if (strcmp(path, "/") == 0) {
        block = sb.root_dir;
    } else {
        char name[UFS_MAX_NAME];
        u32 parent;
        if (get_parent(path, &parent, name) != 0) return -1;
        
        // Ищем запись напрямую, без промежуточной структуры
        u8 tmp[UFS_BLOCK_SIZE];
        if (read_block(parent, tmp) != 0) return -1;
        ufs_entry_t* tmp_e = (ufs_entry_t*)tmp;
        int total = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
        
        u32 found_block = 0;
        int found_dir = 0;
        for (int i = 2; i < total; i++) {
            if (tmp_e[i].name[0] && strcmp(tmp_e[i].name, name) == 0) {
                found_block = tmp_e[i].first_block;
                found_dir = tmp_e[i].is_dir;
                break;
            }
        }
        
        if (found_block == 0) return -1;
        if (!found_dir) return -1;
        block = found_block;
    }
    
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    
    ufs_entry_t* raw = (ufs_entry_t*)buf;
    int total = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    *count = 0;
    for (int i = 2; i < total; i++) {
        if (raw[i].name[0] != 0) (*count)++;
    }
    
    if (*count == 0) {
        *entries = NULL;
        return 0;
    }
    
    *entries = kmalloc(*count * sizeof(FSNode));
    int idx = 0;
    
    for (int i = 2; i < total; i++) {
        if (raw[i].name[0] != 0) {
            strcpy((*entries)[idx].name, raw[i].name);
            (*entries)[idx].size = raw[i].size;
            (*entries)[idx].first_block = raw[i].first_block;
            (*entries)[idx].is_dir = raw[i].is_dir;
            idx++;
        }
    }
    
    return 0;
}

int ufs_exists(const char* path) {
    if (!mounted) return 0;
    if (strcmp(path, "/") == 0) return 1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return 0;
    
    return find_in_dir(parent, name, NULL) >= 0;
}

int ufs_isdir(const char* path) {
    if (!mounted) return 0;
    if (strcmp(path, "/") == 0) return 1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return 0;
    
    ufs_entry_t e;
    if (find_in_dir(parent, name, &e) != 0) return 0;
    
    return e.is_dir;
}

int ufs_rmdir(const char* path) {
    if (!mounted) return -1;
    if (strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    
    // Ищем запись напрямую
    u8 tmp[UFS_BLOCK_SIZE];
    if (read_block(parent, tmp) != 0) return -1;
    ufs_entry_t* tmp_e = (ufs_entry_t*)tmp;
    int total = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    u32 found_block = 0;
    int found_idx = -1;
    for (int i = 2; i < total; i++) {
        if (tmp_e[i].name[0] && strcmp(tmp_e[i].name, name) == 0) {
            found_block = tmp_e[i].first_block;
            found_idx = i;
            if (!tmp_e[i].is_dir) return -1;
            break;
        }
    }
    
    if (found_block == 0 || found_idx == -1) return -1;
    
    // Проверяем что директория пуста
    u8 dir_buf[UFS_BLOCK_SIZE];
    if (read_block(found_block, dir_buf) != 0) return -1;
    ufs_entry_t* dir_e = (ufs_entry_t*)dir_buf;
    
    for (int i = 2; i < total; i++) {
        if (dir_e[i].name[0] != 0) return -1;
    }
    
    // Удаляем запись из родителя
    memset(&tmp_e[found_idx], 0, sizeof(ufs_entry_t));
    if (write_block(parent, tmp) != 0) return -1;
    
    // Освобождаем блок директории
    u8 zero[UFS_BLOCK_SIZE] = {0};
    write_block(found_block, zero);
    sb.free_blocks++;
    save_superblock();
    
    return 0;
}

int ufs_delete(const char* path) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    
    // Ищем запись напрямую
    u8 tmp[UFS_BLOCK_SIZE];
    if (read_block(parent, tmp) != 0) return -1;
    ufs_entry_t* tmp_e = (ufs_entry_t*)tmp;
    int total = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    u32 found_block = 0;
    u32 found_size = 0;
    int found_idx = -1;
    for (int i = 2; i < total; i++) {
        if (tmp_e[i].name[0] && strcmp(tmp_e[i].name, name) == 0) {
            found_block = tmp_e[i].first_block;
            found_size = tmp_e[i].size;
            found_idx = i;
            if (tmp_e[i].is_dir) return -1;
            break;
        }
    }
    
    if (found_block == 0 || found_idx == -1) return -1;
    
    // Освобождаем блоки файла
    if (found_block != 0) {
        u32 blocks = (found_size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
        for (u32 i = 0; i < blocks; i++) {
            u8 zero[UFS_BLOCK_SIZE] = {0};
            write_block(found_block + i, zero);
            sb.free_blocks++;
        }
    }
    
    // Удаляем запись из родителя
    memset(&tmp_e[found_idx], 0, sizeof(ufs_entry_t));
    save_superblock();
    return write_block(parent, tmp);
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

int ufs_grep(const char* path, const char* pattern, void (*callback)(const char* line, u32 line_num)) {
    u8* data;
    u32 size;
    if (ufs_read(path, &data, &size) != 0) return -1;
    
    char* p = (char*)data;
    char line[256];
    u32 line_num = 1;
    u32 line_pos = 0;
    
    while (*p) {
        if (*p == '\n') {
            line[line_pos] = '\0';
            if (strstr(line, pattern)) {
                callback(line, line_num);
            }
            line_pos = 0;
            line_num++;
        } else if (line_pos < 255) {
            line[line_pos++] = *p;
        }
        p++;
    }
    
    if (line_pos > 0) {
        line[line_pos] = '\0';
        if (strstr(line, pattern)) {
            callback(line, line_num);
        }
    }
    
    kfree(data);
    return 0;
}

static void find_recursive(const char* start, const char* name, void (*callback)(const char* path)) {
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir(start, &entries, &count) != 0) return;
    
    for (u32 i = 0; i < count; i++) {
        char full[256];
        
        if (strcmp(start, "/") == 0) {
            snprintf(full, sizeof(full), "/%s", entries[i].name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", start, entries[i].name);
        }
        
        if (strcmp(entries[i].name, name) == 0) {
            callback(full);
        }
        
        if (entries[i].is_dir) {
            find_recursive(full, name, callback);
        }
    }
    
    kfree(entries);
}

int ufs_find(const char* start, const char* name, void (*callback)(const char* path)) {
    find_recursive(start, name, callback);
    return 0;
}

int ufs_chmod(const char* path, u16 mode) {
    (void)path; (void)mode;
    return 0;
}

int ufs_chown(const char* path, u16 uid, u16 gid) {
    (void)path; (void)uid; (void)gid;
    return 0;
}
