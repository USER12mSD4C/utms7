#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../kernel/panic.h"

#define UFS_MAGIC 0x55465300
#define SUPERBLOCK_BLOCK 0
#define ROOTDIR_BLOCK 1
#define ENTRIES_PER_BLOCK (UFS_BLOCK_SIZE / sizeof(FSNode))

#define UFS_DEBUG 1

#ifdef UFS_DEBUG
#include "../include/stdarg.h"
static void ufs_log(const char* fmt, ...) {
    char buf[256];
    char* p = buf;
    *p++ = '[';
    *p++ = 'U';
    *p++ = 'F';
    *p++ = 'S';
    *p++ = ']';
    *p++ = ' ';
    
    va_list args;
    va_start(args, fmt);
    int len = 0;
    char* f = (char*)fmt;
    while (*f && len < 200) {
        if (*f == '%' && *(f+1) == 'd') {
            u32 num = va_arg(args, u32);
            char numbuf[16];
            int ni = 0;
            if (num == 0) {
                buf[p - buf + len++] = '0';
            } else {
                char tmp[16];
                int ti = 0;
                while (num > 0) {
                    tmp[ti++] = '0' + (num % 10);
                    num /= 10;
                }
                while (ti > 0) {
                    buf[p - buf + len++] = tmp[--ti];
                }
            }
            f += 2;
        } else if (*f == '%' && *(f+1) == 's') {
            char* s = va_arg(args, char*);
            while (*s && len < 200) {
                buf[p - buf + len++] = *s++;
            }
            f += 2;
        } else if (*f == '%' && *(f+1) == 'x') {
            u32 num = va_arg(args, u32);
            char hex[] = "0123456789ABCDEF";
            buf[p - buf + len++] = '0';
            buf[p - buf + len++] = 'x';
            for (int i = 28; i >= 0; i -= 4) {
                buf[p - buf + len++] = hex[(num >> i) & 0xF];
            }
            f += 2;
        } else {
            buf[p - buf + len++] = *f++;
        }
    }
    va_end(args);
    
    buf[p - buf + len] = '\0';
    vga_write(buf);
}
#else
#define ufs_log(...)
#endif

typedef struct {
    u32 magic;
    u32 total_blocks;
    u32 free_blocks;
    u32 root_dir;
} __attribute__((packed)) ufs_superblock_t;

static ufs_superblock_t sb;
static int mounted = 0;
static u32 part_start = 0;

static int read_block(u32 b, u8* buf) {
    if (!buf) return -1;
    return disk_read(part_start + b, buf);
}

static int write_block(u32 b, u8* buf) {
    if (!buf) return -1;
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

static int read_all_entries(u32 dir_block, FSNode** entries, u32* count) {
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
    
    if (total == 0) {
        *entries = NULL;
        *count = 0;
        return 0;
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
    ufs_log("add_to_dir: block=%d name=%s\n", dir_block, new_entry->name);
    
    if (!new_entry || !new_entry->name[0]) return -1;
    
    u32 current = dir_block;
    u32 last = 0;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) {
            ufs_log("  read failed %d\n", current);
            return -1;
        }
        
        FSNode* e = (FSNode*)buf;
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, new_entry->name) == 0) {
                ufs_log("  already exists\n");
                return -1;
            }
        }
        
        last = current;
        current = e[0].next_block;
    }
    
    current = dir_block;
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] == 0) {
                ufs_log("  free slot at %d[%d]\n", current, i);
                memcpy(&e[i], new_entry, sizeof(FSNode));
                return write_block(current, buf);
            }
        }
        
        current = e[0].next_block;
    }
    
    ufs_log("  need new block\n");
    u32 new_block = find_free_block();
    if (!new_block) {
        ufs_log("  no free blocks\n");
        return -1;
    }
    
    u8 buf[UFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    FSNode* e = (FSNode*)buf;
    
    strcpy(e[0].name, ".");
    e[0].first_block = new_block;
    e[0].is_dir = 1;
    e[0].size = 0;
    e[0].next_block = 0;
    
    strcpy(e[1].name, "..");
    e[1].first_block = dir_block;
    e[1].is_dir = 1;
    e[1].size = 0;
    e[1].next_block = 0;
    
    memcpy(&e[2], new_entry, sizeof(FSNode));
    
    if (write_block(new_block, buf) != 0) {
        free_block(new_block);
        return -1;
    }
    
    if (last != 0) {
        u8 last_buf[UFS_BLOCK_SIZE];
        if (read_block(last, last_buf) != 0) {
            return -1;
        }
        FSNode* last_e = (FSNode*)last_buf;
        last_e[0].next_block = new_block;
        if (write_block(last, last_buf) != 0) {
            return -1;
        }
    }
    
    ufs_log("  added, new block=%d\n", new_block);
    return 0;
}

static int remove_from_dir(u32 dir_block, const char* name) {
    ufs_log("remove_from_dir: block=%d name=%s\n", dir_block, name);
    
    u32 current = dir_block;
    
    while (current != 0) {
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) return -1;
        
        FSNode* e = (FSNode*)buf;
        
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
                ufs_log("  found at %d[%d]\n", current, i);
                memset(&e[i], 0, sizeof(FSNode));
                return write_block(current, buf);
            }
        }
        
        current = e[0].next_block;
    }
    
    ufs_log("  not found\n");
    return -1;
}

static int get_parent(const char* path, u32* block, char* name) {
    ufs_log("get_parent: %s\n", path);
    
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
        return -1;
    }
    
    char dir[256];  // СВОЙ БУФЕР
    char temp[256]; // СВОЙ БУФЕР
    split_path(path, dir, name);
    
    ufs_log("  dir=%s name=%s\n", dir, name);
    
    if (strcmp(dir, "/") == 0) {
        *block = sb.root_dir;
        ufs_log("  root_dir=%d\n", sb.root_dir);
        return 0;
    }
    
    strcpy(temp, dir);  // КОПИРУЕМ В temp
    char* part = strtok(temp, "/");  // РАБОТАЕМ С temp
    u32 current = sb.root_dir;
    
    while (part) {
        ufs_log("  looking for %s in %d\n", part, current);
        
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) != 0) {
            ufs_log("  read_block failed\n");
            return -1;
        }
        
        FSNode* e = (FSNode*)buf;
        int found = 0;
        
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (e[i].name[0] && strcmp(e[i].name, part) == 0) {
                if (!e[i].is_dir) {
                    ufs_log("  not a dir\n");
                    return -1;
                }
                current = e[i].first_block;
                found = 1;
                ufs_log("  found, block=%d\n", current);
                break;
            }
        }
        
        if (!found) {
            ufs_log("  not found\n");
            return -1;
        }
        
        part = strtok(NULL, "/");
    }
    
    *block = current;
    return 0;
}

int ufs_mount(u32 start_lba) {
    ufs_log("ufs_mount: start_lba=%d\n", start_lba);
    
    part_start = start_lba;
    u8 buf[UFS_BLOCK_SIZE];
    
    if (disk_read(start_lba, buf) != 0) {
        ufs_log("  read failed\n");
        return -1;
    }
    
    memcpy(&sb, buf, sizeof(sb));
    
    if (sb.magic != UFS_MAGIC) {
        ufs_log("  bad magic: %x\n", sb.magic);
        return -1;
    }
    
    ufs_log("  total_blocks=%d free_blocks=%d\n", sb.total_blocks, sb.free_blocks);
    
    mounted = 1;
    return 0;
}

int ufs_format(u32 start_lba, u32 blocks) {
    ufs_log("ufs_format: start_lba=%d blocks=%d\n", start_lba, blocks);
    
    part_start = start_lba;
    
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
    e[0].size = 0;
    e[0].next_block = 0;
    
    strcpy(e[1].name, "..");
    e[1].first_block = ROOTDIR_BLOCK;
    e[1].is_dir = 1;
    e[1].size = 0;
    e[1].next_block = 0;
    
    if (disk_write(start_lba + ROOTDIR_BLOCK, buf) != 0) return -1;
    
    mounted = 1;
    return 0;
}

int ufs_mkdir(const char* path) {
    ufs_log("ufs_mkdir: %s\n", path);
    
    if (!mounted) {
        ufs_log("  not mounted\n");
        return -1;
    }
    
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
        ufs_log("  invalid path\n");
        return -1;
    }
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) {
        ufs_log("  get_parent failed\n");
        return -1;
    }
    
    if (name[0] == '\0') {
        ufs_log("  empty name\n");
        return -1;
    }
    
    ufs_log("  parent=%d name=%s\n", parent_block, name);
    
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(parent_block, buf) != 0) {
        ufs_log("  read parent failed\n");
        return -1;
    }
    
    FSNode* e = (FSNode*)buf;
    for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
        if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
            ufs_log("  already exists\n");
            return -1;
        }
    }
    
    u32 new_block = find_free_block();
    if (!new_block) {
        ufs_log("  no free blocks\n");
        return -1;
    }
    
    u8 new_buf[UFS_BLOCK_SIZE];
    memset(new_buf, 0, sizeof(new_buf));
    FSNode* new_e = (FSNode*)new_buf;
    
    strcpy(new_e[0].name, ".");
    new_e[0].first_block = new_block;
    new_e[0].is_dir = 1;
    new_e[0].size = 0;
    new_e[0].next_block = 0;
    
    strcpy(new_e[1].name, "..");
    new_e[1].first_block = parent_block;
    new_e[1].is_dir = 1;
    new_e[1].size = 0;
    new_e[1].next_block = 0;
    
    if (write_block(new_block, new_buf) != 0) {
        ufs_log("  write new dir failed\n");
        free_block(new_block);
        return -1;
    }
    
    FSNode entry;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, name);
    entry.first_block = new_block;
    entry.is_dir = 1;
    entry.size = 0;
    entry.next_block = 0;
    
    int ret = add_to_dir(parent_block, &entry);
    ufs_log("  add_to_dir returned %d\n", ret);
    
    return ret;
}

int ufs_write(const char* path, u8* data, u32 size) {
    ufs_log("ufs_write: %s size=%d\n", path, size);
    
    if (!mounted) {
        ufs_log("  not mounted\n");
        return -1;
    }
    
    if (!path || path[0] == '\0') {
        ufs_log("  invalid path\n");
        return -1;
    }
    
    // Проверяем существование файла
    int file_exists = ufs_exists(path) && !ufs_isdir(path);
    
    // Если файл существует - пробуем удалить
    if (file_exists) {
        ufs_log("  file exists, deleting\n");
        if (ufs_delete(path) != 0) {
            ufs_log("  failed to delete existing file\n");
            // НЕ ПАДАЕМ, просто идём дальше
        }
    }
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (get_parent(path, &parent_block, name) != 0) {
        ufs_log("  get_parent failed\n");
        return -1;
    }
    
    if (name[0] == '\0') {
        ufs_log("  empty name\n");
        return -1;
    }
    
    // Проверяем что родительский блок валидный
    if (parent_block == 0 || parent_block >= sb.total_blocks) {
        ufs_log("  invalid parent block\n");
        return -1;
    }
    
    ufs_log("  parent=%d name=%s\n", parent_block, name);
    
    u8 parent_buf[UFS_BLOCK_SIZE];
    if (read_block(parent_block, parent_buf) != 0) {
        ufs_log("  read parent failed\n");
        return -1;
    }
    
    // Если size == 0 - создаём пустой файл
    if (size == 0) {
        FSNode entry;
        memset(&entry, 0, sizeof(entry));
        strcpy(entry.name, name);
        entry.size = 0;
        entry.first_block = 0;
        entry.is_dir = 0;
        entry.next_block = 0;
        
        // Проверяем нет ли уже такой записи
        FSNode* parent_e = (FSNode*)parent_buf;
        for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
            if (parent_e[i].name[0] && strcmp(parent_e[i].name, name) == 0) {
                // Уже есть пустой файл - ок
                return 0;
            }
        }
        
        return add_to_dir(parent_block, &entry);
    }
    
    // Выделяем блоки под данные
    u32 blocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    ufs_log("  need %d blocks\n", blocks);
    
    if (sb.free_blocks < blocks) {
        ufs_log("  not enough free blocks: have %d need %d\n", sb.free_blocks, blocks);
        return -1;
    }
    
    u32 first_block = 0;
    for (u32 i = 0; i < blocks; i++) {
        u32 b = find_free_block();
        if (!b) {
            ufs_log("  failed to allocate block %d\n", i);
            // Откатываем уже выделенные
            for (u32 j = 0; j < i; j++) {
                free_block(first_block + j);
            }
            return -1;
        }
        
        if (i == 0) first_block = b;
        
        u8 block_buf[UFS_BLOCK_SIZE] = {0};
        u32 offset = i * UFS_BLOCK_SIZE;
        u32 chunk = size - offset;
        if (chunk > UFS_BLOCK_SIZE) chunk = UFS_BLOCK_SIZE;
        
        if (data) memcpy(block_buf, data + offset, chunk);
        
        if (write_block(b, block_buf) != 0) {
            ufs_log("  write block %d failed\n", b);
            // Откатываем
            for (u32 j = 0; j <= i; j++) {
                free_block(first_block + j);
            }
            return -1;
        }
    }
    
    // Создаём запись в родительской директории
    FSNode entry;
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.name, name);
    entry.size = size;
    entry.first_block = first_block;
    entry.is_dir = 0;
    entry.next_block = 0;
    
    // Проверяем есть ли уже такая запись и удаляем если есть
    FSNode* parent_e = (FSNode*)parent_buf;
    for (int i = 2; i < ENTRIES_PER_BLOCK; i++) {
        if (parent_e[i].name[0] && strcmp(parent_e[i].name, name) == 0) {
            // Затираем старую запись
            memcpy(&parent_e[i], &entry, sizeof(FSNode));
            ufs_log("  updated existing entry at %d[%d]\n", parent_block, i);
            return write_block(parent_block, parent_buf);
        }
    }
    
    // Иначе добавляем новую
    return add_to_dir(parent_block, &entry);
}

int ufs_read(const char* path, u8** data, u32* size) {
    ufs_log("ufs_read: %s\n", path);
    
    if (!mounted) return -1;
    if (!path || path[0] == '\0') return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent_block;
    
    if (strcmp(path, "/") == 0) {
        return -1;
    }
    
    if (get_parent(path, &parent_block, name) != 0) {
        ufs_log("  get_parent failed\n");
        return -1;
    }
    
    FSNode e;
    if (find_in_dir(parent_block, name, &e) != 0) {
        ufs_log("  not found\n");
        return -1;
    }
    
    if (e.is_dir) {
        ufs_log("  is a directory\n");
        return -1;
    }
    
    *size = e.size;
    ufs_log("  size=%d first_block=%d\n", e.size, e.first_block);
    
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
            ufs_log("  read block %d failed\n", block);
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

int ufs_readdir(const char* path, FSNode** entries, u32* count) {
    ufs_log("ufs_readdir: %s\n", path);
    
    if (!mounted) return -1;
    
    u32 dir_block;
    
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
        dir_block = sb.root_dir;
    } else {
        char name[UFS_MAX_NAME];
        u32 parent;
        
        if (get_parent(path, &parent, name) != 0) {
            ufs_log("  get_parent failed\n");
            return -1;
        }
        
        FSNode e;
        if (find_in_dir(parent, name, &e) != 0) {
            ufs_log("  not found\n");
            return -1;
        }
        
        if (!e.is_dir) {
            ufs_log("  not a directory\n");
            return -1;
        }
        
        dir_block = e.first_block;
    }
    
    ufs_log("  dir_block=%d\n", dir_block);
    return read_all_entries(dir_block, entries, count);
}

int ufs_delete(const char* path) {
    ufs_log("ufs_delete: %s\n", path);
    
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
    ufs_log("ufs_rmdir: %s\n", path);
    
    if (!mounted) return -1;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    
    if (get_parent(path, &parent, name) != 0) return -1;
    
    FSNode e;
    if (find_in_dir(parent, name, &e) != 0) return -1;
    if (!e.is_dir) return -1;
    
    FSNode* entries;
    u32 count;
    if (read_all_entries(e.first_block, &entries, &count) != 0) return -1;
    
    int empty = (count == 0);
    if (entries) kfree(entries);
    
    if (!empty) return -1;
    
    u32 current = e.first_block;
    while (current != 0) {
        u32 next = 0;
        u8 buf[UFS_BLOCK_SIZE];
        if (read_block(current, buf) == 0) {
            FSNode* be = (FSNode*)buf;
            next = be[0].next_block;
        }
        free_block(current);
        current = next;
    }
    
    return remove_from_dir(parent, name);
}

int ufs_rmdir_force(const char* path) {
    ufs_log("ufs_rmdir_force: %s\n", path);
    
    if (!mounted) return -1;
    if (!path || path[0] == '\0' || strcmp(path, "/") == 0) return -1;
    
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir(path, &entries, &count) != 0) return -1;
    
    for (u32 i = 0; i < count; i++) {
        char full[UFS_MAX_PATH];
        
        if (strcmp(path, "/") == 0) {
            strcpy(full, "/");
            strcat(full, entries[i].name);
        } else {
            strcpy(full, path);
            strcat(full, "/");
            strcat(full, entries[i].name);
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
    
    return (find_in_dir(parent, name, NULL) == 0);
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
