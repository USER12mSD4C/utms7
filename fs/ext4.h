#ifndef EXT4_H
#define EXT4_H

#include "../include/types.h"

#define EXT4_SUPERBLOCK_OFFSET 1024
#define EXT4_MAGIC 0xEF53

typedef struct {
    u32 inodes_count;
    u32 blocks_count_lo;
    u32 reserved_blocks_count_lo;
    u32 free_blocks_count_lo;
    u32 free_inodes_count_lo;
    u32 first_data_block;
    u32 log_block_size;
    u32 log_cluster_size;
    u32 blocks_per_group;
    u32 clusters_per_group;
    u32 inodes_per_group;
    u32 mount_time;
    u32 write_time;
    u16 mount_count;
    u16 max_mount_count;
    u16 magic;
    u16 state;
    u16 errors;
    u16 minor_rev_level;
    u32 lastcheck;
    u32 checkinterval;
    u32 creator_os;
    u32 rev_level;
    u16 def_resuid;
    u16 def_resgid;
    // ...还有很多
} __attribute__((packed)) ext4_superblock_t;

int ext4_mount(u32 start_lba);
int ext4_read_file(const char *path, u8 **data, u32 *size);
int ext4_read_dir(const char *path, void **entries, u32 *count);

#endif
