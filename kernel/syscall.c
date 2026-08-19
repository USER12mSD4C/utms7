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
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
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

    if (!p->fds[fd].used || p->fds[fd].type != 0) return -1;
    file_data_t* f = &p->fds[fd].data.file;

    if (f->flags & O_APPEND) f->pos = f->buf_size;

    u32 new_size = f->pos + count;
    if (new_size > f->buf_capacity) {
        u32 new_cap = f->buf_capacity;
        while (new_cap < new_size) {
            new_cap *= 2;
            if (new_cap == 0) new_cap = 4096;
        }
        u8* new_buf = kmalloc(new_cap);
        if (!new_buf) return -1;
        if (f->buf && f->buf_size > 0) memcpy(new_buf, f->buf, f->buf_size);
        if (f->buf) kfree(f->buf);
        f->buf = new_buf;
        f->buf_capacity = new_cap;
    }

    if (copy_from_user(f->buf + f->pos, (void*)buf, count) != 0) return -1;
    f->pos += count;
    if (f->pos > f->buf_size) f->buf_size = f->pos;
    f->dirty = 1;
    return count;
}

static long sys_read(trap_frame_t* frame, long fd, long buf, long count, long a4, long a5, long a6) {
    (void)frame; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    if (!is_user_pointer((void*)buf)) return -1;

    if (fd == 0) {
        long read_count = 0;
        u8 *user_buf = (u8*)buf;

        while (!keyboard_data_ready()) {
            __asm__ volatile("sti");
            sched_sleep(1);
        }

        while (read_count < count && keyboard_data_ready()) {
            char c = keyboard_getc();
            u8 tmp = (u8)c;
            if (copy_to_user(user_buf + read_count, &tmp, 1) != 0) return -1;
            read_count++;
        }

        return read_count;
    }

    if (!p->fds[fd].used || p->fds[fd].type != 0) return -1;
    file_data_t* f = &p->fds[fd].data.file;
    if (f->pos >= f->buf_size) return 0;

    long to_copy = count;
    if (f->pos + to_copy > f->buf_size) to_copy = f->buf_size - f->pos;

    if (copy_to_user((void*)buf, f->buf + f->pos, to_copy) != 0) return -1;
    f->pos += to_copy;
    return to_copy;
}

static long sys_open(trap_frame_t* frame, long path, long flags, long mode, long a4, long a5, long a6) {
    (void)frame; (void)mode; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;

    char path_buf[256];
    if (is_user_pointer((void*)path)) {
        if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
        path_buf[255] = '\0';
    } else {
        strncpy(path_buf, (const char*)path, 255);
        path_buf[255] = '\0';
    }

    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!p->fds[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;

    if (strncmp(path_buf, "/dev/dri/card0", 14) == 0) {
        p->fds[fd].used = 1;
        p->fds[fd].type = 1;
        return fd;
    }

    u8* file_buf = NULL;
    u32 file_size = 0;

    if (ufs_exists(path_buf)) {
        if (flags & O_TRUNC) {
            file_buf = kmalloc(4096);
            if (!file_buf) return -1;
            file_size = 0;
        } else {
            if (ufs_read(path_buf, &file_buf, &file_size) != 0) {
                file_buf = kmalloc(4096);
                if (!file_buf) return -1;
                file_size = 0;
            }
        }
    } else {
        if (flags & O_CREAT) {
            file_buf = kmalloc(4096);
            if (!file_buf) return -1;
            file_size = 0;
        } else {
            return -1;
        }
    }

    p->fds[fd].used = 1;
    p->fds[fd].type = 0;
    strcpy(p->fds[fd].data.file.path, path_buf);
    p->fds[fd].data.file.buf = file_buf;
    p->fds[fd].data.file.buf_size = file_size;
    p->fds[fd].data.file.buf_capacity = (file_buf == NULL) ? 0 : 4096;
    p->fds[fd].data.file.pos = (flags & O_APPEND) ? file_size : 0;
    p->fds[fd].data.file.dirty = 0;
    p->fds[fd].data.file.flags = flags & 0xFFF;

    return fd;
}

static long sys_close(trap_frame_t* frame, long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;

    if (p->fds[fd].used) {
        if (p->fds[fd].type == FD_TYPE_FILE) {
            file_data_t* f = &p->fds[fd].data.file;
            if (f->dirty && f->buf) ufs_write(f->path, f->buf, f->buf_size);
            if (f->buf) kfree(f->buf);
        } else if (p->fds[fd].type == FD_TYPE_UNIX) {
            unix_close(fd);
        }
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

static long sys_exec(trap_frame_t* frame, long path, long argv_ptr, long envp_ptr, long a4, long a5, long a6) {
    (void)envp_ptr; (void)a4; (void)a5; (void)a6;

    process_t *p = sched_current();
    if (!p) return -1;

    if (!is_user_pointer((void*)path)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    char argv_buf[64][256];
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

    if (ufs_read(path_buf, &elf_data, &elf_size) != 0) {
        extern int get_module_data(const char* name, u8** buf, u32* size);

        const char* mod_name = strrchr(path_buf, '/');
        if (mod_name) mod_name++;
        else mod_name = path_buf;

        if (get_module_data(mod_name, &elf_data, &elf_size) != 0) {
            return -1;
        }
    }

    u64* new_pml4 = create_address_space();
    if (!new_pml4) {
        kfree(elf_data);
        return -1;
    }

    u64 max_vaddr = 0;
    u64 entry = elf_load(elf_data, elf_size, new_pml4, &max_vaddr);
    if (entry == 0) {
        free_address_space(new_pml4);
        kfree(elf_data);
        return -1;
    }

    u64 user_stack_top = 0x0000004000000000ULL;
    u64 stack_pages = 64;

    for (u64 i = 0; i < stack_pages; i++) {
        u64 phys = (u64)pmm_alloc_page();
        if (!phys) {
            free_address_space(new_pml4);
            kfree(elf_data);
            return -1;
        }

        u64 virt = user_stack_top - (stack_pages - i) * 4096;
        if (paging_map_for_process(new_pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page((void*)phys);
            free_address_space(new_pml4);
            kfree(elf_data);
            return -1;
        }
    }

    __asm__ volatile ("cli");

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
        0x12, 0x34, 0x56, 0x78,
        0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88
    };

    rsp -= 16;
    memcpy((void*)rsp, random_bytes, 16);
    u64 at_random_ptr = rsp;

    u64 auxv[] = {
        6, 4096,
        25, at_random_ptr,
        9, entry,
        0, 0
    };

    rsp -= sizeof(auxv);
    memcpy((void*)rsp, auxv, sizeof(auxv));

    rsp -= 8;
    *(u64*)rsp = 0;
    u64 envp_base = rsp;

    rsp -= (u64)(argc + 1) * 8;
    rsp &= ~0xFULL;

    for (int i = 0; i < argc; i++) {
        *(u64*)(rsp + 8 + (u64)i * 8) = argv_ptrs[i];
    }

    *(u64*)(rsp + 8 + (u64)argc * 8) = 0;

    u64 argv_base = rsp + 8;
    *(u64*)rsp = (u64)argc;

    p->cr3 = (u64)new_pml4;
    p->user_rip = entry;
    p->user_rsp = rsp;
    p->heap_start = (max_vaddr + 4095) & ~4095ULL;
    p->heap_end = p->heap_start;

    frame->rax = 0;
    frame->rbx = 0;
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
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (p->fds[fd].type != 0) return -1;

    file_data_t* f = &p->fds[fd].data.file;

    switch (whence) {
        case 0: f->pos = offset; break;
        case 1: f->pos += offset; break;
        case 2: f->pos = f->buf_size + offset; break;
        default: return -1;
    }

    if ((long)f->pos < 0) f->pos = 0;
    return f->pos;
}

static long sys_stat(trap_frame_t* frame, long path, long statbuf, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)statbuf)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    if (!ufs_exists(path_buf)) return -1;

    sys_stat_t st;
    st.size = ufs_file_size(path_buf);
    st.is_dir = ufs_isdir(path_buf) ? 1 : 0;
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
        file_data_t* f = &p->fds[fd].data.file;
        st.size = f->buf_size;
        st.is_dir = ufs_isdir(f->path) ? 1 : 0;
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
    (void)frame; (void)mode; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    return ufs_mkdir(path_buf);
}

static long sys_rmdir(trap_frame_t* frame, long path, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    return ufs_rmdir(path_buf);
}

static long sys_unlink(trap_frame_t* frame, long path, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char path_buf[256];
    if (is_user_pointer((void*)path)) {
        if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
        path_buf[255] = '\0';
    } else {
        strncpy(path_buf, (const char*)path, 255);
        path_buf[255] = '\0';
    }

    return ufs_delete(path_buf);
}

static long sys_rename(trap_frame_t* frame, long old, long new, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)old) || !is_user_pointer((void*)new)) return -1;

    char old_buf[256], new_buf[256];
    if (copy_from_user(old_buf, (void*)old, 255) != 0) return -1;
    if (copy_from_user(new_buf, (void*)new, 255) != 0) return -1;

    return ufs_mv(old_buf, new_buf);
}

static long sys_chdir(trap_frame_t* frame, long path, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;

    char path_buf[256];
    if (copy_from_user(path_buf, (void*)path, 255) != 0) return -1;
    path_buf[255] = '\0';

    if (!ufs_isdir(path_buf)) return -1;

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

    FSNode* kernel_entries;
    u32 kernel_count;

    if (ufs_readdir(path_buf, &kernel_entries, &kernel_count) != 0) return -1;

    u32 to_copy = kernel_count;
    if (count && to_copy > (u32)count) to_copy = count;

    for (u32 i = 0; i < to_copy; i++) {
        FSNode user_entry;
        memset(&user_entry, 0, sizeof(FSNode));
        strcpy(user_entry.name, kernel_entries[i].name);
        user_entry.size = kernel_entries[i].size;
        user_entry.is_dir = kernel_entries[i].is_dir;
        user_entry.mode = kernel_entries[i].mode;
        user_entry.mtime = kernel_entries[i].mtime;

        if (copy_to_user((void*)((char*)entries + i * sizeof(FSNode)), &user_entry, sizeof(FSNode)) != 0) {
            kfree(kernel_entries);
            return -1;
        }
    }

    kfree(kernel_entries);
    return to_copy;
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

    char dev_buf[32];
    char point_buf[256];
    if (copy_from_user(dev_buf, (void*)dev, 31) != 0) return -1;
    if (copy_from_user(point_buf, (void*)point, 255) != 0) return -1;
    dev_buf[31] = '\0';
    point_buf[255] = '\0';

    partition_t* p = udisk_get_partition(dev_buf);
    if (!p) return -1;

    if (ufs_ismounted()) return -1;

    if (ufs_mount_with_point(p->start_lba, p->disk_num, point_buf) != 0) return -1;

    void fs_set_current_dir(const char*);
    fs_set_current_dir(point_buf);
    return 0;
}

static long sys_partition_umount(trap_frame_t* frame, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)frame; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return ufs_umount();
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
    syscall_table[57] = sys_fork;

    wrmsr(MSR_LSTAR, (u64)syscall_entry);

    u64 star = ((u64)0x1B << 48) | ((u64)0x08 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_SFMASK, 0x600);

    u64 efer;
    __asm__ volatile("rdmsr" : "=a"(((u32*)&efer)[0]), "=d"(((u32*)&efer)[1]) : "c"(0xC0000080));
    efer |= 1;
    __asm__ volatile("wrmsr" : : "c"(0xC0000080), "a"((u32)efer), "d"((u32)(efer >> 32)));

    return 0;
}

long syscall_handler_c(trap_frame_t* frame, long num) {
    __asm__ volatile("sti");

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
