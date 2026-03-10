#include "vfs.h"
#include "memory.h"
#include "../include/string.h"

static vfs_node_t *root = NULL;

static vfs_node_t *vfs_create_node(const char *name, u32 flags) {
    vfs_node_t *node = kmalloc(sizeof(vfs_node_t));
    memset(node, 0, sizeof(vfs_node_t));
    strcpy(node->name, name);
    node->flags = flags;
    return node;
}

void vfs_init(void) {
    root = vfs_create_node("/", FS_DIRECTORY);
}

vfs_node_t *vfs_mount(const char *path, vfs_ops_t *ops, void *fs_data) {
    vfs_node_t *node = vfs_create_node("", FS_DIRECTORY | FS_MOUNTPOINT);
    node->ops = ops;
    node->fs_data = fs_data;
    
    if (strcmp(path, "/") == 0) {
        // Монтируем в корень
        root = node;
    } else {
        // TODO: монтировать в поддиректорию
    }
    
    return node;
}

static int vfs_split_path(const char *path, char *parent, char *child) {
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        strcpy(parent, "/");
        strcpy(child, path);
        return 0;
    }
    
    int parent_len = last_slash - path;
    strncpy(parent, path, parent_len);
    parent[parent_len] = '\0';
    strcpy(child, last_slash + 1);
    
    return 0;
}

int vfs_open(const char *path, vfs_node_t **node) {
    if (strcmp(path, "/") == 0) {
        *node = root;
        return 0;
    }
    
    char parent_path[256];
    char child_name[256];
    vfs_split_path(path, parent_path, child_name);
    
    vfs_node_t *parent = NULL;
    if (vfs_open(parent_path, &parent) != 0) {
        return -1;
    }
    
    if (parent->flags & FS_MOUNTPOINT) {
        // Используем драйвер ФС
        return vfs_finddir(parent, child_name, node);
    } else {
        // TODO: искать в дочерних узлах
        return -1;
    }
}

int vfs_read(vfs_node_t *node, u64 offset, u64 size, u8 *buffer) {
    if (!node || !node->ops || !node->ops->read) return -1;
    return node->ops->read(node, offset, size, buffer);
}

int vfs_write(vfs_node_t *node, u64 offset, u64 size, u8 *buffer) {
    if (!node || !node->ops || !node->ops->write) return -1;
    return node->ops->write(node, offset, size, buffer);
}

int vfs_readdir(vfs_node_t *node, u64 index, char *name, u32 *size, u8 *is_dir) {
    if (!node || !node->ops || !node->ops->readdir) return -1;
    return node->ops->readdir(node, index, name, size, is_dir);
}

int vfs_finddir(vfs_node_t *node, const char *name, vfs_node_t **result) {
    if (!node || !node->ops || !node->ops->finddir) return -1;
    return node->ops->finddir(node, name, result);
}
