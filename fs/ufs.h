#ifndef UFS_H
#define UFS_H

#include "../include/types.h"

#define UFS_BLOCK_SIZE 4096
#define UFS_MAGIC 0x55544D53
#define UFS_VERSION 2

#define UFS_INODE_FILE  0x8000
#define UFS_INODE_DIR   0x4000
#define UFS_INODE_SYMLINK 0xA000

int ufs_format(u32 start_lba, u32 blocks, int disk);
int ufs_register(void);

#endif
