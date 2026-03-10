#ifndef VFS_H
#define VFS_H

#include "../include/types.h"

typedef struct vfs_node vfs_node_t;

typedef struct vfs_ops {
    int (*read)(vfs_node_t *node, u64 offset, u64 size, u8 *buffer);
    int (*write)(vfs_node_t *node, u64 offset, u64 size, u8 *buffer);
    int (*readdir)(vfs_node_t *node, u64 index, char *name, u32 *size, u8 *is_dir);
    int (*finddir)(vfs_node_t *node, const char *name, vfs_node_t **result);
    int (*create)(vfs_node_t *node, const char *name, u8 is_dir);
    int (*unlink)(vfs_node_t *node, const char *name);
} vfs_ops_t;

struct vfs_node {
    char name[256];
    u32 flags;
    u64 size;
    u64 inode;
    vfs_ops_t *ops;
    void *fs_data;
    vfs_node_t *parent;
    vfs_node_t *children;
    vfs_node_t *next;
};

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_MOUNTPOINT  0x04

void vfs_init(void);
vfs_node_t *vfs_mount(const char *path, vfs_ops_t *ops, void *fs_data);
int vfs_open(const char *path, vfs_node_t **node);
int vfs_read(vfs_node_t *node, u64 offset, u64 size, u8 *buffer);
int vfs_write(vfs_node_t *node, u64 offset, u64 size, u8 *buffer);
int vfs_readdir(vfs_node_t *node, u64 index, char *name, u32 *size, u8 *is_dir);
int vfs_finddir(vfs_node_t *node, const char *name, vfs_node_t **result);

#endif
