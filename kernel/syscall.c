// kernel/syscall.c
#include "syscall.h"
#include "sched.h"
#include "elf.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/disk.h"
#include "../include/udisk.h"
#include "../net/tcp.h"
#include "../net/udp.h"
#include "../net/dns.h"
#include "../net/net.h"
#include "memory.h"
#include "paging.h"
#include "../include/string.h"

#define MAX_FDS 32

// Структура для stat
typedef struct {
    u32 size;
    u8 is_dir;
    u32 blocks;
} sys_stat_t;

// ==================== ПРОВЕРКА УКАЗАТЕЛЕЙ ====================

static int is_user_pointer(void* ptr) {
    u64 addr = (u64)ptr;
    return (addr >= 0x400000 && addr < 0x800000000000);
}

static int copy_from_user(void* dest, const void* src, u64 size) {
    if (!is_user_pointer((void*)src)) return -1;
    memcpy(dest, src, size);
    return 0;
}

static int copy_to_user(void* dest, const void* src, u64 size) {
    if (!is_user_pointer(dest)) return -1;
    memcpy(dest, src, size);
    return 0;
}

// ==================== СЕТЕВЫЕ ВЫЗОВЫ ====================

static long sys_gethostbyname(long name, long ip, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)name) || !is_user_pointer((void*)ip)) return -1;
    
    char name_buf[256];
    copy_from_user(name_buf, (void*)name, 255);
    name_buf[255] = '\0';
    
    u32 ip_addr = dns_lookup(name_buf, net_get_dns());
    if (ip_addr == 0) return -1;
    
    copy_to_user((void*)ip, &ip_addr, 4);
    return 0;
}

static long sys_socket(long domain, long type, long protocol, long a4, long a5, long a6) {
    (void)domain; (void)type; (void)protocol; (void)a4; (void)a5; (void)a6;
    return tcp_socket();
}

static long sys_connect(long fd, long addr, long port, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return tcp_connect(fd, (u32)addr, (u16)port);
}

static long sys_send(long fd, long buf, long len, long flags, long a5, long a6) {
    (void)flags; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;
    
    u8 *data = kmalloc(len);
    if (!data) return -1;
    copy_from_user(data, (void*)buf, len);
    
    int res = tcp_send(fd, data, len);
    kfree(data);
    return res;
}

static long sys_recv(long fd, long buf, long len, long flags, long a5, long a6) {
    (void)flags; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;
    
    u8 *data = kmalloc(len);
    if (!data) return -1;
    
    int res = tcp_recv(fd, data, len);
    if (res > 0) {
        copy_to_user((void*)buf, data, res);
    }
    
    kfree(data);
    return res;
}

// ==================== ОСТАЛЬНЫЕ СИСТЕМНЫЕ ВЫЗОВЫ ====================
// (все остальные функции из предыдущего syscall.c)

static long sys_exit(long code, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_exit(code);
    return 0;
}

static long sys_write(long fd, long buf, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    if (!is_user_pointer((void*)buf)) return -1;
    
    if (fd == 1 || fd == 2) {
        char* str = (char*)buf;
        for (long i = 0; i < count; i++) {
            vga_putchar(str[i]);
        }
        return count;
    }
    
    if (!p->fds[fd].used) return -1;
    
    if (p->fds[fd].type == 0) {
        char* data = kmalloc(count + 1);
        if (!data) return -1;
        copy_from_user(data, (void*)buf, count);
        data[count] = '\0';
        
        int res = ufs_write(p->fds[fd].data.file.path, (u8*)data, count);
        kfree(data);
        
        if (res == 0) {
            p->fds[fd].data.file.pos += count;
            return count;
        }
    }
    
    return -1;
}

static long sys_read(long fd, long buf, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    if (!is_user_pointer((void*)buf)) return -1;
    
    if (fd == 0) {
        char* buffer = (char*)buf;
        for (long i = 0; i < count; i++) {
            while (!keyboard_data_ready());
            buffer[i] = keyboard_getc();
        }
        return count;
    }
    
    if (!p->fds[fd].used) return -1;
    
    if (p->fds[fd].type == 0) {
        u8 *data;
        u32 size;
        if (ufs_read(p->fds[fd].data.file.path, &data, &size) != 0) return -1;
        
        long pos = p->fds[fd].data.file.pos;
        long to_copy = count;
        if (pos + to_copy > size) to_copy = size - pos;
        if (to_copy > 0) {
            copy_to_user((void*)buf, data + pos, to_copy);
            p->fds[fd].data.file.pos += to_copy;
        }
        
        kfree(data);
        return to_copy;
    }
    
    return -1;
}

static long sys_open(long path, long flags, long mode, long a4, long a5, long a6) {
    (void)mode; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!p->fds[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;
    
    if (!ufs_exists(path_buf)) {
        if (flags & 0x40) {
            if (ufs_write(path_buf, NULL, 0) != 0) return -1;
        } else {
            return -1;
        }
    }
    
    p->fds[fd].used = 1;
    p->fds[fd].type = 0;
    strcpy(p->fds[fd].data.file.path, path_buf);
    p->fds[fd].data.file.pos = 0;
    
    return fd;
}

static long sys_close(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    p->fds[fd].used = 0;
    return 0;
}

static long sys_brk(long addr, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;
    
    if (addr == 0) return p->heap_end;
    
    if (addr > p->heap_end) {
        u64 pages = (addr - p->heap_end + 4095) / 4096;
        u64* pml4 = (u64*)p->cr3;
        
        for (u64 i = 0; i < pages; i++) {
            u64 phys = (u64)kmalloc(4096);
            u64 virt = p->heap_end + i * 4096;
            paging_map_for_process(pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    }
    
    p->heap_end = addr;
    return addr;
}

static long sys_getpid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_get_pid();
}

static long sys_getppid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_get_ppid();
}

static long sys_sleep(long ms, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_sleep(ms);
    return 0;
}

static long sys_yield(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_yield();
    return 0;
}

static long sys_kill(long pid, long sig, long a3, long a4, long a5, long a6) {
    (void)sig; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_kill(pid);
    return 0;
}

static long sys_lseek(long fd, long offset, long whence, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    
    switch (whence) {
        case 0: p->fds[fd].data.file.pos = offset; break;
        case 1: p->fds[fd].data.file.pos += offset; break;
        case 2: {
            u32 size = ufs_file_size(p->fds[fd].data.file.path);
            p->fds[fd].data.file.pos = size + offset;
            break;
        }
        default: return -1;
    }
    
    if (p->fds[fd].data.file.pos < 0) p->fds[fd].data.file.pos = 0;
    return p->fds[fd].data.file.pos;
}

static long sys_stat(long path, long statbuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)statbuf)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    if (!ufs_exists(path_buf)) return -1;
    
    sys_stat_t st;
    st.size = ufs_file_size(path_buf);
    st.is_dir = ufs_isdir(path_buf) ? 1 : 0;
    st.blocks = (st.size + 511) / 512;
    
    copy_to_user((void*)statbuf, &st, sizeof(st));
    return 0;
}

static long sys_fstat(long fd, long statbuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return -1;
    if (!is_user_pointer((void*)statbuf)) return -1;
    
    sys_stat_t st;
    st.size = ufs_file_size(p->fds[fd].data.file.path);
    st.is_dir = ufs_isdir(p->fds[fd].data.file.path) ? 1 : 0;
    st.blocks = (st.size + 511) / 512;
    
    copy_to_user((void*)statbuf, &st, sizeof(st));
    return 0;
}

static long sys_mkdir(long path, long mode, long a3, long a4, long a5, long a6) {
    (void)mode; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    return ufs_mkdir(path_buf);
}

static long sys_rmdir(long path, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    return ufs_rmdir(path_buf);
}

static long sys_unlink(long path, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    return ufs_delete(path_buf);
}

static long sys_rename(long old, long new, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)old) || !is_user_pointer((void*)new)) return -1;
    
    char old_buf[256], new_buf[256];
    copy_from_user(old_buf, (void*)old, 255);
    copy_from_user(new_buf, (void*)new, 255);
    
    return ufs_mv(old_buf, new_buf);
}

static long sys_chdir(long path, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    if (!ufs_isdir(path_buf)) return -1;
    
    extern void fs_set_current_dir(const char*);
    fs_set_current_dir(path_buf);
    return 0;
}

static long sys_getcwd(long buf, long size, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;
    
    extern const char* fs_get_current_dir(void);
    const char* cwd = fs_get_current_dir();
    if (!cwd) cwd = "/";
    
    unsigned long len = strlen(cwd) + 1;
    if (len > (unsigned long)size) return -1;
    
    copy_to_user((void*)buf, cwd, len);
    return len;
}

static long sys_readdir(long path, long entries, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)entries)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    FSNode* kernel_entries;
    u32 kernel_count;
    
    if (ufs_readdir(path_buf, &kernel_entries, &kernel_count) != 0) return -1;
    
    u32 to_copy = kernel_count;
    if (count && to_copy > (u32)count) to_copy = count;
    
    for (u32 i = 0; i < to_copy; i++) {
        FSNode user_entry;
        strcpy(user_entry.name, kernel_entries[i].name);
        user_entry.size = kernel_entries[i].size;
        user_entry.is_dir = kernel_entries[i].is_dir;
        user_entry.first_block = 0;
        user_entry.next_block = 0;
        
        copy_to_user((void*)((char*)entries + i * sizeof(FSNode)), &user_entry, sizeof(FSNode));
    }
    
    kfree(kernel_entries);
    return to_copy;
}

static long sys_fork(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return -1; // TODO: реализовать fork
}

static long sys_exec(long path, long argv, long envp, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    u8 *elf_data;
    u32 elf_size;
    if (ufs_read(path_buf, &elf_data, &elf_size) != 0) return -1;
    
    int res = elf_load_current(elf_data, elf_size, p);
    kfree(elf_data);
    
    return res;
}

static long sys_waitpid(long pid, long status, long options, long a4, long a5, long a6) {
    (void)options; (void)a4; (void)a5; (void)a6;
    return sched_waitpid(pid, (int*)status);
}

static long sys_mmap(long addr, long size, long prot, long flags, long fd, long offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    process_t *p = sched_current();
    if (!p) return -1;
    
    u64 pages = (size + 4095) / 4096;
    u64 virt = p->heap_end;
    u64* pml4 = (u64*)p->cr3;
    
    for (u64 i = 0; i < pages; i++) {
        u64 phys = (u64)kmalloc(4096);
        paging_map_for_process(pml4, phys, virt + i * 4096, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    
    p->heap_end = virt + pages * 4096;
    return virt;
}

static long sys_munmap(long addr, long size, long a3, long a4, long a5, long a6) {
    (void)addr; (void)size; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}

static long sys_meminfo(long total, long used, long free, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)total) || !is_user_pointer((void*)used) || !is_user_pointer((void*)free))
        return -1;
    
    u64 t = memory_used() + memory_free();
    u64 u = memory_used();
    u64 f = memory_free();
    
    copy_to_user((void*)total, &t, 8);
    copy_to_user((void*)used, &u, 8);
    copy_to_user((void*)free, &f, 8);
    return 0;
}

static long sys_gettime(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    extern u32 get_ticks(void);
    return get_ticks();
}

// ==================== ТАБЛИЦА ====================

typedef long (*syscall_t)(long, long, long, long, long, long);
static syscall_t syscall_table[64];

void syscall_init(void) {
    for (int i = 0; i < 64; i++) syscall_table[i] = NULL;
    
    syscall_table[SYS_exit] = sys_exit;
    syscall_table[SYS_read] = sys_read;
    syscall_table[SYS_write] = sys_write;
    syscall_table[SYS_open] = sys_open;
    syscall_table[SYS_close] = sys_close;
    syscall_table[SYS_brk] = sys_brk;
    syscall_table[SYS_getpid] = sys_getpid;
    syscall_table[SYS_getppid] = sys_getppid;
    syscall_table[SYS_sleep] = sys_sleep;
    syscall_table[SYS_yield] = sys_yield;
    syscall_table[SYS_mmap] = sys_mmap;
    syscall_table[SYS_munmap] = sys_munmap;
    syscall_table[SYS_exec] = sys_exec;
    syscall_table[SYS_waitpid] = sys_waitpid;
    syscall_table[SYS_kill] = sys_kill;
    syscall_table[SYS_lseek] = sys_lseek;
    syscall_table[SYS_stat] = sys_stat;
    syscall_table[SYS_fstat] = sys_fstat;
    syscall_table[SYS_mkdir] = sys_mkdir;
    syscall_table[SYS_rmdir] = sys_rmdir;
    syscall_table[SYS_unlink] = sys_unlink;
    syscall_table[SYS_rename] = sys_rename;
    syscall_table[SYS_chdir] = sys_chdir;
    syscall_table[SYS_getcwd] = sys_getcwd;
    syscall_table[SYS_readdir] = sys_readdir;
    syscall_table[SYS_fork] = sys_fork;
    syscall_table[SYS_socket] = sys_socket;
    syscall_table[SYS_connect] = sys_connect;
    syscall_table[SYS_send] = sys_send;
    syscall_table[SYS_recv] = sys_recv;
    syscall_table[SYS_gethostbyname] = sys_gethostbyname;
    syscall_table[SYS_meminfo] = sys_meminfo;
    syscall_table[SYS_gettime] = sys_gettime;
}

long syscall_handler(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    if (num < 0 || num >= 64 || !syscall_table[num]) {
        return -1;
    }
    return syscall_table[num](a1, a2, a3, a4, a5, a6);
}
