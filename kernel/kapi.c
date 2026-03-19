#include "kapi.h"
#include "sched.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/disk.h"
#include "../include/udisk.h"
#include "../net/net.h"
#include "../net/dns.h"
#include "../net/tcp.h"
#include "memory.h"
#include "paging.h"
#include "../include/string.h"
#include "../include/udisk.h"
#include "../include/syscall.h"

#define MAX_FDS 32
#define SYSCALL_COUNT 64

typedef long (*syscall_t)(long, long, long, long, long, long);
static syscall_t syscall_table[SYSCALL_COUNT];
extern u32 system_ticks;
extern const char* fs_get_current_dir(void);
extern void fs_set_current_dir(const char*);

void syscall_register(int num, syscall_t handler) {
    if (num >= 0 && num < SYSCALL_COUNT) {
        syscall_table[num] = handler;
    }
}

// ==================== ПРОВЕРКА УКАЗАТЕЛЕЙ ====================

static int is_user_pointer(void* ptr) {
    // Проверяем, что указатель в пользовательском пространстве (> 0x400000)
    return ((u64)ptr > 0x400000 && (u64)ptr < 0x800000000000);
}

static int copy_from_user(void* dest, const void* src, unsigned long size) {
    if (!is_user_pointer((void*)src)) return -1;
    memcpy(dest, src, size);
    return 0;
}

static int copy_to_user(void* dest, const void* src, unsigned long size) {
    if (!is_user_pointer(dest)) return -1;
    memcpy(dest, src, size);
    return 0;
}

// ==================== БАЗОВЫЕ СИСТЕМНЫЕ ВЫЗОВЫ ====================

static long sys_exit_handler(long code, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_exit(code);
    return 0;
}

static long sys_read_handler(long fd, long buf, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    
    if (!is_user_pointer((void*)buf)) return -1;
    
    // stdin
    if (fd == 0) {
        char* buffer = (char*)buf;
        for (long i = 0; i < count; i++) {
            while (!keyboard_data_ready());
            buffer[i] = keyboard_getc();
        }
        return count;
    }
    
    if (!p->fd_table[fd].used) return -1;
    
    if (p->fd_table[fd].type == 0) { // обычный файл
        u8 *tmp;
        u32 size;
        if (ufs_read(p->fd_table[fd].file.path, &tmp, &size) != 0) return -1;
        
        long to_copy = (count < (long)size - p->fd_table[fd].file.pos) ? 
                        count : (long)size - p->fd_table[fd].file.pos;
        
        if (to_copy > 0) {
            copy_to_user((void*)buf, tmp + p->fd_table[fd].file.pos, to_copy);
            p->fd_table[fd].file.pos += to_copy;
        }
        
        kfree(tmp);
        return to_copy;
    }
    
    return -1;
}

static long sys_write_handler(long fd, long buf, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    
    if (!is_user_pointer((void*)buf)) return -1;
    
    // stdout/stderr
    if (fd == 1 || fd == 2) {
        char* str = (char*)buf;
        for (long i = 0; i < count; i++) {
            vga_putchar(str[i]);
        }
        return count;
    }
    
    if (!p->fd_table[fd].used) return -1;
    
    char* data = kmalloc(count + 1);
    if (!data) return -1;
    copy_from_user(data, (void*)buf, count);
    data[count] = '\0';
    
    int res = ufs_write(p->fd_table[fd].file.path, (u8*)data, count);
    kfree(data);
    
    if (res == 0) {
        p->fd_table[fd].file.pos += count;
        return count;
    }
    return -1;
}

static long sys_open_handler(long path, long flags, long mode, long a4, long a5, long a6) {
    (void)mode; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;
    
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    if (!path_buf[0]) return -1;
    
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!p->fd_table[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;
    
    if (!ufs_exists(path_buf)) {
        if (flags & 0x40) { // O_CREAT
            if (ufs_write(path_buf, NULL, 0) != 0) return -1;
        } else {
            return -1;
        }
    }
    
    p->fd_table[fd].used = 1;
    p->fd_table[fd].type = 0;
    strcpy(p->fd_table[fd].file.path, path_buf);
    p->fd_table[fd].file.pos = 0;
    
    return fd;
}

static long sys_close_handler(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS) return -1;
    p->fd_table[fd].used = 0;
    return 0;
}

static long sys_brk_handler(long addr, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
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

static long sys_getpid_handler(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    return p ? p->pid : -1;
}

static long sys_getppid_handler(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    return p ? p->ppid : -1;
}

static long sys_sleep_handler(long ticks, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_sleep(ticks);
    return 0;
}

static long sys_yield_handler(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_yield();
    return 0;
}

static long sys_mmap_handler(long addr, long size, long prot, long flags, long fd, long offset) {
    (void)addr; (void)size; (void)prot; (void)flags; (void)fd; (void)offset;
    // Упрощенная реализация
    process_t *p = sched_current();
    if (!p) return -1;
    
    u64 phys = (u64)kmalloc(size);
    u64 virt = p->heap_end;
    paging_map(phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    p->heap_end += size;
    
    return virt;
}

static long sys_munmap_handler(long addr, long size, long a3, long a4, long a5, long a6) {
    (void)addr; (void)size; (void)a3; (void)a4; (void)a5; (void)a6;
    // В реальной ОС нужно освобождать память
    return 0;
}

static long sys_exec_handler(long path, long argv, long a3, long a4, long a5, long a6) {
    (void)argv; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p) return -1;
    
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    u8 *elf_data;
    u32 elf_size;
    if (ufs_read(path_buf, &elf_data, &elf_size) != 0) return -1;
    
    extern int elf_load_user(u8*, void*);
    if (elf_load_user(elf_data, p) != 0) {
        kfree(elf_data);
        return -1;
    }
    
    kfree(elf_data);
    return 0;
}

static long sys_waitpid_handler(long pid, long status, long options, long a4, long a5, long a6) {
    (void)pid; (void)status; (void)options; (void)a4; (void)a5; (void)a6;
    // Упрощенно - просто ждем
    sched_sleep(10);
    return 0;
}

static long sys_kill_handler(long pid, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sched_kill(pid);
}

// ==================== ФАЙЛОВЫЕ ОПЕРАЦИИ ====================

static long sys_lseek_handler(long fd, long offset, long whence, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    
    switch (whence) {
        case 0: p->fd_table[fd].file.pos = offset; break;
        case 1: p->fd_table[fd].file.pos += offset; break;
        case 2: {
            u32 size = ufs_file_size(p->fd_table[fd].file.path);
            p->fd_table[fd].file.pos = size + offset;
            break;
        }
        default: return -1;
    }
    
    if (p->fd_table[fd].file.pos < 0) p->fd_table[fd].file.pos = 0;
    
    return p->fd_table[fd].file.pos;
}

typedef struct {
    u32 size;
    u8 is_dir;
    u32 blocks;
} stat_t;

static long sys_stat_handler(long path, long statbuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)statbuf)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    if (!ufs_exists(path_buf)) return -1;
    
    stat_t st;
    st.size = ufs_file_size(path_buf);
    st.is_dir = ufs_isdir(path_buf) ? 1 : 0;
    st.blocks = (st.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    
    copy_to_user((void*)statbuf, &st, sizeof(stat_t));
    return 0;
}

static long sys_fstat_handler(long fd, long statbuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    if (!is_user_pointer((void*)statbuf)) return -1;
    
    stat_t st;
    st.size = ufs_file_size(p->fd_table[fd].file.path);
    st.is_dir = ufs_isdir(p->fd_table[fd].file.path) ? 1 : 0;
    st.blocks = (st.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    
    copy_to_user((void*)statbuf, &st, sizeof(stat_t));
    return 0;
}

static long sys_mkdir_handler(long path, long mode, long a3, long a4, long a5, long a6) {
    (void)mode; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    return ufs_mkdir(path_buf);
}

static long sys_rmdir_handler(long path, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    return ufs_rmdir(path_buf);
}

static long sys_unlink_handler(long path, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    return ufs_delete(path_buf);
}

static long sys_rename_handler(long oldpath, long newpath, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)oldpath) || !is_user_pointer((void*)newpath)) return -1;
    
    char old_buf[256], new_buf[256];
    copy_from_user(old_buf, (void*)oldpath, 255);
    copy_from_user(new_buf, (void*)newpath, 255);
    old_buf[255] = '\0';
    new_buf[255] = '\0';
    
    return ufs_mv(old_buf, new_buf);
}

static long sys_chdir_handler(long path, long a2, long a3, long a4, long a5, long a6) {
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

static long sys_getcwd_handler(long buf, long size, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;
    
    const char* cwd = fs_get_current_dir();
    if (!cwd) cwd = "/";
    
    unsigned long len = strlen(cwd) + 1;
    if (len > (unsigned long)size) return -1;
    
    copy_to_user((void*)buf, cwd, len);
    return len;
}

static long sys_readdir_handler(long path, long entries, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)path) || !is_user_pointer((void*)entries) || !is_user_pointer((void*)count)) return -1;
    
    char path_buf[256];
    copy_from_user(path_buf, (void*)path, 255);
    path_buf[255] = '\0';
    
    FSNode* kernel_entries;
    u32 kernel_count;
    
    if (ufs_readdir(path_buf, &kernel_entries, &kernel_count) != 0) return -1;
    
    unsigned long user_count;
    copy_from_user(&user_count, (void*)count, sizeof(user_count));
    
    unsigned long to_copy = (kernel_count < user_count) ? kernel_count : user_count;
    
    for (unsigned long i = 0; i < to_copy; i++) {
        FSNode user_entry;
        strcpy(user_entry.name, kernel_entries[i].name);
        user_entry.size = kernel_entries[i].size;
        user_entry.is_dir = kernel_entries[i].is_dir;
        copy_to_user((void*)((char*)entries + i * sizeof(FSNode)), &user_entry, sizeof(FSNode));
    }
    
    copy_to_user((void*)count, &to_copy, sizeof(to_copy));
    kfree(kernel_entries);
    return 0;
}

// ==================== ДИСКОВЫЕ ОПЕРАЦИИ ====================

static long sys_disk_list_handler(long disks, long max, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)disks)) return -1;
    
    udisk_scan();
    
    int count = 0;
    for (int i = 0; i < 4 && count < max; i++) {
        disk_info_t* d = udisk_get_info(i);
        if (d && d->present) {
            copy_to_user((void*)((char*)disks + count * sizeof(disk_info_t)), d, sizeof(disk_info_t));
            count++;
        }
    }
    
    return count;
}

static long sys_disk_read_handler(long disk, long lba, long buffer, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buffer)) return -1;
    
    if (disk_set_disk(disk) != 0) return -1;
    
    u8 kernel_buf[512];
    if (disk_read(lba, kernel_buf) != 0) return -1;
    
    copy_to_user((void*)buffer, kernel_buf, 512);
    return 0;
}

static long sys_disk_write_handler(long disk, long lba, long buffer, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buffer)) return -1;
    
    if (disk_set_disk(disk) != 0) return -1;
    
    u8 kernel_buf[512];
    copy_from_user(kernel_buf, (void*)buffer, 512);
    
    return disk_write(lba, kernel_buf);
}

static long sys_partition_list_handler(long disk, long partitions, long max, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)partitions)) return -1;
    
    udisk_scan();
    
    disk_info_t* d = udisk_get_info(disk);
    if (!d || !d->present) return -1;
    
    int count = (d->partition_count < max) ? d->partition_count : max;
    for (int i = 0; i < count; i++) {
        copy_to_user((void*)((char*)partitions + i * sizeof(partition_t)), &d->partitions[i], sizeof(partition_t));
    }
    
    return count;
}

static long sys_partition_create_handler(long dev, long size_mb, long type, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)dev)) return -1;
    
    char dev_buf[32];
    copy_from_user(dev_buf, (void*)dev, 31);
    dev_buf[31] = '\0';
    
    partition_type_t ptype;
    switch (type) {
        case 0: ptype = PARTITION_UFS; break;
        case 1: ptype = PARTITION_FAT32; break;
        case 2: ptype = PARTITION_EXT4; break;
        default: ptype = PARTITION_UFS;
    }
    
    return udisk_create_partition(dev_buf, size_mb, ptype);
}

static long sys_partition_delete_handler(long dev, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)dev)) return -1;
    
    char dev_buf[32];
    copy_from_user(dev_buf, (void*)dev, 31);
    dev_buf[31] = '\0';
    
    return udisk_delete_partition(dev_buf);
}

static long sys_partition_format_handler(long dev, long fstype, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)dev) || !is_user_pointer((void*)fstype)) return -1;
    
    char dev_buf[32];
    char fs_buf[16];
    copy_from_user(dev_buf, (void*)dev, 31);
    copy_from_user(fs_buf, (void*)fstype, 15);
    dev_buf[31] = '\0';
    fs_buf[15] = '\0';
    
    return udisk_format_partition(dev_buf, fs_buf);
}

static long sys_partition_mount_handler(long dev, long point, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)dev) || !is_user_pointer((void*)point)) return -1;
    
    char dev_buf[32];
    char point_buf[256];
    copy_from_user(dev_buf, (void*)dev, 31);
    copy_from_user(point_buf, (void*)point, 255);
    dev_buf[31] = '\0';
    point_buf[255] = '\0';
    
    partition_t* p = udisk_get_partition(dev_buf);
    if (!p) return -1;
    
    if (ufs_ismounted()) return -1;
    
    if (ufs_mount_with_point(p->start_lba, p->disk_num, point_buf) != 0) return -1;
    
    extern void fs_set_current_dir(const char*);
    fs_set_current_dir(point_buf);
    return 0;
}

static long sys_partition_umount_handler(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return ufs_umount();
}

// ==================== СЕТЕВЫЕ ОПЕРАЦИИ ====================

static long sys_socket_handler(long domain, long type, long protocol, long a4, long a5, long a6) {
    (void)domain; (void)type; (void)protocol; (void)a4; (void)a5; (void)a6;
    return tcp_socket_create();
}

static long sys_connect_handler(long fd, long ip, long port, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return tcp_connect(fd, ip, port);
}

static long sys_send_handler(long fd, long buf, long len, long flags, long a5, long a6) {
    (void)flags; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;
    
    char* data = kmalloc(len);
    if (!data) return -1;
    copy_from_user(data, (void*)buf, len);
    
    int res = tcp_send(fd, (u8*)data, len);
    kfree(data);
    return res;
}

static long sys_recv_handler(long fd, long buf, long len, long flags, long a5, long a6) {
    (void)flags; (void)a5; (void)a6;
    if (!is_user_pointer((void*)buf)) return -1;
    
    char* data = kmalloc(len);
    if (!data) return -1;
    
    int res = tcp_recv(fd, (u8*)data, len);
    if (res > 0) {
        copy_to_user((void*)buf, data, res);
    }
    
    kfree(data);
    return res;
}

static long sys_gethostbyname_handler(long name, long ip, long a3, long a4, long a5, long a6) {
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

static long sys_getip_handler(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return net_get_ip();
}

// ==================== ИНФОРМАЦИОННЫЕ ВЫЗОВЫ ====================

static long sys_meminfo_handler(long total, long used, long free, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (!is_user_pointer((void*)total) || !is_user_pointer((void*)used) || !is_user_pointer((void*)free)) return -1;
    
    unsigned long t = memory_used() + memory_free();
    unsigned long u = memory_used();
    unsigned long f = memory_free();
    
    copy_to_user((void*)total, &t, sizeof(t));
    copy_to_user((void*)used, &u, sizeof(u));
    copy_to_user((void*)free, &f, sizeof(f));
    
    return 0;
}

static long sys_ps_handler(long processes, long max, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
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
        copy_to_user((void*)((char*)processes + i * sizeof(ps_entry_t)), &entry, sizeof(ps_entry_t));
    }
    
    return to_copy;
}

static long sys_gettime_handler(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return system_ticks;
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

void kapi_init(void) {
    for (int i = 0; i < SYSCALL_COUNT; i++) syscall_table[i] = NULL;
    
    // Базовые
    syscall_register(SYS_exit, sys_exit_handler);
    syscall_register(SYS_read, sys_read_handler);
    syscall_register(SYS_write, sys_write_handler);
    syscall_register(SYS_open, sys_open_handler);
    syscall_register(SYS_close, sys_close_handler);
    syscall_register(SYS_brk, sys_brk_handler);
    syscall_register(SYS_getpid, sys_getpid_handler);
    syscall_register(SYS_getppid, sys_getppid_handler);
    syscall_register(SYS_sleep, sys_sleep_handler);
    syscall_register(SYS_yield, sys_yield_handler);
    syscall_register(SYS_mmap, sys_mmap_handler);
    syscall_register(SYS_munmap, sys_munmap_handler);
    syscall_register(SYS_exec, sys_exec_handler);
    syscall_register(SYS_waitpid, sys_waitpid_handler);
    syscall_register(SYS_kill, sys_kill_handler);
    
    // Файловые
    syscall_register(SYS_lseek, sys_lseek_handler);
    syscall_register(SYS_stat, sys_stat_handler);
    syscall_register(SYS_fstat, sys_fstat_handler);
    syscall_register(SYS_mkdir, sys_mkdir_handler);
    syscall_register(SYS_rmdir, sys_rmdir_handler);
    syscall_register(SYS_unlink, sys_unlink_handler);
    syscall_register(SYS_rename, sys_rename_handler);
    syscall_register(SYS_chdir, sys_chdir_handler);
    syscall_register(SYS_getcwd, sys_getcwd_handler);
    syscall_register(SYS_readdir, sys_readdir_handler);
    
    // Дисковые
    syscall_register(SYS_disk_list, sys_disk_list_handler);
    syscall_register(SYS_disk_read, sys_disk_read_handler);
    syscall_register(SYS_disk_write, sys_disk_write_handler);
    syscall_register(SYS_partition_list, sys_partition_list_handler);
    syscall_register(SYS_partition_create, sys_partition_create_handler);
    syscall_register(SYS_partition_delete, sys_partition_delete_handler);
    syscall_register(SYS_partition_format, sys_partition_format_handler);
    syscall_register(SYS_partition_mount, sys_partition_mount_handler);
    syscall_register(SYS_partition_umount, sys_partition_umount_handler);
    
    // Сетевые
    syscall_register(SYS_socket, sys_socket_handler);
    syscall_register(SYS_connect, sys_connect_handler);
    syscall_register(SYS_send, sys_send_handler);
    syscall_register(SYS_recv, sys_recv_handler);
    syscall_register(SYS_gethostbyname, sys_gethostbyname_handler);
    syscall_register(SYS_getip, sys_getip_handler);
    
    // Информационные
    syscall_register(SYS_meminfo, sys_meminfo_handler);
    syscall_register(SYS_ps, sys_ps_handler);
    syscall_register(SYS_gettime, sys_gettime_handler);
}

long syscall_handler_c(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    if (num < 0 || num >= SYSCALL_COUNT || !syscall_table[num]) {
        return -1;
    }
    return syscall_table[num](a1, a2, a3, a4, a5, a6);
}
