// kernel/syscall.c
#include "syscall.h"
#include "sched.h"
#include "idt.h"
#include "../fs/ufs.h"
#include "../drivers/drm.h"
#include "../drivers/keyboard.h"
#include "../drivers/disk.h"
#include "../include/udisk.h"
#include "../net/tcp.h"
#include "../net/udp.h"
#include "../net/dns.h"
#include "../net/net.h"
#include "memory.h"
#include "paging.h"
#include "elf.h"
#include "../include/string.h"
#include "unix.h"

#define MAX_FDS 32

#define MSR_STAR       0xC0000081
#define MSR_LSTAR      0xC0000082
#define MSR_SFMASK     0xC0000084

#ifndef O_RDONLY
#define O_RDONLY   0x000
#define O_WRONLY   0x001
#define O_RDWR     0x002
#define O_CREAT    0x040
#define O_TRUNC    0x200
#define O_APPEND   0x400
#endif

extern void syscall_entry(void);

static inline void wrmsr(u32 msr, u64 val) {
    u32 low = val & 0xFFFFFFFF;
    u32 high = val >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

typedef struct {
    u32 size;
    u8 is_dir;
    u32 blocks;
} sys_stat_t;

static int is_user_pointer(void* ptr) {
    u64 addr = (u64)ptr;
    return (addr >= 0x40000000 && addr < 0x0000004000000000ULL);
}

static char fs_current_dir[256] = "/";

void fs_set_current_dir(const char* path) {
    if (path && path[0]) {
        strncpy(fs_current_dir, path, 255);
        fs_current_dir[255] = '\0';
    }
}

const char* fs_get_current_dir(void) {
    return fs_current_dir;
}

static int copy_from_user(void* dest, const void* src, u64 size) {
    process_t *p = sched_current();
    if (!p || !is_user_pointer((void*)src)) return -1;

    u64 old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    if (old_cr3 != p->cr3) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(p->cr3) : "memory");
    }

    memcpy(dest, src, size);

    if (old_cr3 != p->cr3) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
    }

    return 0;
}

static int copy_to_user(void* dest, const void* src, u64 size) {
    process_t *p = sched_current();
    if (!p || !is_user_pointer(dest)) return -1;

    u64 old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    if (old_cr3 != p->cr3) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(p->cr3) : "memory");
    }

    memcpy(dest, src, size);

    if (old_cr3 != p->cr3) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
    }

    return 0;
}

static long sys_partition_format(trap_frame_t* frame, long dev, long fstype, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    char dev_buf[32], fs_buf[16];
    if (!is_user_pointer((void*)dev) || !is_user_pointer((void*)fstype)) return -1;
    if (copy_from_user(dev_buf, (void*)dev, 31) != 0) return -1;
    dev_buf[31] = '\0';
    if (copy_from_user(fs_buf, (void*)fstype, 15) != 0) return -1;
    fs_buf[15] = '\0';
    return udisk_format_partition(dev_buf, fs_buf);
}

static long sys_disk_table(trap_frame_t* frame, long dev, long kind, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    char dev_buf[32];
    if (!is_user_pointer((void*)dev)) return -1;
    if (copy_from_user(dev_buf, (void*)dev, 31) != 0) return -1;
    dev_buf[31] = '\0';
    int disk, part;
    if (parse_devname(dev_buf, &disk, &part) != 0 || part != 0) return -1;
    if (kind == 1) return udisk_create_gpt(disk);
    return udisk_create_mbr(disk);
}

static long sys_partition_create(trap_frame_t* frame, long dev, long size_mb, long type, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    char dev_buf[32];
    if (!is_user_pointer((void*)dev)) return -1;
    if (copy_from_user(dev_buf, (void*)dev, 31) != 0) return -1;
    dev_buf[31] = '\0';
    return udisk_create_partition(dev_buf, (u64)size_mb, (partition_type_t)type);
}

static long sys_partition_delete(trap_frame_t* frame, long dev, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char dev_buf[32];
    if (!is_user_pointer((void*)dev)) return -1;
    if (copy_from_user(dev_buf, (void*)dev, 31) != 0) return -1;
    dev_buf[31] = '\0';
    return udisk_delete_partition(dev_buf);
}

static long sys_exit(trap_frame_t* frame, long code, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_exit(code);
    return 0;
}

static long sys_write(trap_frame_t* frame, long fd, long buf, long count, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (!is_user_pointer((void*)buf)) return -1;
    if (fd == 1 || fd == 2) {
        char temp[256];
        long left = count;
        long offset = 0;
        while (left > 0) {
            long chunk = left < 255 ? left : 255;
            if (copy_from_user(temp, (void*)((char*)buf + offset), chunk) != 0) return -1;
            for (long i = 0; i < chunk; i++) print_char(temp[i]);
            offset += chunk;
            left -= chunk;
        }
        return count;
    }
    if (p->fds[fd].type != 0) return -1;
    vfs_node_t* node = p->fds[fd].data.vnode;
    if (!node) return -1;
    u8* tmp_buf = kmalloc(count);
    if (!tmp_buf) return -1;
    if (copy_from_user(tmp_buf, (void*)buf, count) != 0) { kfree(tmp_buf); return -1; }
    if (p->fds[fd].flags & O_APPEND) p->fds[fd].pos = node->size;
    int res = vfs_write(node, tmp_buf, count, p->fds[fd].pos);
    if (res > 0) p->fds[fd].pos += res;
    kfree(tmp_buf);
    return res;
}

static long sys_read(trap_frame_t* frame, long fd, long buf, long count, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (!is_user_pointer((void*)buf)) return -1;
    if (fd == 0) {
        long read_count = 0;
        u8 *user_buf = (u8*)buf;
        while (!keyboard_data_ready()) { __asm__ volatile("sti"); sched_sleep(1); }
        while (read_count < count && keyboard_data_ready()) {
            char c = keyboard_getc();
            u8 tmp = (u8)c;
            if (copy_to_user(user_buf + read_count, &tmp, 1) != 0) return -1;
            read_count++;
        }
        return read_count;
    }
    if (p->fds[fd].type != 0) return -1;
    vfs_node_t* node = p->fds[fd].data.vnode;
    if (!node) return -1;
    u8* tmp_buf = kmalloc(count);
    if (!tmp_buf) return -1;
    int res = vfs_read(node, tmp_buf, count, p->fds[fd].pos);
    if (res > 0) {
        if (copy_to_user((void*)buf, tmp_buf, res) != 0) { kfree(tmp_buf); return -1; }
        p->fds[fd].pos += res;
    }
    kfree(tmp_buf);
    return res;
}

static long sys_open(trap_frame_t* frame, long path, long flags, long mode, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;
    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!p->fds[i].used) { fd = i; break; }
    }
    if (fd == -1) return -1;
    if (strncmp(path_buf, "/dev/dri/card0", 14) == 0) {
        p->fds[fd].used = 1;
        p->fds[fd].type = 1;
        return fd;
    }
    vfs_node_t* node = vfs_open(path_buf, (int)flags, (int)mode);
    if (!node) return -1;
    p->fds[fd].used = 1;
    p->fds[fd].type = 0;
    p->fds[fd].data.vnode = node;
    p->fds[fd].pos = (flags & O_APPEND) ? node->size : 0;
    p->fds[fd].flags = (int)flags;
    return fd;
}

static long sys_close(trap_frame_t* frame, long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (p->fds[fd].type == 0 && p->fds[fd].data.vnode) {
        vfs_close(p->fds[fd].data.vnode);
    } else if (p->fds[fd].type == FD_TYPE_UNIX) {
        unix_close(fd);
    }
    p->fds[fd].used = 0;
    memset(&p->fds[fd], 0, sizeof(fd_entry_t));
    return 0;
}

static long sys_brk(trap_frame_t* frame, long addr, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;

    if (addr == 0) return p->heap_end;

    if ((u64)addr > p->heap_end) {
        u64 old_page = (p->heap_end + 4095) & ~4095ULL;
        u64 new_page = ((u64)addr + 4095) & ~4095ULL;
        u64* pml4 = (u64*)p->cr3;

        u64 old_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
        if (old_cr3 != p->cr3) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(p->cr3) : "memory");
        }

        for (u64 virt = old_page; virt < new_page; virt += 4096) {
            u64 phys = (u64)pmm_alloc_page();
            if (!phys) {
                if (old_cr3 != p->cr3) {
                    __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
                }
                return -1;
            }
            paging_map_for_process(pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            memset((void*)virt, 0, 4096);
        }

        if (old_cr3 != p->cr3) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
        }
    }

    p->heap_end = addr;
    return addr;
}

static long sys_getpid(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_get_pid();
}

static long sys_getppid(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_get_ppid();
}

static long sys_sleep(trap_frame_t* frame, long ms, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_sleep(ms);
    return 0;
}

static long sys_yield(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_yield();
    return 0;
}

static long sys_mmap(trap_frame_t* frame, long addr, long size, long prot, long flags, long fd, long offset) {
    (void)frame; (void)addr; (void)prot; (void)flags;
    process_t *p = sched_current();
    if (!p) return -1;

    u64 pages = (size + 4095) / 4096;
    u64 virt = p->heap_end;

    if (fd >= 0 && fd < MAX_FDS && p->fds[fd].used && p->fds[fd].type == 1) {
        u64 phys = drm_mmap_fb(offset, size);
        if (phys == 0) return -1;
        u64* pml4 = (u64*)p->cr3;
        u64 old_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
        if (old_cr3 != p->cr3) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(p->cr3) : "memory");
        }
        for (u64 i = 0; i < pages; i++) {
            paging_map_for_process(pml4, phys + i * 4096, virt + i * 4096,
                                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        if (old_cr3 != p->cr3) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
        }
        p->heap_end = virt + pages * 4096;
        return virt;
    }

    u64* pml4 = (u64*)p->cr3;
    for (u64 i = 0; i < pages; i++) {
        u64 phys = (u64)kmalloc(4096);
        if (!phys) return -1;

        u64 p_flags = PAGE_PRESENT | PAGE_USER;
        if (prot & 2) p_flags |= PAGE_WRITABLE;

        if (paging_map_for_process(pml4, phys, virt + i * 4096, p_flags) != 0) {
            kfree((void*)phys);
            return -1;
        }

        u64 old_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
        memset((void*)(virt + i * 4096), 0, 4096);
        __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");
    }

    p->heap_end = virt + pages * 4096;
    return virt;
}

static long sys_munmap(trap_frame_t* frame, long addr, long size, long a3, long a4, long a5, long a6) {
    (void)frame; (void)addr; (void)size; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}

static int resolve_applet(const char* cmd, char* out_path) {
    u8* data = NULL;
    u32 size = 0;
    if (vfs_read_entire("/bin/applets", &data, &size) != 0) return -1;

    char* p = (char*)data;
    char* end = p + size;
    int cmd_len = strlen(cmd);

    while (p < end) {
        char* line_start = p;
        while (p < end && *p != '\n') p++;

        char* c = line_start;
        while (c < p && (*c == ' ' || *c == '\t')) c++;
        if (c >= p) { if (p < end) p++; continue; }

        char* cmd_start = c;
        while (c < p && *c != ' ' && *c != '\t') c++;
        int cur_cmd_len = c - cmd_start;

        while (c < p && (*c == ' ' || *c == '\t')) c++;
        if (c >= p) { if (p < end) p++; continue; }

        char* bin_start = c;
        while (c < p && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n') c++;
        int bin_len = c - bin_start;

        if (cur_cmd_len == cmd_len && strncmp(cmd_start, cmd, cmd_len) == 0) {
            if (bin_len > 0 && bin_len < 200) {
                int has_slash = 0;
                for (int i = 0; i < bin_len; i++) {
                    if (bin_start[i] == '/') has_slash = 1;
                }

                if (has_slash) {
                    memcpy(out_path, bin_start, bin_len);
                    out_path[bin_len] = '\0';
                } else {
                    out_path[0] = '/';
                    out_path[1] = 'b';
                    out_path[2] = 'i';
                    out_path[3] = 'n';
                    out_path[4] = '/';
                    memcpy(out_path + 5, bin_start, bin_len);
                    out_path[5 + bin_len] = '\0';
                }
                kfree(data);
                return 0;
            }
        }
        if (p < end) p++;
    }
    kfree(data);
    return -1;
}

static long sys_exec(trap_frame_t* frame, long path, long argv_ptr, long envp_ptr, long a4, long a5, long a6) {
    (void)envp_ptr; (void)a4; (void)a5; (void)a6;
    __asm__ volatile("cli");

    process_t *p = sched_current();
    if (!p) return -1;
    if (!is_user_pointer((void*)path)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    char (*argv_buf)[256] = kmalloc(64 * 256);
    if (!argv_buf) return -1;

    int argc = 0;
    if (argv_ptr && is_user_pointer((void*)argv_ptr)) {
        for (int i = 0; i < 63; i++) {
            u64 str_ptr;
            if (copy_from_user(&str_ptr, (void*)((char*)argv_ptr + i * 8), 8) != 0) break;
            if (str_ptr == 0) break;
            if (!is_user_pointer((void*)str_ptr)) break;
            if (copy_from_user(argv_buf[argc], (void*)str_ptr, 255) != 0) break;
            argv_buf[argc][255] = '\0';
            argc++;
        }
    }

    if (argc == 0) {
        const char* name = path_buf;
        const char* slash = strrchr(path_buf, '/');
        if (slash) name = slash + 1;
        strncpy(argv_buf[0], name, 255);
        argv_buf[0][255] = '\0';
        argc = 1;
    }

    u8 *elf_data = NULL;
    u32 elf_size = 0;
    char resolved_path[256];
    strncpy(resolved_path, path_buf, 255);
    resolved_path[255] = '\0';

    extern int vfs_readlink(const char*, char*, u32);
    for (int depth = 0; depth < 8; depth++) {
        char link_target[256];
        int res = vfs_readlink(resolved_path, link_target, 255);
        if (res <= 0) break;
        link_target[255] = '\0';

        if (link_target[0] == '/') {
            strncpy(resolved_path, link_target, 255);
            resolved_path[255] = '\0';
        } else {
            char dir[256];
            strncpy(dir, resolved_path, 255);
            dir[255] = '\0';

            char* slash = strrchr(dir, '/');
            if (slash && slash != dir) {
                *slash = '\0';
            } else if (slash == dir) {
                dir[1] = '\0';
            } else {
                strcpy(dir, "/");
            }

            char tmp[512];
            if (strcmp(dir, "/") == 0) {
                strcpy(tmp, "/");
                strcat(tmp, link_target);
            } else {
                strcpy(tmp, dir);
                strcat(tmp, "/");
                strcat(tmp, link_target);
            }

            strncpy(resolved_path, tmp, 255);
            resolved_path[255] = '\0';
        }
    }

    if (vfs_read_entire(resolved_path, &elf_data, &elf_size) != 0) {
        extern int get_module_data(const char* name, u8** buf, u32* size);
        const char* mod_name = strrchr(resolved_path, '/');
        if (mod_name) mod_name++;
        else mod_name = resolved_path;

        if (get_module_data(mod_name, &elf_data, &elf_size) != 0) {
            kfree(argv_buf);
            return -1;
        }
    }

    u64* new_pml4 = create_address_space();
    if (!new_pml4) {
        kfree(elf_data);
        kfree(argv_buf);
        return -1;
    }

    u64 max_vaddr = 0;
    u64 entry = elf_load(elf_data, elf_size, new_pml4, &max_vaddr);
    print("SYS_EXEC: path="); print(path_buf); print(" entry="); printhex(entry); print("\n");

    if (entry == 0) {
        free_address_space(new_pml4);
        kfree(elf_data);
        kfree(argv_buf);
        return -1;
    }

    u64 user_stack_top = 0x0000004000000000ULL;
    u64 stack_pages = 64;
    for (u64 i = 0; i < stack_pages; i++) {
        u64 phys = (u64)pmm_alloc_page();
        if (!phys) {
            free_address_space(new_pml4);
            kfree(elf_data);
            kfree(argv_buf);
            return -1;
        }

        u64 virt = user_stack_top - (stack_pages - i) * 4096;
        if (paging_map_for_process(new_pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page((void*)phys);
            free_address_space(new_pml4);
            kfree(elf_data);
            kfree(argv_buf);
            return -1;
        }
    }

    u64 old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(new_pml4) : "memory");

    u64 rsp = user_stack_top;
    rsp &= ~0xFULL;

    u64 argv_ptrs[64];
    for (int i = 0; i < argc; i++) {
        u64 len = strlen(argv_buf[i]) + 1;
        rsp -= len;
        rsp &= ~0xFULL;
        memcpy((void*)rsp, argv_buf[i], len);
        argv_ptrs[i] = rsp;
    }

    u8 random_bytes[16] = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };

    rsp -= 16;
    rsp &= ~0xFULL;
    memcpy((void*)rsp, random_bytes, 16);
    u64 at_random_ptr = rsp;

    u64 auxv[8];
    auxv[0] = 6;
    auxv[1] = 4096;
    auxv[2] = 25;
    auxv[3] = at_random_ptr;
    auxv[4] = 9;
    auxv[5] = entry;
    auxv[6] = 0;
    auxv[7] = 0;

    u64 args_area = ((u64)argc + 3) * 8 + sizeof(auxv);
    rsp -= args_area;
    rsp &= ~0xFULL;

    *(u64*)rsp = (u64)argc;

    u64 argv_base = rsp + 8;
    for (int i = 0; i < argc; i++) {
        *(u64*)(argv_base + (u64)i * 8) = argv_ptrs[i];
    }
    *(u64*)(argv_base + (u64)argc * 8) = 0;

    u64 envp_base = argv_base + ((u64)argc + 1) * 8;
    *(u64*)envp_base = 0;

    u64 auxv_base = envp_base + 8;
    memcpy((void*)auxv_base, auxv, sizeof(auxv));

    p->cr3 = (u64)new_pml4;
    p->user_rip = entry;
    p->user_rsp = rsp;
    p->heap_start = (max_vaddr + 4095) & ~4095ULL;
    p->heap_end = p->heap_start;

    frame->rax = 0;
    frame->rcx = entry;
    frame->r11 = 0x202;
    frame->user_rsp = rsp;
    frame->rdi = argc;
    frame->rsi = argv_base;
    frame->rdx = envp_base;
    frame->r10 = 0;
    frame->r8 = 0;
    frame->r9 = 0;
    frame->rbp = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;

    if (old_cr3 != (u64)new_pml4) {
        u64* old_pml4 = (u64*)old_cr3;
        if (old_pml4 && old_pml4 != (u64*)0x1000) {
            free_address_space(old_pml4);
        }
    }

    kfree(elf_data);
    kfree(argv_buf);
    return 0;
}

static long sys_waitpid(trap_frame_t* frame, long pid, long status, long options, long a4, long a5, long a6) {
    (void)frame; (void)options; (void)a4; (void)a5; (void)a6;
    return sched_waitpid(pid, (int*)status);
}

static long sys_kill(trap_frame_t* frame, long pid, long sig, long a3, long a4, long a5, long a6) {
    (void)frame; (void)sig; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_kill(pid);
}

static long sys_lseek(trap_frame_t* frame, long fd, long offset, long whence, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used || p->fds[fd].type != 0) return -1;
    vfs_node_t* node = p->fds[fd].data.vnode;
    if (!node) return -1;
    switch (whence) {
        case 0: p->fds[fd].pos = offset; break;
        case 1: p->fds[fd].pos += offset; break;
        case 2: p->fds[fd].pos = node->size + offset; break;
        default: return -1;
    }
    return p->fds[fd].pos;
}

static long sys_stat(trap_frame_t* frame, long path, long statbuf, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)statbuf)) return -1;
    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';
    vfs_node_t* node = vfs_resolve_path(path_buf);
    if (!node) return -1;
    sys_stat_t st;
    u64 size; u32 mode; u8 is_dir;
    vfs_stat(node, &size, &mode, &is_dir);
    st.size = (u32)size;
    st.is_dir = is_dir;
    st.blocks = (st.size + 511) / 512;
    if (copy_to_user((void*)statbuf, &st, sizeof(st)) != 0) return -1;
    return 0;
}

static long sys_fstat(trap_frame_t* frame, long fd, long statbuf, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (!is_user_pointer((void*)statbuf)) return -1;

    sys_stat_t st;
    if (p->fds[fd].type == 0) {
        vfs_node_t* node = p->fds[fd].data.vnode;
        if (!node) return -1;
        u64 size; u32 mode; u8 is_dir;
        vfs_stat(node, &size, &mode, &is_dir);
        st.size = (u32)size;
        st.is_dir = is_dir;
        st.blocks = (st.size + 511) / 512;
    } else {
        st.size = 0;
        st.is_dir = 0;
        st.blocks = 0;
    }

    if (copy_to_user((void*)statbuf, &st, sizeof(st)) != 0) return -1;
    return 0;
}

static long sys_mkdir(trap_frame_t* frame, long path, long mode, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';
    return vfs_mkdir(path_buf, (u32)mode);
}

static long sys_rmdir(trap_frame_t* frame, long path, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';
    return vfs_rmdir(path_buf);
}

static long sys_unlink(trap_frame_t* frame, long path, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';
    return vfs_unlink(path_buf);
}

static long sys_rename(trap_frame_t* frame, long old, long new, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)old) || !is_user_pointer((void*)new)) return -1;
    char old_buf[256], new_buf[256];
    if (copy_from_user(old_buf, (void*)old, 255) != 0) return -1;
    if (copy_from_user(new_buf, (void*)new, 255) != 0) return -1;
    old_buf[255] = '\0';
    new_buf[255] = '\0';
    return vfs_rename(old_buf, new_buf);
}

static long sys_chdir(trap_frame_t* frame, long path, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    if (!vfs_isdir(path_buf)) return -1;

    void fs_set_current_dir(const char*);
    fs_set_current_dir(path_buf);
    return 0;
}

static long sys_getcwd(trap_frame_t* frame, long buf, long size, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;

    const char* fs_get_current_dir(void);
    const char* cwd = fs_get_current_dir();
    if (!cwd) cwd = "/";

    unsigned long len = strlen(cwd) + 1;
    if (len > (unsigned long)size) return -1;

    if (copy_to_user((void*)buf, cwd, len) != 0) return -1;
    return len;
}

static long sys_readdir(trap_frame_t* frame, long path, long entries, long count, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)entries)) return -1;
    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    vfs_node_t* dir = vfs_resolve_path(path_buf);
    if (!dir) return -1;

    vfs_dirent_t* kernel_entries = kmalloc(count * sizeof(vfs_dirent_t));
    if (!kernel_entries) return -1;

    u32 kernel_count = (u32)count;
    if (vfs_readdir(dir, kernel_entries, &kernel_count) != 0) {
        kfree(kernel_entries);
        return -1;
    }

    for (u32 i = 0; i < kernel_count; i++) {
        vfs_user_dirent_t user_entry;
        memset(&user_entry, 0, sizeof(user_entry));
        strncpy(user_entry.name, kernel_entries[i].name, VFS_MAX_NAME - 1);
        user_entry.name[VFS_MAX_NAME - 1] = '\0';
        user_entry.size = (u32)kernel_entries[i].size;
        user_entry.is_dir = (kernel_entries[i].type == VFS_DIR) ? 1 : 0;

        if (copy_to_user((void*)((char*)entries + i * sizeof(vfs_user_dirent_t)), &user_entry, sizeof(vfs_user_dirent_t)) != 0) {
            kfree(kernel_entries);
            return -1;
        }
    }

    kfree(kernel_entries);
    return kernel_count;
}

static long sys_disk_list(trap_frame_t* frame, long disks_ptr, long max, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)disks_ptr)) return -1;

    udisk_scan();

    int count = 0;
    for (int i = 0; i < 4 && count < max; i++) {
        disk_info_t* d = udisk_get_info(i);
        if (d && d->present) {
            if (copy_to_user((void*)((char*)disks_ptr + count * sizeof(disk_info_t)), d, sizeof(disk_info_t)) != 0) {
                return -1;
            }
            count++;
        }
    }
    return count;
}

static long sys_partition_mount(trap_frame_t* frame, long dev, long point, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)dev) || !is_user_pointer((void*)point)) return -1;

    char dev_buf[32], point_buf[256];
    if (copy_from_user(dev_buf, (void*)dev, 31) != 0) return -1;
    if (copy_from_user(point_buf, (void*)point, 255) != 0) return -1;
    dev_buf[31] = '\0';
    point_buf[255] = '\0';

    if (vfs_is_mounted(point_buf)) return -1;
    if (vfs_mount_fs("ufs", dev_buf, point_buf) != 0) return -1;

    void fs_set_current_dir(const char*);
    fs_set_current_dir(point_buf);
    return 0;
}

static long sys_partition_umount(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return vfs_unmount("/");
}

static long sys_socket(trap_frame_t* frame, long domain, long type, long protocol, long a4, long a5, long a6) {
    (void)frame; (void)protocol; (void)a4; (void)a5; (void)a6;
    if (domain == 1) return unix_socket_create();
    if (domain == 2) return tcp_socket();
    return -1;
}

static long sys_connect(trap_frame_t* frame, long fd, long addr, long addrlen, long a4, long a5, long a6) {
    (void)frame; (void)addrlen; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;

    u16 family;
    if (copy_from_user(&family, (void*)addr, 2) != 0) return -1;

    if (p->fds[fd].type == FD_TYPE_UNIX) {
        struct { u16 fam; char path[108]; } un;
        if (copy_from_user(&un, (void*)addr, sizeof(un)) != 0) return -1;
        return unix_connect(fd, un.path);
    }
    if (p->fds[fd].type == FD_TYPE_TCP || family == 2) {
        struct { u16 fam; u16 port; u32 ip; } in;
        if (copy_from_user(&in, (void*)addr, sizeof(in)) != 0) return -1;
        return tcp_connect(fd, in.ip, in.port);
    }
    return -1;
}

static long sys_send(trap_frame_t* frame, long fd, long buf, long len, long flags, long a5, long a6) {
    (void)frame; (void)flags; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (!is_user_pointer((void*)buf)) return -1;

    if (p->fds[fd].type == FD_TYPE_UNIX) {
        u8 *data = kmalloc(len);
        if (!data) return -1;
        if (copy_from_user(data, (void*)buf, len) != 0) { kfree(data); return -1; }
        int res = unix_send(fd, data, len);
        kfree(data);
        return res;
    }
    if (p->fds[fd].type == FD_TYPE_TCP) {
        u8 *data = kmalloc(len);
        if (!data) return -1;
        if (copy_from_user(data, (void*)buf, len) != 0) { kfree(data); return -1; }
        int res = tcp_send(fd, data, len);
        kfree(data);
        return res;
    }
    return -1;
}

static long sys_recv(trap_frame_t* frame, long fd, long buf, long len, long flags, long a5, long a6) {
    (void)frame; (void)flags; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (!is_user_pointer((void*)buf)) return -1;

    if (p->fds[fd].type == FD_TYPE_UNIX) {
        u8 *data = kmalloc(len);
        if (!data) return -1;
        int res = unix_recv(fd, data, len);
        if (res > 0) {
            if (copy_to_user((void*)buf, data, res) != 0) { kfree(data); return -1; }
        }
        kfree(data);
        return res;
    }
    if (p->fds[fd].type == FD_TYPE_TCP) {
        u8 *data = kmalloc(len);
        if (!data) return -1;
        int res = tcp_recv(fd, data, len);
        if (res > 0) {
            if (copy_to_user((void*)buf, data, res) != 0) { kfree(data); return -1; }
        }
        kfree(data);
        return res;
    }
    return -1;
}

static long sys_gethostbyname(trap_frame_t* frame, long name, long ip, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)name) || !is_user_pointer((void*)ip)) return -1;

    char name_buf[256];
    if (copy_from_user(name_buf, (void*)name, 255) != 0) return -1;
    name_buf[255] = '\0';

    u32 ip_addr = dns_lookup(name_buf, net_get_dns());
    if (ip_addr == 0) return -1;

    if (copy_to_user((void*)ip, &ip_addr, 4) != 0) return -1;
    return 0;
}

static long sys_getip(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return net_get_ip();
}

static long sys_meminfo(trap_frame_t* frame, long total, long used, long free, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)total) || !is_user_pointer((void*)used) || !is_user_pointer((void*)free))
        return -1;

    u64 t = memory_used() + memory_free();
    u64 u = memory_used();
    u64 f = memory_free();

    if (copy_to_user((void*)total, &t, 8) != 0) return -1;
    if (copy_to_user((void*)used, &u, 8) != 0) return -1;
    if (copy_to_user((void*)free, &f, 8) != 0) return -1;
    return 0;
}

static long sys_ps(trap_frame_t* frame, long processes, long max, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)processes)) return -1;

    process_t* kernel_procs[MAX_PROCESSES];
    int count = sched_get_processes(kernel_procs, MAX_PROCESSES);

    int to_copy = (count < max) ? count : max;

    typedef struct {
        int pid;
        int ppid;
        char name[32];
        int state;
    } ps_entry_t;

    for (int i = 0; i < to_copy; i++) {
        ps_entry_t entry;
        entry.pid = kernel_procs[i]->pid;
        entry.ppid = kernel_procs[i]->ppid;
        strcpy(entry.name, kernel_procs[i]->name);
        entry.state = kernel_procs[i]->state;
        if (copy_to_user((void*)((char*)processes + i * sizeof(ps_entry_t)), &entry, sizeof(ps_entry_t)) != 0) {
            return -1;
        }
    }

    return to_copy;
}

static long sys_gettime(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return get_ticks();
}

static long sys_ioctl(trap_frame_t* frame, long fd, long request, long arg, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;

    if (p->fds[fd].type == 1) {
        u32 size = (request >> 16) & 0x3FFF;
        void *kdata = NULL;
        if (size > 0 && arg != 0) {
            kdata = kmalloc(size);
            if (copy_from_user(kdata, (void*)arg, size) != 0) {
                kfree(kdata);
                return -1;
            }
        }

        int ret = drm_ioctl(request, (unsigned long)kdata);

        if (size > 0 && arg != 0 && ret >= 0) {
            copy_to_user((void*)arg, kdata, size);
        }
        if (kdata) kfree(kdata);
        return ret;
    }
    return -1;
}

static long sys_clone(trap_frame_t* frame, long rip, long rsp, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_clone((u64)rip, (u64)rsp);
}

static long sys_fork(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_fork(frame);
}

static long sys_dup(trap_frame_t* frame, long oldfd, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || oldfd < 0 || oldfd >= 32 || !p->fds[oldfd].used) return -1;

    int newfd = -1;
    for (int i = 0; i < 32; i++) {
        if (!p->fds[i].used) {
            newfd = i;
            break;
        }
    }
    if (newfd == -1) return -1;

    p->fds[newfd] = p->fds[oldfd];
    return newfd;
}

static long sys_dup2(trap_frame_t* frame, long oldfd, long newfd, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || oldfd < 0 || oldfd >= 32 || !p->fds[oldfd].used) return -1;
    if (newfd < 0 || newfd >= 32) return -1;

    if (oldfd == newfd) return newfd;

    p->fds[newfd] = p->fds[oldfd];
    p->fds[newfd].used = 1;
    return newfd;
}

static long sys_clear(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    print_clear();
    return 0;
}

static long sys_setcolor(trap_frame_t* frame, long fg, long bg, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    print_setcolor((u8)fg, (u8)bg);
    return 0;
}

typedef long (*syscall_t)(trap_frame_t*, long, long, long, long, long, long);
static syscall_t syscall_table[64];

static long sys_bind(trap_frame_t* frame, long fd, long addr, long addrlen, long a4, long a5, long a6) {
    (void)frame; (void)addrlen; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;

    struct { u16 fam; char path[108]; } un;
    if (copy_from_user(&un, (void*)addr, sizeof(un)) != 0) return -1;
    return unix_bind(fd, un.path);
}

static long sys_listen(trap_frame_t* frame, long fd, long backlog, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;
    return unix_listen(fd, backlog);
}

static long sys_accept(trap_frame_t* frame, long fd, long addr, long addrlen, long a4, long a5, long a6) {
    (void)frame; (void)addr; (void)addrlen; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != FD_TYPE_UNIX) return -1;
    return unix_accept(fd);
}

static long sys_symlink(trap_frame_t* frame, long target, long linkpath, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    char t_buf[256], l_buf[256];
    if (copy_from_user(t_buf, (void*)target, 255) != 0) return -1;
    if (copy_from_user(l_buf, (void*)linkpath, 255) != 0) return -1;
    t_buf[255] = '\0'; l_buf[255] = '\0';
    return vfs_symlink(t_buf, l_buf);
}

static long sys_readlink(trap_frame_t* frame, long path, long buf, long size, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    char p_buf[256], k_buf[256];
    if (copy_from_user(p_buf, (void*)path, 255) != 0) return -1;
    p_buf[255] = '\0';
    int res = vfs_readlink(p_buf, k_buf, 256);
    if (res < 0) return -1;
    if (copy_to_user((void*)buf, k_buf, res + 1) != 0) return -1;
    return res;
}

static long sys_fs_register(trap_frame_t* frame, long name, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)name)) return -1;
    char name_buf[32];
    if (copy_from_user(name_buf, (void*)name, 31) != 0) return -1;
    name_buf[31] = '\0';

    if (strcmp(name_buf, "ufs") == 0) {
        extern int ufs_register(void);
        return ufs_register();
    }

    return -1;
}

int syscall_init(void) {
    for (int i = 0; i < 64; i++) syscall_table[i] = NULL;

    syscall_table[0] = sys_exit;
    syscall_table[1] = sys_read;
    syscall_table[2] = sys_write;
    syscall_table[3] = sys_open;
    syscall_table[4] = sys_close;
    syscall_table[5] = sys_brk;
    syscall_table[6] = sys_getpid;
    syscall_table[7] = sys_getppid;
    syscall_table[8] = sys_sleep;
    syscall_table[9] = sys_yield;
    syscall_table[10] = sys_mmap;
    syscall_table[11] = sys_munmap;
    syscall_table[12] = sys_exec;
    syscall_table[13] = sys_waitpid;
    syscall_table[14] = sys_kill;
    syscall_table[15] = sys_lseek;
    syscall_table[16] = sys_stat;
    syscall_table[17] = sys_fstat;
    syscall_table[18] = sys_mkdir;
    syscall_table[19] = sys_rmdir;
    syscall_table[20] = sys_unlink;
    syscall_table[21] = sys_rename;
    syscall_table[22] = sys_chdir;
    syscall_table[23] = sys_getcwd;
    syscall_table[24] = sys_readdir;
    syscall_table[25] = sys_dup;
    syscall_table[26] = sys_dup2;
    syscall_table[27] = sys_ioctl;
    syscall_table[28] = sys_clone;
    syscall_table[30] = sys_disk_list;
    syscall_table[37] = sys_partition_mount;
    syscall_table[38] = sys_partition_umount;
    syscall_table[39] = sys_partition_format;
    syscall_table[40] = sys_socket;
    syscall_table[41] = sys_connect;
    syscall_table[42] = sys_disk_table;
    syscall_table[43] = sys_partition_create;
    syscall_table[44] = sys_partition_delete;
    syscall_table[45] = sys_send;
    syscall_table[46] = sys_recv;
    syscall_table[47] = sys_gethostbyname;
    syscall_table[48] = sys_bind;
    syscall_table[49] = sys_listen;
    syscall_table[50] = sys_accept;
    syscall_table[51] = sys_ps;
    syscall_table[52] = sys_gettime;
    syscall_table[53] = sys_clear;
    syscall_table[54] = sys_setcolor;
    syscall_table[55] = sys_symlink;
    syscall_table[56] = sys_readlink;
    syscall_table[57] = sys_fork;
    syscall_table[58] = sys_fs_register;

    wrmsr(MSR_LSTAR, (u64)syscall_entry);

    u64 star = ((u64)0x18 << 48) | ((u64)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_SFMASK, 0x200);
    wrmsr(0xC0000102, 0);

    u64 efer;
    __asm__ volatile("rdmsr" : "=a"(((u32*)&efer)[0]), "=d"(((u32*)&efer)[1]) : "c"(0xC0000080));
    efer |= 1;
    __asm__ volatile("wrmsr" : : "c"(0xC0000080), "a"((u32)efer), "d"((u32)(efer >> 32)));

    return 0;
}

long syscall_handler_c(trap_frame_t* frame, long num) {
    long a1 = frame->rdi;
    long a2 = frame->rsi;
    long a3 = frame->rdx;
    long a4 = frame->r10;
    long a5 = frame->r8;
    long a6 = frame->r9;

    if (num < 0 || num >= 64 || !syscall_table[num]) {
        frame->rax = -1;
        return -1;
    }

    long ret = syscall_table[num](frame, a1, a2, a3, a4, a5, a6);
    frame->rax = ret;
    return ret;
}
