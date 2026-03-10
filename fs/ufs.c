#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../include/io.h"

#define UFS_MAGIC 0x55465300
#define UFS_BLOCK_SIZE 512
#define UFS_BLOCK_OFFSET 2048

typedef struct {
    u32 magic;
    u32 total_blocks;
    u32 free_blocks;
    u32 root_dir;
    u32 reserved[125];
} ufs_superblock_t;

typedef struct {
    char name[56];
    u32 size;
    u32 first_block;
    u32 created;
    u32 modified;
    u8 is_dir;
    u8 reserved[55];
} ufs_entry_t;

static ufs_superblock_t sb;
static int mounted = 0;
static u32 partition_start = 0;

static void debug_putc(char c) { outb(0xE9, c); }
static void debug_puts(const char* s) { while (*s) debug_putc(*s++); }

static void debug_puthex(u32 val) {
    char hex[] = "0123456789ABCDEF";
    debug_putc('0');
    debug_putc('x');
    debug_putc(hex[(val >> 28) & 0xF]);
    debug_putc(hex[(val >> 24) & 0xF]);
    debug_putc(hex[(val >> 20) & 0xF]);
    debug_putc(hex[(val >> 16) & 0xF]);
    debug_putc(hex[(val >> 12) & 0xF]);
    debug_putc(hex[(val >> 8) & 0xF]);
    debug_putc(hex[(val >> 4) & 0xF]);
    debug_putc(hex[val & 0xF]);
}

static int read_block(u32 block, u8 *buf) {
    u32 lba = partition_start + UFS_BLOCK_OFFSET + block;
    return disk_read(lba, buf);
}

static int write_block(u32 block, u8 *buf) {
    u32 lba = partition_start + UFS_BLOCK_OFFSET + block;
    return disk_write(lba, buf);
}

static u32 find_free_block(void) {
    for (u32 i = 2; i < sb.total_blocks; i++) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(i, buf) != 0) continue;
        
        int empty = 1;
        for (int j = 0; j < UFS_BLOCK_SIZE; j++) {
            if (buf[j] != 0) { empty = 0; break; }
        }
        if (empty) {
            sb.free_blocks--;
            return i;
        }
    }
    return 0;
}

static void split_path(const char *full, char *dir, char *name) {
    const char *last_slash = strrchr(full, '/');
    
    if (!last_slash) {
        strcpy(dir, "/");
        strcpy(name, full);
        return;
    }
    
    int dir_len = last_slash - full;
    if (dir_len == 0) {
        strcpy(dir, "/");
    } else {
        strncpy(dir, full, dir_len);
        dir[dir_len] = '\0';
    }
    strcpy(name, last_slash + 1);
}

static int find_in_dir(u32 dir_block, const char *name, ufs_entry_t *out) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    
    ufs_entry_t *entries = (ufs_entry_t*)buf;
    for (int i = 0; i < UFS_BLOCK_SIZE / sizeof(ufs_entry_t); i++) {
        if (entries[i].name[0] && strcmp((char*)entries[i].name, name) == 0) {
            if (out) *out = entries[i];
            return i;
        }
    }
    return -1;
}

int ufs_is_mounted(void) {
    return mounted;
}

static int add_to_dir(u32 dir_block, ufs_entry_t *entry) {
    u8 buf[UFS_BLOCK_SIZE];
    int res = read_block(dir_block, buf);
    if (res != 0) {
        debug_puts("add_to_dir: creating new directory block\n");
        memset(buf, 0, sizeof(buf));
    }
    
    ufs_entry_t *entries = (ufs_entry_t*)buf;
    int entries_per_block = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    // Ищем свободное место
    for (int i = 0; i < entries_per_block; i++) {
        if (entries[i].name[0] == 0) {
            debug_puts("add_to_dir: found free slot at "); debug_puthex(i); debug_puts("\n");
            entries[i] = *entry;
            return write_block(dir_block, buf);
        }
    }
    
    debug_puts("add_to_dir: no free slots\n");
    return -1;
}

static int remove_from_dir(u32 dir_block, const char *name) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    
    ufs_entry_t *entries = (ufs_entry_t*)buf;
    for (int i = 0; i < UFS_BLOCK_SIZE / sizeof(ufs_entry_t); i++) {
        if (entries[i].name[0] && strcmp((char*)entries[i].name, name) == 0) {
            memset(&entries[i], 0, sizeof(ufs_entry_t));
            return write_block(dir_block, buf);
        }
    }
    return -1;
}

static int get_dir_block(const char *path, u32 *block, char *name) {
    char dir_path[256];
    split_path(path, dir_path, name);
    
    if (strcmp(dir_path, "/") == 0) {
        *block = sb.root_dir;
        return 0;
    }
    
    const char *search = dir_path;
    if (search[0] == '/') search++;
    
    ufs_entry_t dir_entry;
    if (find_in_dir(sb.root_dir, search, &dir_entry) != 0) {
        return -1;
    }
    
    if (!dir_entry.is_dir) return -1;
    *block = dir_entry.first_block;
    return 0;
}

int ufs_mount(u32 start_lba) {
    partition_start = start_lba;
    
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(0, buf) != 0) return -1;
    
    memcpy(&sb, buf, sizeof(sb));
    if (sb.magic != UFS_MAGIC) return -1;
    
    mounted = 1;
    return 0;
}

int ufs_format(u32 start_lba, u32 blocks) {
    partition_start = start_lba;
    
    memset(&sb, 0, sizeof(sb));
    sb.magic = UFS_MAGIC;
    sb.total_blocks = blocks;
    sb.free_blocks = blocks - 2;
    sb.root_dir = 1;
    
    u8 buf[UFS_BLOCK_SIZE];
    memcpy(buf, &sb, sizeof(sb));
    if (write_block(0, buf) != 0) return -1;
    
    memset(buf, 0, sizeof(buf));
    ufs_entry_t *entries = (ufs_entry_t*)buf;
    
    strcpy((char*)entries[0].name, ".");
    entries[0].is_dir = 1;
    entries[0].first_block = sb.root_dir;
    
    strcpy((char*)entries[1].name, "..");
    entries[1].is_dir = 1;
    entries[1].first_block = sb.root_dir;
    
    if (write_block(sb.root_dir, buf) != 0) return -1;
    
    mounted = 1;
    return 0;
}

int ufs_write(const char *path, u8 *data, u32 size) {
    if (!mounted) return -1;
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) return -1;
    
    ufs_entry_t old;
    if (find_in_dir(parent_block, name, &old) == 0) {
        if (old.is_dir) return -1;
        remove_from_dir(parent_block, name);
    }
    
    if (size == 0) {
        ufs_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        strcpy((char*)entry.name, name);
        entry.size = 0;
        entry.first_block = 0;
        entry.is_dir = 0;
        
        if (add_to_dir(parent_block, &entry) != 0) return -1;
        
        u8 sb_buf[UFS_BLOCK_SIZE];
        memcpy(sb_buf, &sb, sizeof(sb));
        write_block(0, sb_buf);
        return 0;
    }
    
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    if (sb.free_blocks < blocks) return -1;
    
    u32 first_block = 0;
    
    for (u32 i = 0; i < blocks; i++) {
        u32 new_block = find_free_block();
        if (new_block == 0) return -1;
        
        if (i == 0) first_block = new_block;
        
        u8 buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        if (offset < size) {
            u32 chunk = size - offset;
            if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
            memcpy(buf, data + offset, chunk);
        }
        
        if (write_block(new_block, buf) != 0) return -1;
    }
    
    ufs_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strcpy((char*)entry.name, name);
    entry.size = size;
    entry.first_block = first_block;
    entry.is_dir = 0;
    
    if (add_to_dir(parent_block, &entry) != 0) return -1;
    
    u8 sb_buf[UFS_BLOCK_SIZE];
    memcpy(sb_buf, &sb, sizeof(sb));
    write_block(0, sb_buf);
    
    return 0;
}

int ufs_read(const char *path, u8 **data, u32 *size) {
    if (!mounted) return -1;
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) return -1;
    
    ufs_entry_t entry;
    if (find_in_dir(parent_block, name, &entry) != 0) return -1;
    if (entry.is_dir) return -1;
    
    *size = entry.size;
    *data = kmalloc(entry.size + 1);
    if (!*data) return -1;
    
    if (entry.size == 0) {
        **data = '\0';
        return 0;
    }
    
    u32 blocks = (entry.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    u32 block = entry.first_block;
    
    for (u32 i = 0; i < blocks; i++) {
        if (read_block(block + i, *data + i * UFS_BLOCK_SIZE) != 0) {
            kfree(*data);
            return -1;
        }
    }
    
    (*data)[entry.size] = '\0';
    return 0;
}

int ufs_delete(const char *path) {
    if (!mounted) return -1;
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) return -1;
    
    ufs_entry_t entry;
    if (find_in_dir(parent_block, name, &entry) != 0) return -1;
    if (entry.is_dir) return -1;
    
    if (remove_from_dir(parent_block, name) != 0) return -1;
    
    u8 sb_buf[UFS_BLOCK_SIZE];
    memcpy(sb_buf, &sb, sizeof(sb));
    write_block(0, sb_buf);
    
    return 0;
}

int ufs_mkdir(const char *path) {
    if (!mounted) return -1;
    if (!path || strlen(path) == 0) return -1;
    if (strcmp(path, "/") == 0) return -1;
    
    debug_puts("ufs_mkdir: "); debug_puts(path); debug_puts("\n");
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) {
        debug_puts("  get_dir_block failed\n");
        return -1;
    }
    
    debug_puts("  parent_block="); debug_puthex(parent_block); 
    debug_puts(" name="); debug_puts(name); debug_puts("\n");
    
    // Проверяем что не существует
    if (find_in_dir(parent_block, name, NULL) >= 0) {
        debug_puts("  already exists\n");
        return -1;
    }
    
    // Выделяем блок для новой директории
    u32 new_block = find_free_block();
    if (new_block == 0) {
        debug_puts("  no free block\n");
        return -1;
    }
    
    debug_puts("  new_block="); debug_puthex(new_block); debug_puts("\n");
    
    // Создаем новую директорию с . и ..
    u8 buf[UFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    
    ufs_entry_t *new_entries = (ufs_entry_t*)buf;
    
    // .
    strcpy((char*)new_entries[0].name, ".");
    new_entries[0].is_dir = 1;
    new_entries[0].first_block = new_block;
    new_entries[0].size = 0;
    
    // ..
    strcpy((char*)new_entries[1].name, "..");
    new_entries[1].is_dir = 1;
    new_entries[1].first_block = parent_block;
    new_entries[1].size = 0;
    
    if (write_block(new_block, buf) != 0) {
        debug_puts("  write_block failed\n");
        sb.free_blocks++;
        return -1;
    }
    
    // Создаем запись в родительской директории
    ufs_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strcpy((char*)entry.name, name);
    entry.is_dir = 1;
    entry.first_block = new_block;
    entry.size = 0;
    
    if (add_to_dir(parent_block, &entry) != 0) {
        debug_puts("  add_to_dir failed\n");
        sb.free_blocks++;
        return -1;
    }
    
    // Обновляем суперблок
    u8 sb_buf[UFS_BLOCK_SIZE];
    memcpy(sb_buf, &sb, sizeof(sb));
    if (write_block(0, sb_buf) != 0) {
        debug_puts("  write_sb failed\n");
        return -1;
    }
    
    debug_puts("  OK\n");
    return 0;
}

int ufs_rmdir(const char *path) {
    if (!mounted) return -1;
    if (strcmp(path, "/") == 0) return -1;
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) return -1;
    
    ufs_entry_t entry;
    if (find_in_dir(parent_block, name, &entry) != 0) return -1;
    if (!entry.is_dir) return -1;
    
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(entry.first_block, buf) != 0) return -1;
    
    ufs_entry_t *sub_entries = (ufs_entry_t*)buf;
    for (int i = 2; i < UFS_BLOCK_SIZE / sizeof(ufs_entry_t); i++) {
        if (sub_entries[i].name[0]) return -1;
    }
    
    if (remove_from_dir(parent_block, name) != 0) return -1;
    sb.free_blocks++;
    
    u8 sb_buf[UFS_BLOCK_SIZE];
    memcpy(sb_buf, &sb, sizeof(sb));
    write_block(0, sb_buf);
    
    return 0;
}

int ufs_readdir(const char *path, FSNode **entries, u32 *count) {
    if (!mounted) return -1;
    
    u32 dir_block;
    if (strcmp(path, "/") == 0) {
        dir_block = sb.root_dir;
    } else {
        char name[56];
        if (get_dir_block(path, &dir_block, name) != 0) return -1;
    }
    
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(dir_block, buf) != 0) return -1;
    
    ufs_entry_t *raw = (ufs_entry_t*)buf;
    
    *count = 0;
    for (int i = 2; i < UFS_BLOCK_SIZE / sizeof(ufs_entry_t); i++) {
        if (raw[i].name[0]) (*count)++;
    }
    
    if (*count == 0) {
        *entries = NULL;
        return 0;
    }
    
    *entries = kmalloc(*count * sizeof(FSNode));
    if (!*entries) return -1;
    
    int idx = 0;
    for (int i = 2; i < UFS_BLOCK_SIZE / sizeof(ufs_entry_t); i++) {
        if (raw[i].name[0]) {
            strcpy((*entries)[idx].name, (char*)raw[i].name);
            (*entries)[idx].size = raw[i].size;
            (*entries)[idx].is_dir = raw[i].is_dir;
            idx++;
        }
    }
    
    return 0;
}

int ufs_exists(const char *path) {
    if (!mounted) return 0;
    if (strcmp(path, "/") == 0) return 1;
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) return 0;
    return (find_in_dir(parent_block, name, NULL) >= 0);
}

int ufs_isdir(const char *path) {
    if (!mounted) return 0;
    if (strcmp(path, "/") == 0) return 1;
    
    char name[56];
    u32 parent_block;
    
    if (get_dir_block(path, &parent_block, name) != 0) return 0;
    
    ufs_entry_t entry;
    if (find_in_dir(parent_block, name, &entry) != 0) return 0;
    return entry.is_dir;
}

int ufs_stat(u32 *total, u32 *used, u32 *free) {
    if (!mounted) return -1;
    *total = sb.total_blocks * UFS_BLOCK_SIZE;
    *free = sb.free_blocks * UFS_BLOCK_SIZE;
    *used = *total - *free;
    return 0;
}

int ufs_cp(const char *src, const char *dst) {
    u8 *data;
    u32 size;
    
    if (ufs_read(src, &data, &size) != 0) return -1;
    int ret = ufs_write(dst, data, size);
    kfree(data);
    return ret;
}

int ufs_mv(const char *src, const char *dst) {
    if (ufs_cp(src, dst) != 0) return -1;
    return ufs_delete(src);
}

int ufs_grep(const char *path, const char *pattern, void (*callback)(const char *line, u32 line_num)) {
    u8 *data;
    u32 size;
    
    if (ufs_read(path, &data, &size) != 0) return -1;
    
    char *p = (char*)data;
    char line[256];
    u32 line_num = 1;
    u32 line_pos = 0;
    
    while (*p && line_num < 1000) {
        if (*p == '\n' || *p == '\r') {
            line[line_pos] = '\0';
            if (strstr(line, pattern)) {
                callback(line, line_num);
            }
            line_pos = 0;
            line_num++;
            if (*p == '\r' && *(p+1) == '\n') p++;
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

static void find_recursive(const char *start_path, const char *name, void (*callback)(const char *path)) {
    FSNode *entries;
    u32 count;
    
    if (ufs_readdir(start_path, &entries, &count) != 0) return;
    
    for (u32 i = 0; i < count; i++) {
        char full_path[256];
        if (strcmp(start_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", entries[i].name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", start_path, entries[i].name);
        }
        
        if (strcmp(entries[i].name, name) == 0) {
            callback(full_path);
        }
        
        if (entries[i].is_dir) {
            find_recursive(full_path, name, callback);
        }
    }
    
    if (entries) kfree(entries);
}

int ufs_find(const char *start_path, const char *name, void (*callback)(const char *path)) {
    find_recursive(start_path, name, callback);
    return 0;
}

int ufs_chmod(const char *path, u16 mode) {
    (void)path;
    (void)mode;
    return 0; // UFS пока не поддерживает права
}

int ufs_chown(const char *path, u16 uid, u16 gid) {
    (void)path;
    (void)uid;
    (void)gid;
    return 0; // UFS пока не поддерживает владельцев
}
