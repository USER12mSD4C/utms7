#ifndef UFS_H
#define UFS_H

#include "../include/types.h"

#define UFS_BLOCK_SIZE 512
#define UFS_MAX_NAME 56
#define UFS_MAX_PATH 256

typedef struct {
    char name[UFS_MAX_NAME];
    u32 size;
    u8 is_dir;
} FSNode;

// Основные функции
int ufs_mount(u32 start_lba);
int ufs_format(u32 start_lba, u32 blocks);

// Файловые операции
int ufs_write(const char *path, u8 *data, u32 size);
int ufs_read(const char *path, u8 **data, u32 *size);
int ufs_delete(const char *path);
int ufs_exists(const char *path);
int ufs_isdir(const char *path);

// Директории
int ufs_mkdir(const char *path);
int ufs_rmdir(const char *path);
int ufs_readdir(const char *path, FSNode **entries, u32 *count);

// Информация
int ufs_stat(u32 *total, u32 *used, u32 *free);

int ufs_is_mounted(void);

// Linux-подобные функции
int ufs_cp(const char *src, const char *dst);
int ufs_mv(const char *src, const char *dst);
int ufs_grep(const char *path, const char *pattern, void (*callback)(const char *line, u32 line_num));
int ufs_find(const char *start_path, const char *name, void (*callback)(const char *path));
int ufs_chmod(const char *path, u16 mode);
int ufs_chown(const char *path, u16 uid, u16 gid);

#endif
