#include "kapi.h"
#include "sched.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "memory.h"
#include "paging.h"
#include "../fs/ufs.h"

#define MAX_FDS 32
#define SYSCALL_COUNT 64

typedef long (*syscall_t)(long, long, long, long, long, long);
static syscall_t syscall_table[SYSCALL_COUNT];

void syscall_register(int num, syscall_t handler) {
    if (num >= 0 && num < SYSCALL_COUNT) syscall_table[num] = handler;
}

long syscall_handler_c(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    if (num < 0 || num >= SYSCALL_COUNT || !syscall_table[num]) return -1;
    return syscall_table[num](a1, a2, a3, a4, a5, a6);
}

// ===== ФАЙЛОВЫЕ СИСТЕМНЫЕ ВЫЗОВЫ =====
static long sys_write(long fd, long buf, long count, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    
    char *str = (char*)buf;
    if (fd == 1 || fd == 2) {
        for (long i = 0; i < count; i++) vga_putchar(str[i]);
        return count;
    }
    
    if (!p->fd_table[fd].used) return -1;
    
    // В UFS пока нет write_at, используем write (упрощенно)
    // TODO: добавить позиционирование
    int res = ufs_write(p->fd_table[fd].file.path, (u8*)buf, count);
    if (res == 0) {
        p->fd_table[fd].file.pos += count;
        return count;
    }
    return -1;
}

static long sys_read(long fd, long buf, long count, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    
    if (fd == 0) {
        for (long i = 0; i < count; i++) {
            while (!keyboard_data_ready());
            ((char*)buf)[i] = keyboard_getc();
        }
        return count;
    }
    
    if (!p->fd_table[fd].used) return -1;
    
    // В UFS пока нет read_at, используем read (упрощенно)
    u8 *tmp;
    u32 size;
    if (ufs_read(p->fd_table[fd].file.path, &tmp, &size) != 0) return -1;
    
    long to_copy = (count < (long)size - p->fd_table[fd].file.pos) ? 
                    count : (long)size - p->fd_table[fd].file.pos;
    
    if (to_copy > 0) {
        memcpy((u8*)buf, tmp + p->fd_table[fd].file.pos, to_copy);
        p->fd_table[fd].file.pos += to_copy;
    }
    
    kfree(tmp);
    return to_copy;
}

static long sys_open(long path, long flags, long mode, long unused1, long unused2, long unused3) {
    (void)mode;
    process_t *p = sched_current();
    if (!p) return -1;
    
    char *path_str = (char*)path;
    if (!path_str || !path_str[0]) return -1;
    
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!p->fd_table[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;
    
    if (!ufs_exists(path_str)) {
        if (flags & 0x40) {
            if (ufs_write(path_str, NULL, 0) != 0) return -1;
        } else {
            return -1;
        }
    }
    
    p->fd_table[fd].used = 1;
    p->fd_table[fd].type = 0;
    
    int len = strlen(path_str);
    if (len > 255) len = 255;
    for (int i = 0; i < len; i++) {
        p->fd_table[fd].file.path[i] = path_str[i];
    }
    p->fd_table[fd].file.path[len] = '\0';
    
    p->fd_table[fd].file.pos = 0;
    
    return fd;
}

static long sys_close(long fd, long unused1, long unused2, long unused3, long unused4, long unused5) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    p->fd_table[fd].used = 0;
    return 0;
}

static long sys_lseek(long fd, long offset, long whence, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    
    // Упрощенно - без проверки размера файла
    switch (whence) {
        case 0: p->fd_table[fd].file.pos = offset; break;
        case 1: p->fd_table[fd].file.pos += offset; break;
        case 2: p->fd_table[fd].file.pos = offset; break; // В реальности с конца
        default: return -1;
    }
    
    if (p->fd_table[fd].file.pos < 0) p->fd_table[fd].file.pos = 0;
    
    return p->fd_table[fd].file.pos;
}

static long sys_brk(long addr, long unused1, long unused2, long unused3, long unused4, long unused5) {
    process_t *p = sched_current();
    if (!p) return -1;
    
    if (addr == 0) return p->heap_end;
    
    if (addr > p->heap_end) {
        u64 pages = (addr - p->heap_end + 4095) / 4096;
        for (u64 i = 0; i < pages; i++) {
            u64 phys = (u64)kmalloc(4096);
            u64 virt = p->heap_end + i * 4096;
            paging_map(phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    }
    
    p->heap_end = addr;
    return addr;
}

static long sys_exit(long status, long unused1, long unused2, long unused3, long unused4, long unused5) {
    sched_exit(status);
    return 0;
}

static long sys_getpid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6) {
    process_t *p = sched_current();
    return p ? p->pid : -1;
}

void kapi_init(void) {
    for (int i = 0; i < SYSCALL_COUNT; i++) syscall_table[i] = NULL;
    
    syscall_register(0, sys_read);
    syscall_register(1, sys_write);
    syscall_register(2, sys_open);
    syscall_register(3, sys_close);
    syscall_register(8, sys_lseek);
    syscall_register(12, sys_brk);
    syscall_register(39, sys_getpid);
    syscall_register(60, sys_exit);
}
