#include "kapi.h"
#include "sched.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "memory.h"
#include "paging.h"

#define MAX_FDS 32
#define SYSCALL_COUNT 64

typedef long (*syscall_t)(long, long, long, long, long, long);
static syscall_t syscall_table[SYSCALL_COUNT];

typedef struct {
    int used;
    int type; // 0: file, 1: socket, 2: pipe
    union {
        struct {
            char path[256];
            u32 inode;
            u32 pos;
        } file;
        struct {
            int sockfd;
            int state;
        } socket;
    };
} fd_entry_t;

static fd_entry_t fd_table[MAX_FDS];

void syscall_register(int num, syscall_t handler) {
    if (num >= 0 && num < SYSCALL_COUNT) syscall_table[num] = handler;
}

// Инициализация таблицы файловых дескрипторов для процесса
void fd_table_init(process_t *p) {
    for (int i = 0; i < MAX_FDS; i++) p->fd_table[i].used = 0;
    // stdin, stdout, stderr
    p->fd_table[0].used = 1; p->fd_table[0].type = 0; // stdin
    p->fd_table[1].used = 1; p->fd_table[1].type = 0; // stdout
    p->fd_table[2].used = 1; p->fd_table[2].type = 0; // stderr
}

long syscall_handler_c(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    if (num < 0 || num >= SYSCALL_COUNT || !syscall_table[num]) return -1;
    return syscall_table[num](a1, a2, a3, a4, a5, a6);
}

// ===== ФАЙЛОВЫЕ СИСВОЛЫ =====
static long sys_write(long fd, long buf, long count, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    
    if (fd == 1 || fd == 2) { // stdout/stderr
        char *str = (char*)buf;
        for (long i = 0; i < count; i++) vga_putchar(str[i]);
        return count;
    }
    
    // Запись в файл
    fd_entry_t *f = &p->fd_table[fd];
    if (f->type != 0) return -1;
    
    int res = ufs_write_at(f->file.path, (u8*)buf, count, f->file.pos);
    if (res > 0) f->file.pos += res;
    return res;
}

static long sys_read(long fd, long buf, long count, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    
    if (fd == 0) { // stdin
        // TODO: читать с клавиатуры
        return 0;
    }
    
    fd_entry_t *f = &p->fd_table[fd];
    if (f->type != 0) return -1;
    
    int res = ufs_read_at(f->file.path, (u8*)buf, count, f->file.pos);
    if (res > 0) f->file.pos += res;
    return res;
}

static long sys_open(long path, long flags, long mode, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p) return -1;
    
    char *path_str = (char*)path;
    if (!path_str || !path_str[0]) return -1;
    
    // Находим свободный fd
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!p->fd_table[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;
    
    // Проверяем существование
    if (!ufs_exists(path_str)) {
        if (flags & 0x40) { // O_CREAT
            ufs_write(path_str, NULL, 0);
        } else {
            return -1;
        }
    }
    
    p->fd_table[fd].used = 1;
    p->fd_table[fd].type = 0;
    strcpy(p->fd_table[fd].file.path, path_str);
    p->fd_table[fd].file.pos = 0;
    p->fd_table[fd].file.inode = 0; // TODO
    
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
    
    fd_entry_t *f = &p->fd_table[fd];
    if (f->type != 0) return -1;
    
    u32 size = ufs_file_size(f->file.path);
    
    switch (whence) {
        case 0: f->file.pos = offset; break; // SEEK_SET
        case 1: f->file.pos += offset; break; // SEEK_CUR
        case 2: f->file.pos = size + offset; break; // SEEK_END
        default: return -1;
    }
    
    if (f->file.pos > size) f->file.pos = size;
    if (f->file.pos < 0) f->file.pos = 0;
    
    return f->file.pos;
}

// ===== ПАМЯТЬ =====
static long sys_brk(long addr, long unused1, long unused2, long unused3, long unused4, long unused5) {
    process_t *p = sched_current();
    if (!p) return -1;
    
    if (addr == 0) return p->heap_end;
    
    if (addr > p->heap_end) {
        u64 pages = (addr - p->heap_end + 4095) / 4096;
        for (u64 i = 0; i < pages; i++) {
            u64 phys = (u64)kmalloc(4096);
            u64 virt = p->heap_end + i * 4096;
            paging_map(p->cr3, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    }
    
    p->heap_end = addr;
    return addr;
}

// ===== ПРОЦЕССЫ =====
static long sys_exit(long status, long unused1, long unused2, long unused3, long unused4, long unused5) {
    sched_exit(status);
    return 0;
}

static long sys_getpid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6) {
    process_t *p = sched_current();
    return p ? p->pid : -1;
}

// ===== СЕТЕВЫЕ СИСВОЛЫ =====
static long sys_socket(long domain, long type, long protocol, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p) return -1;
    
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!p->fd_table[i].used) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;
    
    p->fd_table[fd].used = 1;
    p->fd_table[fd].type = 1;
    p->fd_table[fd].socket.sockfd = net_socket_create(domain, type, protocol);
    p->fd_table[fd].socket.state = 0;
    
    return fd;
}

static long sys_connect(long fd, long addr, long addrlen, long unused1, long unused2, long unused3) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    if (p->fd_table[fd].type != 1) return -1;
    
    struct sockaddr_in *sa = (struct sockaddr_in*)addr;
    return net_socket_connect(p->fd_table[fd].socket.sockfd, sa->sin_addr, sa->sin_port);
}

static long sys_send(long fd, long buf, long len, long flags, long unused1, long unused2) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    if (p->fd_table[fd].type != 1) return -1;
    
    return net_socket_send(p->fd_table[fd].socket.sockfd, (u8*)buf, len);
}

static long sys_recv(long fd, long buf, long len, long flags, long unused1, long unused2) {
    process_t *p = sched_current();
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fd_table[fd].used) return -1;
    if (p->fd_table[fd].type != 1) return -1;
    
    return net_socket_recv(p->fd_table[fd].socket.sockfd, (u8*)buf, len);
}

void kapi_init(void) {
    syscall_register(0, sys_read);
    syscall_register(1, sys_write);
    syscall_register(2, sys_open);
    syscall_register(3, sys_close);
    syscall_register(8, sys_lseek);
    syscall_register(12, sys_brk);
    syscall_register(39, sys_getpid);
    syscall_register(60, sys_exit);
    
    // Сеть
    syscall_register(41, sys_socket);
    syscall_register(42, sys_connect);
    syscall_register(44, sys_send);
    syscall_register(45, sys_recv);
}
