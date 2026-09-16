#include "../core/vfs.h"
#include "../core/memory.h"
#include "../core/sched.h"
#include "../../include/string.h"
#include "../../include/fs_auto.h"

extern u32 get_ticks(void);

#define PROC_UPTIME      1
#define PROC_MEMINFO     2
#define PROC_CPUINFO     3
#define PROC_VERSION     4
#define PROC_PID_DIR     100
#define PROC_PID_CMDLINE 101
#define PROC_PID_STATUS  102

static vfs_fs_ops_t procfs_ops;

static int procfs_read(vfs_node_t* node, void* buf, u64 size, u64 offset) {
    if (!node || node->type != VFS_FILE) return -1;

    char tmp[512];
    int len = 0;
    u32 ptype = (u32)(u64)node->fs_data;

    switch (ptype) {
        case PROC_UPTIME: {
            u64 ticks = get_ticks();
            u64 sec = ticks / 1000;
            u64 frac = ticks % 1000;
            len = snprintf(tmp, sizeof(tmp), "%lu.%03lu %lu.%03lu\n",
                (unsigned long)sec, (unsigned long)frac,
                (unsigned long)sec, (unsigned long)frac);
            break;
        }
        case PROC_MEMINFO: {
            u64 total = memory_used() + memory_free();
            u64 used = memory_used();
            u64 free_mem = memory_free();
            len = snprintf(tmp, sizeof(tmp),
                "MemTotal: %lu kB\nMemFree: %lu kB\nMemUsed: %lu kB\n",
                (unsigned long)(total / 1024),
                (unsigned long)(free_mem / 1024),
                (unsigned long)(used / 1024));
            break;
        }
        case PROC_CPUINFO: {
            len = snprintf(tmp, sizeof(tmp),
                "processor\t: 0\nmodel name\t: UTMS7 Virtual CPU\n");
            break;
        }
        case PROC_VERSION: {
            len = snprintf(tmp, sizeof(tmp), "UTMS7 0.2 x86_64\n");
            break;
        }
        case PROC_PID_CMDLINE: {
            u32 pid = (u32)(u64)node->private;
            process_t* procs[MAX_PROCESSES];
            int count = sched_get_processes(procs, MAX_PROCESSES);
            for (int i = 0; i < count; i++) {
                if (procs[i]->pid == (int)pid) {
                    len = snprintf(tmp, sizeof(tmp), "%s\n", procs[i]->name);
                    break;
                }
            }
            break;
        }
        case PROC_PID_STATUS: {
            u32 pid = (u32)(u64)node->private;
            process_t* procs[MAX_PROCESSES];
            int count = sched_get_processes(procs, MAX_PROCESSES);
            for (int i = 0; i < count; i++) {
                if (procs[i]->pid == (int)pid) {
                    const char* state_str = "R";
                    if (procs[i]->state == 3) state_str = "S";
                    else if (procs[i]->state == 4) state_str = "B";
                    else if (procs[i]->state == 5) state_str = "Z";
                    len = snprintf(tmp, sizeof(tmp),
                        "Name:\t%s\nState:\t%s\nPid:\t%d\nPPid:\t%d\n",
                        procs[i]->name, state_str,
                        procs[i]->pid, procs[i]->ppid);
                    break;
                }
            }
            break;
        }
        default:
            return -1;
    }

    if (len <= 0) return 0;
    if (offset >= (u64)len) return 0;
    u64 to_copy = (u64)len - offset;
    if (size < to_copy) to_copy = size;
    memcpy(buf, tmp + offset, to_copy);
    return (int)to_copy;
}

static vfs_node_t* procfs_make_file(const char* name, u32 ptype, vfs_node_t* parent) {
    vfs_node_t* n = vfs_create_node(name, VFS_FILE);
    if (!n) return NULL;
    n->mode = 0444;
    n->fs = &procfs_ops;
    n->fs_data = (void*)(u64)ptype;
    n->private = NULL;
    n->parent = parent;
    n->next = parent->children;
    parent->children = n;
    return n;
}

static int procfs_lookup(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    if (!dir || dir->type != VFS_DIR || !name || !out) return -1;

    vfs_node_t* c = dir->children;
    while (c) {
        if (strcmp(c->name, name) == 0) {
            *out = c;
            return 0;
        }
        c = c->next;
    }

    if (strcmp(name, "uptime") == 0) {
        vfs_node_t* n = procfs_make_file("uptime", PROC_UPTIME, dir);
        if (!n) return -1;
        *out = n;
        return 0;
    }
    if (strcmp(name, "meminfo") == 0) {
        vfs_node_t* n = procfs_make_file("meminfo", PROC_MEMINFO, dir);
        if (!n) return -1;
        *out = n;
        return 0;
    }
    if (strcmp(name, "cpuinfo") == 0) {
        vfs_node_t* n = procfs_make_file("cpuinfo", PROC_CPUINFO, dir);
        if (!n) return -1;
        *out = n;
        return 0;
    }
    if (strcmp(name, "version") == 0) {
        vfs_node_t* n = procfs_make_file("version", PROC_VERSION, dir);
        if (!n) return -1;
        *out = n;
        return 0;
    }

    u32 pid = 0;
    int is_num = 1;
    for (int i = 0; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9') { is_num = 0; break; }
        pid = pid * 10 + (name[i] - '0');
    }

    if (is_num && pid > 0) {
        process_t* procs[MAX_PROCESSES];
        int count = sched_get_processes(procs, MAX_PROCESSES);
        int found = 0;
        for (int i = 0; i < count; i++) {
            if (procs[i]->pid == (int)pid) { found = 1; break; }
        }
        if (!found) return -1;

        vfs_node_t* n = vfs_create_node(name, VFS_DIR);
        if (!n) return -1;
        n->mode = 0555;
        n->fs = &procfs_ops;
        n->fs_data = (void*)(u64)PROC_PID_DIR;
        n->private = NULL;
        n->parent = dir;
        n->next = dir->children;
        dir->children = n;
        *out = n;
        return 0;
    }

    return -1;
}

static int procfs_pid_lookup(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    if (!dir || !name || !out) return -1;

    u32 pid = 0;
    const char* parent_name = dir->name;
    for (int i = 0; parent_name[i]; i++) {
        if (parent_name[i] < '0' || parent_name[i] > '9') return -1;
        pid = pid * 10 + (parent_name[i] - '0');
    }

    vfs_node_t* c = dir->children;
    while (c) {
        if (strcmp(c->name, name) == 0) {
            *out = c;
            return 0;
        }
        c = c->next;
    }

    if (strcmp(name, "cmdline") == 0) {
        vfs_node_t* n = procfs_make_file("cmdline", PROC_PID_CMDLINE, dir);
        if (!n) return -1;
        n->private = (void*)(u64)pid;
        *out = n;
        return 0;
    }
    if (strcmp(name, "status") == 0) {
        vfs_node_t* n = procfs_make_file("status", PROC_PID_STATUS, dir);
        if (!n) return -1;
        n->private = (void*)(u64)pid;
        *out = n;
        return 0;
    }

    return -1;
}

static int procfs_lookup_dispatch(vfs_node_t* dir, const char* name, vfs_node_t** out) {
    u32 ptype = (u32)(u64)dir->fs_data;
    if (ptype == PROC_PID_DIR) {
        return procfs_pid_lookup(dir, name, out);
    }
    return procfs_lookup(dir, name, out);
}

static int procfs_readdir(vfs_node_t* dir, vfs_dirent_t* entries, u32* count) {
    if (!dir || dir->type != VFS_DIR || !entries || !count) return -1;

    u32 max = *count;
    u32 n = 0;
    u32 ptype = (u32)(u64)dir->fs_data;

    if (ptype == PROC_PID_DIR) {
        vfs_node_t* c = dir->children;
        while (c && n < max) {
            strncpy(entries[n].name, c->name, VFS_MAX_NAME - 1);
            entries[n].name[VFS_MAX_NAME - 1] = '\0';
            entries[n].type = c->type;
            entries[n].size = 0;
            n++;
            c = c->next;
        }
        if (n < max) {
            strncpy(entries[n].name, "cmdline", VFS_MAX_NAME - 1);
            entries[n].type = VFS_FILE;
            entries[n].size = 0;
            n++;
        }
        if (n < max) {
            strncpy(entries[n].name, "status", VFS_MAX_NAME - 1);
            entries[n].type = VFS_FILE;
            entries[n].size = 0;
            n++;
        }
        *count = n;
        return 0;
    }

    if (n < max) { strncpy(entries[n].name, "uptime", VFS_MAX_NAME - 1); entries[n].type = VFS_FILE; entries[n].size = 0; n++; }
    if (n < max) { strncpy(entries[n].name, "meminfo", VFS_MAX_NAME - 1); entries[n].type = VFS_FILE; entries[n].size = 0; n++; }
    if (n < max) { strncpy(entries[n].name, "cpuinfo", VFS_MAX_NAME - 1); entries[n].type = VFS_FILE; entries[n].size = 0; n++; }
    if (n < max) { strncpy(entries[n].name, "version", VFS_MAX_NAME - 1); entries[n].type = VFS_FILE; entries[n].size = 0; n++; }

    process_t* procs[MAX_PROCESSES];
    int pcount = sched_get_processes(procs, MAX_PROCESSES);
    for (int i = 0; i < pcount && n < max; i++) {
        char pname[16];
        snprintf(pname, sizeof(pname), "%d", procs[i]->pid);
        strncpy(entries[n].name, pname, VFS_MAX_NAME - 1);
        entries[n].name[VFS_MAX_NAME - 1] = '\0';
        entries[n].type = VFS_DIR;
        entries[n].size = 0;
        n++;
    }

    vfs_node_t* c = dir->children;
    while (c && n < max) {
        int dup = 0;
        for (u32 j = 0; j < n; j++) {
            if (strcmp(entries[j].name, c->name) == 0) { dup = 1; break; }
        }
        if (!dup) {
            strncpy(entries[n].name, c->name, VFS_MAX_NAME - 1);
            entries[n].name[VFS_MAX_NAME - 1] = '\0';
            entries[n].type = c->type;
            entries[n].size = 0;
            n++;
        }
        c = c->next;
    }

    *count = n;
    return 0;
}

static int procfs_stat(vfs_node_t* node, u64* size, u32* mode, u8* is_dir) {
    if (!node) return -1;
    if (size) *size = 0;
    if (mode) *mode = node->mode;
    if (is_dir) *is_dir = (node->type == VFS_DIR) ? 1 : 0;
    return 0;
}

static int procfs_mount(vfs_node_t** root, const char* dev) {
    (void)dev;
    if (!root) return -1;
    vfs_node_t* n = vfs_create_node("", VFS_DIR);
    if (!n) return -1;
    n->mode = 0555;
    n->fs = &procfs_ops;
    n->fs_data = (void*)(u64)0;
    *root = n;
    return 0;
}

static int procfs_unmount(vfs_node_t* root) {
    if (!root) return -1;
    vfs_node_t* c = root->children;
    while (c) {
        vfs_node_t* next = c->next;
        vfs_node_t* cc = c->children;
        while (cc) {
            vfs_node_t* cnext = cc->next;
            kfree(cc);
            cc = cnext;
        }
        kfree(c);
        c = next;
    }
    kfree(root);
    return 0;
}

static vfs_fs_ops_t procfs_ops = {
    .name = "procfs",
    .mount = procfs_mount,
    .unmount = procfs_unmount,
    .lookup = procfs_lookup_dispatch,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .read = procfs_read,
    .write = NULL,
    .readdir = procfs_readdir,
    .stat = procfs_stat,
    .rename = NULL,
    .format = NULL
};

int procfs_register(void) {
    return vfs_register_fs(&procfs_ops);
}

FS_REGISTER("procfs", procfs_register);
