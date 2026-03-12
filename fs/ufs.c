#include "ufs.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"

#define UFS_MAGIC 0x55465300
#define UFS_BLOCK_SIZE 512

// Структура суперблока (упакована)
typedef struct {
    u32 magic;
    u32 total_blocks;
    u32 free_blocks;
    u32 root_dir;
} __attribute__((packed)) ufs_superblock_t;

// Структура записи директории (упакована)
typedef struct {
    char name[UFS_MAX_NAME];
    u32 size;
    u32 first_block;
    u8 is_dir;
} __attribute__((packed)) ufs_entry_t;

static ufs_superblock_t sb;
static int mounted = 0;
static u32 part_start = 0;  // LBA начала раздела UFS

// ========== НИЗКОУРОВНЕВЫЕ ФУНКЦИИ ==========

static int read_block(u32 b, u8* buf) {
    return disk_read(part_start + b, buf);
}

static int write_block(u32 b, u8* buf) {
    return disk_write(part_start + b, buf);
}

// Поиск свободного блока (начиная с 2, потому что 0 - суперблок, 1 - корень)
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
            return i;
        }
    }
    return 0;
}

// Разделить путь на директорию и имя
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

// Поиск записи в блоке директории (возвращает индекс или -1)
static int find_in_dir(u32 block, const char* name, ufs_entry_t* out) {
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    ufs_entry_t* e = (ufs_entry_t*)buf;
    int count = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    for (int i = 2; i < count; i++) {  // начинаем с 2, пропуская . и ..
        if (e[i].name[0] && strcmp(e[i].name, name) == 0) {
            if (out) *out = e[i];
            return i;
        }
    }
    return -1;
}

// Добавить запись в директорию (в первое свободное место после 2)
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
    return -1;  // нет места
}

// Удалить запись из директории
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

// Получить блок родительской директории и имя файла/директории
static int get_parent(const char* path, u32* block, char* name) {
    if (strcmp(path, "/") == 0) {
        return -1;  // у корня нет родителя
    }
    
    char dir[256];
    split_path(path, dir, name);
    
    // Если родитель - корень
    if (strcmp(dir, "/") == 0) {
        *block = sb.root_dir;
        return 0;
    }
    
    // Иначе проходим по пути
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

// ========== API ==========

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
    
    // Суперблок
    memset(&sb, 0, sizeof(sb));
    sb.magic = UFS_MAGIC;
    sb.total_blocks = blocks;
    sb.free_blocks = blocks - 1;  // 1 блок под корневую директорию
    sb.root_dir = 1;
    
    u8 buf[UFS_BLOCK_SIZE];
    memcpy(buf, &sb, sizeof(sb));
    if (disk_write(start_lba, buf) != 0) return -1;
    
    // Корневая директория (блок 1)
    memset(buf, 0, UFS_BLOCK_SIZE);
    ufs_entry_t* e = (ufs_entry_t*)buf;
    strcpy(e[0].name, ".");
    e[0].is_dir = 1;
    e[0].first_block = 1;
    strcpy(e[1].name, "..");
    e[1].is_dir = 1;
    e[1].first_block = 1;
    // Остальные записи обнулены
    
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
    
    // Если файл уже существует, удаляем его
    ufs_entry_t old;
    if (find_in_dir(parent, name, &old) == 0) {
        if (old.is_dir) return -1;
        remove_from_dir(parent, name);
    }
    
    if (size == 0) {
        // Создаём пустой файл
        ufs_entry_t ne;
        memset(&ne, 0, sizeof(ne));
        strcpy(ne.name, name);
        ne.size = 0;
        ne.first_block = 0;
        ne.is_dir = 0;
        return add_to_dir(parent, &ne);
    }
    
    // Выделяем блоки
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
        memcpy(buf, data + offset, chunk);
        
        if (write_block(b, buf) != 0) return -1;
    }
    
    // Создаём запись
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
    
    // Проверяем, не существует ли уже
    if (find_in_dir(parent, name, NULL) >= 0) return -1;
    
    // Выделяем блок для новой директории
    u32 nb = find_free_block();
    if (!nb) return -1;
    
    // Создаём новую директорию с . и ..
    u8 buf[UFS_BLOCK_SIZE] = {0};
    ufs_entry_t* e = (ufs_entry_t*)buf;
    strcpy(e[0].name, ".");
    e[0].is_dir = 1;
    e[0].first_block = nb;
    strcpy(e[1].name, "..");
    e[1].is_dir = 1;
    e[1].first_block = parent;
    
    if (write_block(nb, buf) != 0) return -1;
    
    // Добавляем запись в родительскую директорию
    ufs_entry_t ne;
    memset(&ne, 0, sizeof(ne));
    strcpy(ne.name, name);
    ne.is_dir = 1;
    ne.first_block = nb;
    
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
        ufs_entry_t e;
        if (find_in_dir(parent, name, &e) != 0) return -1;
        if (!e.is_dir) return -1;
        block = e.first_block;
    }
    
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(block, buf) != 0) return -1;
    ufs_entry_t* raw = (ufs_entry_t*)buf;
    int total = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    
    // Подсчёт реальных записей (пропуская . и ..)
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
            strncpy((*entries)[idx].name, raw[i].name, UFS_MAX_NAME - 1);
            (*entries)[idx].name[UFS_MAX_NAME - 1] = '\0';
            (*entries)[idx].size = raw[i].size;
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
    
    ufs_entry_t e;
    if (find_in_dir(parent, name, &e) != 0) return -1;
    if (!e.is_dir) return -1;
    
    // Проверяем, что директория пуста (кроме . и ..)
    u8 buf[UFS_BLOCK_SIZE];
    if (read_block(e.first_block, buf) != 0) return -1;
    ufs_entry_t* entries = (ufs_entry_t*)buf;
    int total = UFS_BLOCK_SIZE / sizeof(ufs_entry_t);
    for (int i = 2; i < total; i++) {
        if (entries[i].name[0] != 0) return -1;  // не пусто
    }
    
    // Удаляем запись из родителя
    return remove_from_dir(parent, name);
}

int ufs_delete(const char* path) {
    if (!mounted) return -1;
    
    char name[UFS_MAX_NAME];
    u32 parent;
    if (get_parent(path, &parent, name) != 0) return -1;
    
    ufs_entry_t e;
    if (find_in_dir(parent, name, &e) != 0) return -1;
    if (e.is_dir) return -1;
    
    return remove_from_dir(parent, name);
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

// Дополнительные функции (реализованы без заглушек)
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
    // Заглушка, но функция присутствует
    return 0;
}

int ufs_chown(const char* path, u16 uid, u16 gid) {
    (void)path; (void)uid; (void)gid;
    return 0;
}
