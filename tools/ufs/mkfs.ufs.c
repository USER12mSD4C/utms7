#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define UFS_BLOCK_SIZE 4096
#define UFS_MAGIC 0x55544D53
#define UFS_VERSION 2

typedef struct { unsigned int start; unsigned int len; } extent_t;

typedef struct {
    unsigned int magic; unsigned int version; unsigned int total_blocks; unsigned int free_blocks;
    unsigned int inode_count; unsigned int free_inodes; unsigned int bitmap_start;
    unsigned int inode_table_start; unsigned int data_start; unsigned int root_inode;
    unsigned int pad[54]; unsigned int checksum;
} __attribute__((packed)) ufs_superblock_t;

typedef struct {
    unsigned short mode; unsigned short uid; unsigned short gid; unsigned short nlink;
    unsigned int size; unsigned int blocks; unsigned int atime; unsigned int mtime; unsigned int ctime;
    extent_t extents[8]; unsigned int pad[10]; unsigned int checksum;
} __attribute__((packed)) ufs_inode_t;

typedef struct {
    unsigned int inode; unsigned char type; unsigned char name_len; unsigned short pad; char name[56];
} __attribute__((packed)) ufs_dirent_t;

void write_block(int fd, unsigned int block, const void *data) {
    lseek(fd, block * UFS_BLOCK_SIZE, SEEK_SET);
    if (write(fd, data, UFS_BLOCK_SIZE) != UFS_BLOCK_SIZE) {
        perror("write");
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device/image>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    struct stat st;
    fstat(fd, &st);
    unsigned long long size_bytes = st.st_size;
    if (size_bytes == 0) {
        size_bytes = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
    }

    unsigned int total_blocks = size_bytes / UFS_BLOCK_SIZE;
    if (total_blocks < 10) {
        fprintf(stderr, "Device too small\n");
        close(fd);
        return 1;
    }

    unsigned int bitmap_blocks = (total_blocks + (UFS_BLOCK_SIZE * 8) - 1) / (UFS_BLOCK_SIZE * 8);
    unsigned int inode_count = 1024;
    if (inode_count > total_blocks / 4) inode_count = total_blocks / 4;
    unsigned int inodes_per_block = UFS_BLOCK_SIZE / sizeof(ufs_inode_t);
    unsigned int inode_blocks = (inode_count + inodes_per_block - 1) / inodes_per_block;

    ufs_superblock_t sb = {0};
    sb.magic = UFS_MAGIC;
    sb.version = UFS_VERSION;
    sb.total_blocks = total_blocks;
    sb.inode_count = inode_count;
    sb.bitmap_start = 1;
    sb.inode_table_start = sb.bitmap_start + bitmap_blocks;
    sb.data_start = sb.inode_table_start + inode_blocks;
    sb.root_inode = 1;
    sb.free_blocks = total_blocks - sb.data_start;
    sb.free_inodes = inode_count;

    char block[UFS_BLOCK_SIZE];
    memset(block, 0, UFS_BLOCK_SIZE);

    printf("Formatting %s as UTMS UFS...\n", argv[1]);
    for (unsigned int i = 0; i < sb.data_start; i++) {
        write_block(fd, i, block);
    }

    for (unsigned int i = 0; i < sb.data_start; i++) {
        unsigned int b_block = sb.bitmap_start + i / (UFS_BLOCK_SIZE * 8);
        unsigned int off = (i % (UFS_BLOCK_SIZE * 8)) / 8;
        unsigned int bit = i % 8;
        lseek(fd, b_block * UFS_BLOCK_SIZE + off, SEEK_SET);
        if (read(fd, block, 1) != 1) block[0] = 0;
        block[0] |= (1 << bit);
        lseek(fd, b_block * UFS_BLOCK_SIZE + off, SEEK_SET);
        if (write(fd, block, 1) != 1) { perror("write"); return 1; }
    }

    unsigned int root_block = sb.data_start;
    sb.free_blocks--;

    unsigned int rb_block = sb.bitmap_start + root_block / (UFS_BLOCK_SIZE * 8);
    unsigned int rb_off = (root_block % (UFS_BLOCK_SIZE * 8)) / 8;
    unsigned int rb_bit = root_block % 8;
    lseek(fd, rb_block * UFS_BLOCK_SIZE + rb_off, SEEK_SET);
    if (read(fd, block, 1) != 1) block[0] = 0;
    block[0] |= (1 << rb_bit);
    lseek(fd, rb_block * UFS_BLOCK_SIZE + rb_off, SEEK_SET);
    if (write(fd, block, 1) != 1) { perror("write"); return 1; }

    ufs_inode_t root = {0};
    root.mode = 0x4000 | 0755;
    root.nlink = 2;
    root.atime = root.mtime = root.ctime = time(NULL);
    root.size = 2 * sizeof(ufs_dirent_t);
    root.blocks = 1;
    root.extents[0].start = root_block;
    root.extents[0].len = 1;

    unsigned int root_ino_block = sb.inode_table_start;
    lseek(fd, root_ino_block * UFS_BLOCK_SIZE, SEEK_SET);
    if (read(fd, block, UFS_BLOCK_SIZE) != UFS_BLOCK_SIZE) memset(block, 0, UFS_BLOCK_SIZE);
    memcpy(block, &root, sizeof(ufs_inode_t));
    write_block(fd, root_ino_block, block);
    sb.free_inodes--;

    memset(block, 0, UFS_BLOCK_SIZE);
    ufs_dirent_t *ents = (ufs_dirent_t *)block;
    ents[0].inode = 1; ents[0].type = 2; ents[0].name_len = 1; strcpy(ents[0].name, ".");
    ents[1].inode = 1; ents[1].type = 2; ents[1].name_len = 2; strcpy(ents[1].name, "..");
    write_block(fd, root_block, block);

    lseek(fd, 0, SEEK_SET);
    if (write(fd, &sb, sizeof(sb)) != sizeof(sb)) { perror("write"); return 1; }

    close(fd);
    printf("Done. Blocks: %u, Inodes: %u\n", total_blocks, inode_count);
    return 0;
}
