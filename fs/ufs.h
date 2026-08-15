// File: fs/ufs.h
#ifndef UFS_H
#define UFS_H

#include "../include/types.h"

#define UFS_BLOCK_SIZE 512
#define UFS_MAX_NAME 28
#define UFS_MAX_PATH 256
#define UFS_DIRECT_BLOCKS 10
#define UFS_CACHE_SIZE 64

#define UFS_MODE_FILE  0x8000
#define UFS_MODE_DIR   0x4000
#define UFS_MODE_RW    0x01FF

typedef struct {
    u16 mode;
    u16 uid;
    u16 gid;
    u8  nlink;
    u8  reserved[3];
    u32 size;
    u32 blocks;
    u32 direct[UFS_DIRECT_BLOCKS];
    u32 indirect;
    u32 atime;
    u32 mtime;
    u32 ctime;
    char name[UFS_MAX_NAME];
    u8 is_dir;
    u8 pad[1];
} __attribute__((packed)) FSNode;

int ufs_mount(u32 start_lba, int disk);
int ufs_mount_with_point(u32 start_lba, int disk, const char* point);
int ufs_umount(void);
int ufs_ismounted(void);
const char* ufs_get_device(void);
const char* ufs_get_mount_point(void);
int ufs_format(u32 start_lba, u32 blocks, int disk);
int ufs_write(const char* path, u8* data, u32 size);
int ufs_rewrite(const char* path, u8* data, u32 size);
int ufs_read(const char* path, u8** data, u32* size);
int ufs_delete(const char* path);
int ufs_mkdir(const char* path);
int ufs_rmdir(const char* path);
int ufs_rmdir_force(const char* path);
int ufs_readdir(const char* path, FSNode** entries, u32* count);
int ufs_exists(const char* path);
int ufs_isdir(const char* path);
int ufs_stat(u32* total, u32* used, u32* free);
int ufs_cp(const char* src, const char* dst);
int ufs_mv(const char* src, const char* dst);
u32 ufs_file_size(const char* path);

#endif
