#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include "../include/types.h"

#define VFS_FILE    1
#define VFS_DIR     2
#define VFS_SYMLINK 3

#define VFS_MAX_NAME   256
#define VFS_MAX_MOUNTS 16
#define VFS_MAX_FDS    64

typedef struct vfs_node vfs_node_t;
typedef struct vfs_fs_ops vfs_fs_ops_t;
typedef struct vfs_mount vfs_mount_t;

typedef struct {
    char name[VFS_MAX_NAME];
    u32 type;
    u64 size;
} vfs_dirent_t;

typedef struct {
    char name[VFS_MAX_NAME];
    u32 size;
    u8 is_dir;
    u8 pad[3];
} __attribute__((packed)) vfs_user_dirent_t;

struct vfs_node {
    char name[VFS_MAX_NAME];
    u32 type;
    u32 mode;
    u64 size;
    u64 capacity;
    u8* data;
    char symlink[VFS_MAX_NAME];
    vfs_node_t* parent;
    vfs_node_t* children;
    vfs_node_t* next;
    vfs_fs_ops_t* fs;
    void* private;
    void* fs_data;
    int refcount;
};

struct vfs_fs_ops {
    const char* name;
    int (*mount)(vfs_node_t** root, const char* dev);
    int (*unmount)(vfs_node_t* root);
    int (*lookup)(vfs_node_t* dir, const char* name, vfs_node_t** out);
    int (*create)(vfs_node_t* dir, const char* name, u32 mode);
    int (*mkdir)(vfs_node_t* dir, const char* name, u32 mode);
    int (*unlink)(vfs_node_t* dir, const char* name);
    int (*symlink)(vfs_node_t* dir, const char* name, const char* target);
    int (*readlink)(vfs_node_t* node, char* buf, u32 size);
    int (*read)(vfs_node_t* node, void* buf, u64 size, u64 offset);
    int (*write)(vfs_node_t* node, const void* buf, u64 size, u64 offset);
    int (*readdir)(vfs_node_t* dir, vfs_dirent_t* entries, u32* count);
    int (*stat)(vfs_node_t* node, u64* size, u32* mode, u8* is_dir);
    int (*rename)(vfs_node_t* old_dir, const char* old_name, vfs_node_t* new_dir, const char* new_name);
    int (*format)(const char* dev);
};

struct vfs_mount {
    char path[VFS_MAX_NAME];
    vfs_node_t* root;
    vfs_fs_ops_t* fs;
    int used;
};

int vfs_init(void);
int vfs_register_fs(vfs_fs_ops_t* ops);
vfs_node_t* vfs_create_node(const char* name, u32 type);
vfs_node_t* vfs_root(void);
vfs_node_t* vfs_resolve_path(const char* path);
int vfs_mount_fs(const char* fstype, const char* dev, const char* mountpoint);
int vfs_unmount(const char* mountpoint);
vfs_node_t* vfs_open(const char* path, int flags, int mode);
int vfs_close(vfs_node_t* node);
int vfs_read(vfs_node_t* node, void* buf, u64 size, u64 offset);
int vfs_write(vfs_node_t* node, const void* buf, u64 size, u64 offset);
int vfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count);
int vfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir);
int vfs_mkdir(const char* path, u32 mode);
int vfs_unlink(const char* path);
int vfs_rmdir(const char* path);
int vfs_rename(const char* old, const char* new);
int vfs_symlink(const char* target, const char* linkpath);
int vfs_readlink(const char* path, char* buf, u32 size);

int vfs_read_entire(const char* path, u8** data, u32* size);
int vfs_write_entire(const char* path, const u8* data, u32 size);
int vfs_exists(const char* path);
int vfs_isdir(const char* path);
int vfs_is_mounted(const char* path);
int vfs_format(const char* fstype, const char* dev);

#endif
