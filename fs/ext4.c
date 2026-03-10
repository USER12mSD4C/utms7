#include "ext4.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"
#include "../include/string.h"

static u32 partition_offset = 0;
static ext4_superblock_t sb;
static u32 block_size = 1024;
static u32 inodes_per_group;
static u32 blocks_per_group;

int ext4_mount(u32 start_lba) {
    partition_offset = start_lba;
    
    // Читаем суперблок (смещение 1024 байта от начала)
    u8 sector[512];
    if (disk_read(start_lba + 2, sector) != 0) { // LBA 2 = offset 1024
        return -1;
    }
    
    memcpy(&sb, sector + (1024 % 512), sizeof(sb));
    
    if (sb.magic != EXT4_MAGIC) {
        return -1;
    }
    
    block_size = 1024 << sb.log_block_size;
    inodes_per_group = sb.inodes_per_group;
    blocks_per_group = sb.blocks_per_group;
    
    return 0;
}

static u32 ext4_iget(u32 inode_num, void *inode) {
    u32 group = (inode_num - 1) / inodes_per_group;
    u32 index = (inode_num - 1) % inodes_per_group;
    u32 inode_table_block = 0; // TODO: из группового дескриптора
    u32 inode_size = 256; // TODO: из суперблока
    
    u32 block = inode_table_block + (index * inode_size) / block_size;
    u32 offset = (index * inode_size) % block_size;
    
    u8 *buffer = kmalloc(block_size);
    disk_read(partition_offset + block, buffer);
    memcpy(inode, buffer + offset, inode_size);
    kfree(buffer);
    
    return 0;
}

int ext4_read_file(const char *path, u8 **data, u32 *size) {
    // TODO: реализовать
    (void)path;
    (void)data;
    (void)size;
    return -1;
}
