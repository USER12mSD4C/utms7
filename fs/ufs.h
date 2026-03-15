#ifndef UFS_H
#define UFS_H

#include "../include/types.h"

#define UFS_BLOCK_SIZE 512
#define UFS_MAX_NAME 32
#define UFS_MAX_PATH 256

typedef struct {
    char name[UFS_MAX_NAME];
    u32 size;
    u32 first_block;
    u32 next_block;
    u8 is_dir;
} __attribute__((packed)) FSNode;

int ufs_mount(u32 start_lba, int disk);
int ufs_format(u32 start_lba, u32 blocks, int disk);
int ufs_write(const char* path, u8* data, u32 size);
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
int ufs_rewrite(const char* path, u8* data, u32 size);
int ufs_ismounted(void);
const char* ufs_get_device(void);
int ufs_umount(void);

#endif
