#include "vfs.h"
#include "memory.h"
#include "../include/string.h"

static vfs_fs_ops_t* fs_list[16];
static int fs_count = 0;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_node_t* root_node = NULL;
static int vfs_ready = 0;
static vfs_fs_ops_t ramfs_ops;

vfs_node_t* vfs_create_node(const char* name, u32 type) {
    vfs_node_t* n = kmalloc(sizeof(vfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(vfs_node_t));
    strncpy(n->name, name, VFS_MAX_NAME - 1);
    n->type = type;
    n->refcount = 1;
    return n;
}

vfs_node_t* vfs_root(void) { return root_node; }

int vfs_register_fs(vfs_fs_ops_t* ops) {
    if (!ops || !ops->name || fs_count >= 16) return -1;
    fs_list[fs_count++] = ops;
    return 0;
}

static vfs_node_t* ramfs_find_child(vfs_node_t* dir, const char* name) {
    if (!dir || dir->type != VFS_DIR) return NULL;
    vfs_node_t* c = dir->children;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

static void ramfs_add_child(vfs_node_t* dir, vfs_node_t* child) {
    if (!dir || !child) return;
    child->parent = dir;
    child->next = dir->children;
    dir->children = child;
}

static int ramfs_remove_child(vfs_node_t* dir, vfs_node_t* child) {
    if (!dir || !child) return -1;
    vfs_node_t** pp = &dir->children;
    while (*pp) {
        if (*pp == child) {
            *pp = child->next;
            child->next = NULL;
            child->parent = NULL;
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

static int ramfs_lookup(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    if (!dir || dir->type != VFS_DIR || !name || !out) return -1;
    vfs_node_t* child = ramfs_find_child(dir, name);
    if (!child) return -1;
    *out = child;
    return 0;
}

static int ramfs_create(vfs_node_t* dir, const char* name, u32 mode) {
    if (!dir || dir->type != VFS_DIR || !name) return -1;
    if (ramfs_find_child(dir, name)) return -1;
    vfs_node_t* n = vfs_create_node(name, VFS_FILE);
    if (!n) return -1;
    n->mode = mode;
    n->fs = dir->fs;
    ramfs_add_child(dir, n);
    return 0;
}

static int ramfs_mkdir(vfs_node_t* dir, const char* name, u32 mode) {
    if (!dir || dir->type != VFS_DIR || !name) return -1;
    if (ramfs_find_child(dir, name)) return -1;
    vfs_node_t* n = vfs_create_node(name, VFS_DIR);
    if (!n) return -1;
    n->mode = mode;
    n->fs = dir->fs;
    ramfs_add_child(dir, n);
    return 0;
}

static void ramfs_free_node(vfs_node_t* node);

static int ramfs_unlink(vfs_node_t* dir, const char* name) {
    if (!dir || dir->type != VFS_DIR || !name) return -1;
    vfs_node_t* child = ramfs_find_child(dir, name);
    if (!child) return -1;
    if (child->type == VFS_DIR && child->children != NULL) return -1;
    if (ramfs_remove_child(dir, child) != 0) return -1;
    ramfs_free_node(child);
    return 0;
}

static int ramfs_symlink(vfs_node_t* dir, const char* name, const char* target) {
    if (!dir || dir->type != VFS_DIR || !name || !target) return -1;
    if (ramfs_find_child(dir, name)) return -1;
    vfs_node_t* n = vfs_create_node(name, VFS_SYMLINK);
    if (!n) return -1;
    n->mode = 0777;
    n->fs = dir->fs;
    strncpy(n->symlink, target, VFS_MAX_NAME - 1);
    n->symlink[VFS_MAX_NAME - 1] = '\0';
    ramfs_add_child(dir, n);
    return 0;
}

static int ramfs_readlink(vfs_node_t* node, char* buf, u32 size) {
    if (!node || node->type != VFS_SYMLINK || !buf || size == 0) return -1;
    strncpy(buf, node->symlink, size - 1);
    buf[size - 1] = '\0';
    return (int)strlen(buf);
}

static int ramfs_read(vfs_node_t* node, void* buf, u64 size, u64 offset) {
    if (!node || node->type != VFS_FILE || !buf) return -1;
    if (offset >= node->size) return 0;
    u64 to_copy = node->size - offset;
    if (size < to_copy) to_copy = size;
    memcpy(buf, node->data + offset, to_copy);
    return (int)to_copy;
}

static int ramfs_write(vfs_node_t* node, const void* buf, u64 size, u64 offset) {
    if (!node || node->type != VFS_FILE || !buf) return -1;
    if (size == 0) return 0;
    u64 end = offset + size;
    if (end > node->capacity) {
        u64 new_cap = node->capacity ? node->capacity : 4096;
        while (new_cap < end) new_cap *= 2;
        u8* nd = kmalloc(new_cap);
        if (!nd) return -1;
        memset(nd, 0, new_cap);
        if (node->data && node->size > 0) memcpy(nd, node->data, node->size);
        if (node->data) kfree(node->data);
        node->data = nd;
        node->capacity = new_cap;
    }
    if (offset > node->size) memset(node->data + node->size, 0, offset - node->size);
    memcpy(node->data + offset, buf, size);
    if (end > node->size) node->size = end;
    return (int)size;
}

static int ramfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count) {
    if (!dir || dir->type != VFS_DIR || !entries || !count) return -1;
    u32 max = *count;
    u32 n = 0;
    vfs_node_t* c = dir->children;
    while (c && n < max) {
        strncpy(entries[n].name, c->name, VFS_MAX_NAME - 1);
        entries[n].name[VFS_MAX_NAME - 1] = '\0';
        entries[n].type = c->type;
        entries[n].size = c->size;
        n++;
        c = c->next;
    }
    *count = n;
    return 0;
}

static int ramfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir) {
    if (!node) return -1;
    if (size) *size = node->size;
    if (mode) *mode = node->mode;
    if (is_dir) *is_dir = (node->type == VFS_DIR) ? 1 : 0;
    return 0;
}

static int ramfs_rename(vfs_node_t* old_dir, const char* old_name, vfs_node_t* new_dir, const char* new_name) {
    if (!old_dir || !new_dir || !old_name || !new_name) return -1;
    vfs_node_t* child = ramfs_find_child(old_dir, old_name);
    if (!child) return -1;
    vfs_node_t* existing = ramfs_find_child(new_dir, new_name);
    if (existing) {
        if (existing->type == VFS_DIR && existing->children != NULL) return -1;
        ramfs_remove_child(new_dir, existing);
        ramfs_free_node(existing);
    }
    if (ramfs_remove_child(old_dir, child) != 0) return -1;
    strncpy(child->name, new_name, VFS_MAX_NAME - 1);
    child->name[VFS_MAX_NAME - 1] = '\0';
    ramfs_add_child(new_dir, child);
    return 0;
}

static void ramfs_free_node(vfs_node_t* node) {
    if (!node) return;
    vfs_node_t* c = node->children;
    while (c) {
        vfs_node_t* next = c->next;
        ramfs_free_node(c);
        c = next;
    }
    if (node->data) kfree(node->data);
    kfree(node);
}

static int ramfs_mount(vfs_node_t** root, const char* dev) {
    (void)dev;
    if (!root) return -1;
    vfs_node_t* n = vfs_create_node("", VFS_DIR);
    if (!n) return -1;
    n->mode = 0755;
    n->fs = &ramfs_ops;
    *root = n;
    return 0;
}

static int ramfs_unmount(vfs_node_t* root) {
    ramfs_free_node(root);
    return 0;
}

static vfs_fs_ops_t ramfs_ops = {
    .name = "ramfs",
    .mount = ramfs_mount,
    .unmount = ramfs_unmount,
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
    .unlink = ramfs_unlink,
    .symlink = ramfs_symlink,
    .readlink = ramfs_readlink,
    .read = ramfs_read,
    .write = ramfs_write,
    .readdir = ramfs_readdir,
    .stat = ramfs_stat,
    .rename = ramfs_rename,
    .format = NULL
};

static vfs_mount_t* find_mount(const char* path, const char** rel) {
    int best = -1;
    u32 best_len = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) continue;
        u32 len = strlen(mounts[i].path);
        if (strncmp(path, mounts[i].path, len) == 0) {
            if (path[len] == '\0' || path[len] == '/' || len == 1) {
                if (len > best_len) {
                    best_len = len;
                    best = i;
                }
            }
        }
    }
    if (best < 0) return NULL;
    *rel = path + best_len;
    while (**rel == '/') (*rel)++;
    return &mounts[best];
}

static int lookup_one(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    if (!dir || !name || !out) return -1;
    if (!dir->fs || !dir->fs->lookup) return -1;
    return dir->fs->lookup(dir, name, out);
}

static vfs_node_t* resolve_path_internal(const char* path, int depth) {
    if (!vfs_ready || !path || depth > 8) return NULL;
    if (path[0] == '\0') return root_node;
    if (path[0] != '/') return NULL;

    const char* rel = NULL;
    vfs_mount_t* mnt = find_mount(path, &rel);
    if (!mnt || !mnt->root) return NULL;

    vfs_node_t* node = mnt->root;
    if (node->type == VFS_SYMLINK) {
        char target[VFS_MAX_NAME];
        if (node->fs && node->fs->readlink) {
            node->fs->readlink(node, target, VFS_MAX_NAME);
            if (target[0] == '/') return resolve_path_internal(target, depth + 1);
        }
    }

    if (!rel || rel[0] == '\0') return node;

    char tmp[1024];
    strncpy(tmp, rel, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* p = tmp;

    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        char* comp = p;
        while (*p && *p != '/') p++;
        char saved = 0;
        if (*p) { saved = *p; *p = '\0'; }

        if (strcmp(comp, ".") == 0) { if (saved) *p = saved; continue; }
        if (strcmp(comp, "..") == 0) {
            if (node->parent) node = node->parent;
            if (saved) *p = saved;
            continue;
        }

        if (node->type != VFS_DIR) return NULL;
        vfs_node_t* next = NULL;
        if (lookup_one(node, comp, &next) != 0) return NULL;
        node = next;
        if (saved) *p = saved;
    }

    return node;
}

vfs_node_t* vfs_resolve_path(const char* path) { return resolve_path_internal(path, 0); }

int vfs_mount_fs(const char* fstype, const char* dev, const char* mountpoint) {
    if (!fstype || !mountpoint) return -1;

    vfs_fs_ops_t* ops = NULL;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_list[i]->name, fstype) == 0) {
            ops = fs_list[i];
            break;
        }
    }

    if (!ops || !ops->mount) return -1;

    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return -1;

    vfs_node_t* fs_root = NULL;
    if (ops->mount(&fs_root, dev) != 0) return -1;
    if (!fs_root) return -1;
    if (!fs_root->fs) fs_root->fs = ops;

    strncpy(mounts[slot].path, mountpoint, VFS_MAX_NAME - 1);
    mounts[slot].path[VFS_MAX_NAME - 1] = '\0';
    mounts[slot].root = fs_root;
    mounts[slot].fs = ops;
    mounts[slot].used = 1;

    if (strcmp(mountpoint, "/") == 0) root_node = fs_root;

    return 0;
}

int vfs_unmount(const char* mountpoint) {
    if (!mountpoint) return -1;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && strcmp(mounts[i].path, mountpoint) == 0) {
            vfs_node_t* old_root = mounts[i].root;

            if (mounts[i].fs && mounts[i].fs->unmount) {
                mounts[i].fs->unmount(old_root);
            }

            if (root_node == old_root) root_node = NULL;

            mounts[i].used = 0;
            mounts[i].root = NULL;
            mounts[i].fs = NULL;
            mounts[i].path[0] = '\0';

            return 0;
        }
    }

    return -1;
}

int vfs_is_mounted(const char* path) {
    if (!path) return 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && strcmp(mounts[i].path, path) == 0) return 1;
    }

    return 0;
}

int vfs_format(const char* fstype, const char* dev) {
    if (!fstype || !dev) return -1;

    vfs_fs_ops_t* ops = NULL;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_list[i]->name, fstype) == 0) {
            ops = fs_list[i];
            break;
        }
    }

    if (!ops || !ops->format) return -1;

    return ops->format(dev);
}

vfs_node_t* vfs_open(const char* path, int flags, int mode) {
    vfs_node_t* node = vfs_resolve_path(path);

    if (node) {
        if ((flags & 0x200) && node->type == VFS_FILE && node->fs == &ramfs_ops) {
            node->size = 0;
        }
        node->refcount++;
        return node;
    }

    if (!(flags & 0x40)) return NULL;

    char dir_path[1024];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    char* slash = strrchr(dir_path, '/');
    if (!slash) return NULL;

    char name[VFS_MAX_NAME];
    if (slash == dir_path) {
        strcpy(name, slash + 1);
        strcpy(dir_path, "/");
    } else {
        strcpy(name, slash + 1);
        *slash = '\0';
    }

    vfs_node_t* dir = vfs_resolve_path(dir_path);
    if (!dir || dir->type != VFS_DIR || !dir->fs || !dir->fs->create) return NULL;

    if (dir->fs->create(dir, name, mode) != 0) return NULL;

    vfs_node_t* new_node = NULL;
    if (dir->fs->lookup(dir, name, &new_node) != 0) return NULL;

    new_node->refcount++;
    return new_node;
}

int vfs_close(vfs_node_t* node) {
    if (!node) return -1;
    if (node->refcount > 0) node->refcount--;
    return 0;
}

int vfs_read(vfs_node_t* node, void* buf, u64 size, u64 offset) {
    if (!node || !node->fs || !node->fs->read) return -1;
    return node->fs->read(node, buf, size, offset);
}

int vfs_write(vfs_node_t* node, const void* buf, u64 size, u64 offset) {
    if (!node || !node->fs || !node->fs->write) return -1;
    return node->fs->write(node, buf, size, offset);
}

int vfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count) {
    if (!dir || !dir->fs || !dir->fs->readdir) return -1;
    return dir->fs->readdir(dir, entries, count);
}

int vfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir) {
    if (!node) return -1;
    if (node->fs && node->fs->stat) return node->fs->stat(node, size, mode, is_dir);

    if (size) *size = node->size;
    if (mode) *mode = node->mode;
    if (is_dir) *is_dir = (node->type == VFS_DIR) ? 1 : 0;

    return 0;
}

static int split_path(const char* path, char* dir_buf, u32 dir_size, char* name_buf, u32 name_size) {
    if (!path || !dir_buf || !name_buf || dir_size == 0 || name_size == 0) return -1;

    strncpy(dir_buf, path, dir_size - 1);
    dir_buf[dir_size - 1] = '\0';

    char* slash = strrchr(dir_buf, '/');
    if (!slash) return -1;

    const char* comp = slash + 1;
    u64 comp_len = strlen(comp);
    if (comp_len >= name_size) return -1;

    strcpy(name_buf, comp);

    if (slash == dir_buf) {
        dir_buf[0] = '/';
        dir_buf[1] = '\0';
    } else {
        *slash = '\0';
    }

    return 0;
}

int vfs_mkdir(const char* path, u32 mode) {
    char dir_path[1024];
    char name[VFS_MAX_NAME];

    if (split_path(path, dir_path, sizeof(dir_path), name, sizeof(name)) != 0) return -1;

    vfs_node_t* dir = vfs_resolve_path(dir_path);
    if (!dir || dir->type != VFS_DIR || !dir->fs || !dir->fs->mkdir) return -1;

    return dir->fs->mkdir(dir, name, mode);
}

int vfs_unlink(const char* path) {
    char dir_path[1024];
    char name[VFS_MAX_NAME];

    if (split_path(path, dir_path, sizeof(dir_path), name, sizeof(name)) != 0) return -1;

    vfs_node_t* dir = vfs_resolve_path(dir_path);
    if (!dir || dir->type != VFS_DIR || !dir->fs || !dir->fs->unlink) return -1;

    return dir->fs->unlink(dir, name);
}

int vfs_rmdir(const char* path) {
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) return -1;
    if (node->type != VFS_DIR) return -1;

    return vfs_unlink(path);
}

int vfs_rename(const char* old, const char* new) {
    char old_dir_path[1024];
    char old_name[VFS_MAX_NAME];
    char new_dir_path[1024];
    char new_name[VFS_MAX_NAME];

    if (split_path(old, old_dir_path, sizeof(old_dir_path), old_name, sizeof(old_name)) != 0) return -1;
    if (split_path(new, new_dir_path, sizeof(new_dir_path), new_name, sizeof(new_name)) != 0) return -1;

    vfs_node_t* old_dir = vfs_resolve_path(old_dir_path);
    vfs_node_t* new_dir = vfs_resolve_path(new_dir_path);

    if (!old_dir || !new_dir) return -1;
    if (old_dir->type != VFS_DIR || new_dir->type != VFS_DIR) return -1;
    if (!old_dir->fs || !old_dir->fs->rename) return -1;

    return old_dir->fs->rename(old_dir, old_name, new_dir, new_name);
}

int vfs_symlink(const char* target, const char* linkpath) {
    char dir_path[1024];
    char name[VFS_MAX_NAME];

    if (split_path(linkpath, dir_path, sizeof(dir_path), name, sizeof(name)) != 0) return -1;

    vfs_node_t* dir = vfs_resolve_path(dir_path);
    if (!dir || dir->type != VFS_DIR || !dir->fs || !dir->fs->symlink) return -1;

    return dir->fs->symlink(dir, name, target);
}

int vfs_readlink(const char* path, char* buf, u32 size) {
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node || node->type != VFS_SYMLINK || !node->fs || !node->fs->readlink) return -1;
    return node->fs->readlink(node, buf, size);
}

int vfs_init(void) {
    memset(mounts, 0, sizeof(mounts));
    fs_count = 0;
    root_node = NULL;
    vfs_ready = 0;

    if (vfs_register_fs(&ramfs_ops) != 0) return -1;
    if (vfs_mount_fs("ramfs", NULL, "/") != 0) return -1;

    vfs_ready = 1;

    if (!root_node) return -1;

    vfs_mkdir("/tmp", 0777);
    vfs_mkdir("/dev", 0755);
    vfs_mkdir("/proc", 0555);

    return 0;
}

int vfs_read_entire(const char* path, u8** data, u32* size) {
    if (!path || !data || !size) return -1;

    vfs_node_t* node = vfs_open(path, 0, 0);
    if (!node) return -1;

    if (node->size > 0xFFFFFFFFULL) {
        vfs_close(node);
        return -1;
    }

    u32 sz = (u32)node->size;
    u8* buf = kmalloc(sz + 1);
    if (!buf) {
        vfs_close(node);
        return -1;
    }

    if (sz > 0) {
        if (vfs_read(node, buf, sz, 0) != (int)sz) {
            kfree(buf);
            vfs_close(node);
            return -1;
        }
    }

    buf[sz] = 0;
    vfs_close(node);

    *data = buf;
    *size = sz;

    return 0;
}

int vfs_write_entire(const char* path, const u8* data, u32 size) {
    if (!path || (!data && size != 0)) return -1;

    vfs_node_t* node = vfs_open(path, 0x40 | 0x200, 0644);
    if (!node) return -1;

    int res = 0;
    if (size > 0) {
        res = vfs_write(node, data, size, 0);
    }

    vfs_close(node);

    return (res == (int)size) ? 0 : -1;
}

int vfs_exists(const char* path) {
    vfs_node_t* node = vfs_resolve_path(path);
    return node != NULL;
}

int vfs_isdir(const char* path) {
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) return 0;
    return node->type == VFS_DIR;
}
