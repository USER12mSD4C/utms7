#include "libc.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#define SYS_exit        0
#define SYS_read        1
#define SYS_write       2
#define SYS_open        3
#define SYS_close       4
#define SYS_brk         5
#define SYS_getpid      6
#define SYS_getppid     7
#define SYS_sleep       8
#define SYS_yield       9
#define SYS_mmap        10
#define SYS_munmap      11
#define SYS_exec        12
#define SYS_waitpid     13
#define SYS_kill        14
#define SYS_lseek       15
#define SYS_stat        16
#define SYS_fstat       17
#define SYS_mkdir       18
#define SYS_rmdir       19
#define SYS_unlink      20
#define SYS_rename      21
#define SYS_chdir       22
#define SYS_getcwd      23
#define SYS_readdir     24
#define SYS_dup         25
#define SYS_dup2        26
#define SYS_socket      40
#define SYS_connect     41
#define SYS_send        45
#define SYS_recv        46
#define SYS_meminfo     50
#define SYS_gettime     52
#define SYS_ps          51
#define SYS_clear       53
#define SYS_setcolor    54
#define SYS_fork        57
#define SYS_gethostbyname 47
#define SYS_ioctl       27
#define SYS_disk_list           30
#define SYS_partition_mount     37
#define SYS_partition_umount    38
#define SYS_partition_format    39
#define SYS_disk_table          42
#define SYS_partition_create    43
#define SYS_partition_delete    44
#define SYS_bind        48
#define SYS_listen      49
#define SYS_accept      50
#define SYS_poll        51
#define SYS_fs_register  58

long syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long rax __asm__("rax") = num;
    register long rdi __asm__("rdi") = a1;
    register long rsi __asm__("rsi") = a2;
    register long rdx __asm__("rdx") = a3;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;

    __asm__ volatile ("syscall"
        : "+r"(rax)
        : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return rax;
}

int open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & 0x40) {
        va_list args; va_start(args, flags);
        mode = va_arg(args, int); va_end(args);
    }
    return syscall(SYS_open, (long)path, flags, mode, 0, 0, 0);
}

int close(int fd) { return syscall(SYS_close, fd, 0, 0, 0, 0, 0); }
ssize_t read(int fd, void *buf, size_t count) { return syscall(SYS_read, fd, (long)buf, count, 0, 0, 0); }
ssize_t write(int fd, const void *buf, size_t count) { return syscall(SYS_write, fd, (long)buf, count, 0, 0, 0); }
off_t lseek(int fd, off_t offset, int whence) { return syscall(SYS_lseek, fd, offset, whence, 0, 0, 0); }
int stat(const char *path, struct stat *buf) { return syscall(SYS_stat, (long)path, (long)buf, 0, 0, 0, 0); }
int fstat(int fd, struct stat *buf) { return syscall(SYS_fstat, fd, (long)buf, 0, 0, 0, 0); }
int dup(int oldfd) { return syscall(SYS_dup, oldfd, 0, 0, 0, 0, 0); }
int dup2(int oldfd, int newfd) { return syscall(SYS_dup2, oldfd, newfd, 0, 0, 0, 0); }
int ioctl(int fd, unsigned long request, void *arg) { return syscall(SYS_ioctl, fd, (long)request, (long)arg, 0, 0, 0); }

int mkdir(const char *path, int mode) { return syscall(SYS_mkdir, (long)path, mode, 0, 0, 0, 0); }
int rmdir(const char *path) { return syscall(SYS_rmdir, (long)path, 0, 0, 0, 0, 0); }
int unlink(const char *path) { return syscall(SYS_unlink, (long)path, 0, 0, 0, 0, 0); }
int rename(const char *old, const char *new) { return syscall(SYS_rename, (long)old, (long)new, 0, 0, 0, 0); }
int chdir(const char *path) { return syscall(SYS_chdir, (long)path, 0, 0, 0, 0, 0); }
int fs_register(const char* name) { return syscall(SYS_fs_register, (long)name, 0, 0, 0, 0, 0); }

char *getcwd(char *buf, size_t size) {
    long res = syscall(SYS_getcwd, (long)buf, size, 0, 0, 0, 0);
    return res > 0 ? buf : NULL;
}

typedef struct block_header {
    size_t size;
    struct block_header *next;
    int free;
} block_header_t;

#define BLOCK_ALIGN 16
static block_header_t *free_list = NULL;
static void *heap_brk = NULL;

static void *sbrk(size_t size) {
    if (heap_brk == NULL) {
        long current_brk = syscall(SYS_brk, 0, 0, 0, 0, 0, 0);
        if (current_brk == -1) return (void*)-1;
        heap_brk = (void*)current_brk;
    }
    void *old = heap_brk;
    long res = syscall(SYS_brk, (long)heap_brk + size, 0, 0, 0, 0, 0);
    if (res == -1) return (void*)-1;
    heap_brk = (void*)((char*)heap_brk + size);
    return old;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
    size_t total = size + sizeof(block_header_t);

    block_header_t *prev = NULL;
    block_header_t *curr = free_list;
    while (curr) {
        if (curr->free && curr->size >= total) {
            if (curr->size >= total + sizeof(block_header_t) + BLOCK_ALIGN) {
                block_header_t *new = (block_header_t*)((char*)curr + total);
                new->size = curr->size - total;
                new->next = curr->next;
                new->free = 1;
                curr->next = new;
                curr->size = total;
            }
            curr->free = 0;
            return (void*)((char*)curr + sizeof(block_header_t));
        }
        prev = curr;
        curr = curr->next;
    }
    void *mem = sbrk(total);
    if (mem == (void*)-1) return NULL;
    block_header_t *block = (block_header_t*)mem;
    block->size = total;
    block->next = NULL;
    block->free = 0;
    if (prev) prev->next = block;
    else free_list = block;
    return (void*)((char*)block + sizeof(block_header_t));
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    size_t aligned_size = (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
    size_t total = aligned_size + sizeof(block_header_t);
    block_header_t *block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
    if (block->size >= total) return ptr;
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, block->size - sizeof(block_header_t));
    free(ptr);
    return new_ptr;
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
    block->free = 1;
    block_header_t *curr = free_list;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            curr->size += curr->next->size;
            curr->next = curr->next->next;
        }
        curr = curr->next;
    }
}

int fork(void) { return syscall(SYS_fork, 0, 0, 0, 0, 0, 0); }
int execve(const char *path, char *const argv[], char *const envp[]) { return syscall(SYS_exec, (long)path, (long)argv, (long)envp, 0, 0, 0); }
int execvp(const char *file, char *const argv[]) { return execve(file, argv, NULL); }
int waitpid(int pid, int *status, int options) { return syscall(SYS_waitpid, pid, (long)status, options, 0, 0, 0); }
void _exit(int status) { syscall(SYS_exit, status, 0, 0, 0, 0, 0); while(1); }
int getpid(void) { return syscall(SYS_getpid, 0, 0, 0, 0, 0, 0); }
int getppid(void) { return syscall(SYS_getppid, 0, 0, 0, 0, 0, 0); }
unsigned int sleep(unsigned int seconds) { syscall(SYS_sleep, seconds * 1000, 0, 0, 0, 0, 0); return 0; }
int kill(int pid, int sig) { return syscall(SYS_kill, pid, sig, 0, 0, 0, 0); }

#define ENV_SIZE 64
static char *environment[ENV_SIZE] = { NULL };
static int env_count = 0;

char *getenv(const char *name) {
    for (int i = 0; i < env_count; i++) {
        char *p = environment[i];
        int j = 0;
        while (name[j] && p[j] && name[j] == p[j]) j++;
        if (name[j] == '\0' && p[j] == '=') return p + j + 1;
    }
    return NULL;
}

int putenv(char *string) {
    char *eq = strchr(string, '=');
    if (!eq) return -1;
    for (int i = 0; i < env_count; i++) {
        char *p = environment[i];
        int j = 0;
        while (p[j] && p[j] != '=' && string[j] == p[j]) j++;
        if (string[j] == '\0' && p[j] == '=') { environment[i] = string; return 0; }
    }
    if (env_count < ENV_SIZE - 1) { environment[env_count++] = string; environment[env_count] = NULL; return 0; }
    return -1;
}

int setenv(const char *name, const char *value, int overwrite) {
    char *existing = getenv(name);
    if (existing && !overwrite) return 0;
    char *new = malloc(strlen(name) + strlen(value) + 2);
    if (!new) return -1;
    sprintf(new, "%s=%s", name, value);
    return putenv(new);
}

void unsetenv(const char *name) {
    for (int i = 0; i < env_count; i++) {
        char *p = environment[i];
        int j = 0;
        while (name[j] && p[j] && name[j] == p[j]) j++;
        if (name[j] == '\0' && p[j] == '=') {
            free(environment[i]);
            for (int k = i; k < env_count - 1; k++) environment[k] = environment[k+1];
            env_count--;
            environment[env_count] = NULL;
            return;
        }
    }
}

time_t time(time_t *tloc) {
    time_t t = (time_t)(syscall(SYS_gettime, 0, 0, 0, 0, 0, 0) / 1000);
    if (tloc) *tloc = t;
    return t;
}
unsigned int getticks(void) { return syscall(SYS_gettime, 0, 0, 0, 0, 0, 0); }

int socket(int domain, int type, int protocol) { return syscall(SYS_socket, domain, type, protocol, 0, 0, 0); }
int connect(int sockfd, unsigned int ip, unsigned short port) { return syscall(SYS_connect, sockfd, ip, port, 0, 0, 0); }
ssize_t send(int sockfd, const void *buf, size_t len, int flags) { return syscall(SYS_send, sockfd, (long)buf, len, flags, 0, 0); }
ssize_t recv(int sockfd, void *buf, size_t len, int flags) { return syscall(SYS_recv, sockfd, (long)buf, len, flags, 0, 0); }
int close_socket(int sockfd) { return syscall(SYS_close, sockfd, 0, 0, 0, 0, 0); }

unsigned int gethostbyname(const char *name) {
    unsigned int ip;
    long res = syscall(SYS_gethostbyname, (long)name, (long)&ip, 0, 0, 0, 0);
    return res == 0 ? ip : 0;
}

void clear_screen(void) { syscall(SYS_clear, 0, 0, 0, 0, 0, 0); }
void set_color(int fg, int bg) { syscall(SYS_setcolor, fg, bg, 0, 0, 0, 0); }

int disk_list(void* disks, int max) { return syscall(SYS_disk_list, (long)disks, max, 0, 0, 0, 0); }
int partition_mount(const char* dev, const char* point) { return syscall(SYS_partition_mount, (long)dev, (long)point, 0, 0, 0, 0); }
int partition_umount(void) { return syscall(SYS_partition_umount, 0, 0, 0, 0, 0, 0); }
int partition_format(const char* dev, const char* fstype) { return syscall(SYS_partition_format, (long)dev, (long)fstype, 0, 0, 0, 0); }
int disk_create_table(const char *dev, int gpt) { return syscall(SYS_disk_table, (long)dev, gpt, 0, 0, 0, 0); }
int partition_create(const char *dev, unsigned long size_mb, int type) { return syscall(SYS_partition_create, (long)dev, (long)size_mb, type, 0, 0, 0); }
int partition_delete(const char *dev) { return syscall(SYS_partition_delete, (long)dev, 0, 0, 0, 0, 0); }

int bind(int fd, const void *addr, int addrlen) {
    return syscall(SYS_bind, fd, (long)addr, addrlen, 0, 0, 0);
}

int listen(int fd, int backlog) {
    return syscall(SYS_listen, fd, backlog, 0, 0, 0, 0);
}

int accept(int fd, void *addr, int *addrlen) {
    return syscall(SYS_accept, fd, (long)addr, (long)addrlen, 0, 0, 0);
}

extern int main(int argc, char** argv, char** envp) __attribute__((weak));

int symlink(const char *target, const char *linkpath) { return syscall(55, (long)target, (long)linkpath, 0, 0, 0, 0); }
int readlink(const char *path, char *buf, size_t size) { return syscall(56, (long)path, (long)buf, size, 0, 0, 0); }

static char strerror_buf[64];

char *strerror(int errnum) {
    if (errnum == 0) return "Success";
    snprintf(strerror_buf, sizeof(strerror_buf), "Unknown error %d", errnum);
    return strerror_buf;
}

void perror(const char *s) {
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    char *err = strerror(errno);
    write(2, err, strlen(err));
    write(2, "\n", 1);
}

DIR *opendir(const char *name) {
    DIR *d = (DIR*)malloc(sizeof(DIR));
    if (!d) return NULL;
    strncpy(d->path, name, 255);
    d->path[255] = '\0';
    d->count = 0;
    d->index = 0;

    int n = syscall(SYS_readdir, (long)d->path, (long)d->entries, 64, 0, 0, 0);
    if (n < 0) {
        free(d);
        return NULL;
    }
    d->count = n;
    return d;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp || dirp->index >= dirp->count) return NULL;
    return &dirp->entries[dirp->index++];
}

int closedir(DIR *dirp) {
    if (dirp) free(dirp);
    return 0;
}

int isatty(int fd) {
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

int access(const char *pathname, int mode) {
    struct stat st;
    if (stat(pathname, &st) == 0) return 0;
    return -1;
}

int fcntl(int fd, int cmd, ...) {
    long arg = 0;
    if (cmd == F_SETFL) {
        va_list args;
        va_start(args, cmd);
        arg = va_arg(args, long);
        va_end(args);
    }
    return syscall(SYS_fcntl, fd, cmd, arg, 0, 0, 0);
}

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char *const argv[], const char *optstring) {
    static char *next = NULL;

    if (optind >= argc) return -1;

    char *arg = argv[optind];

    if (!next || *next == '\0') {
        if (arg[0] != '-' || arg[1] == '\0') return -1;

        if (arg[1] == '-' && arg[2] == '\0') {
            optind++;
            return -1;
        }

        next = arg + 1;
    }

    char c = *next++;
    const char *match = strchr(optstring, c);

    if (!match || c == ':') {
        optopt = c;
        if (*next == '\0') optind++;
        if (opterr && optstring[0] != ':') {
            write(2, "getopt: invalid option -- '", 27);
            write(2, &c, 1);
            write(2, "'\n", 2);
        }
        return '?';
    }

    if (match[1] == ':') {
        if (*next != '\0') {
            optarg = next;
            next = NULL;
        } else if (optind + 1 < argc) {
            optarg = argv[++optind];
        } else {
            optopt = c;
            if (optstring[0] == ':') return ':';
            if (opterr) {
                write(2, "getopt: option requires an argument -- '", 40);
                write(2, &c, 1);
                write(2, "'\n", 2);
            }
            return '?';
        }
        optind++;
    } else {
        optarg = NULL;
        if (*next == '\0') optind++;
    }

    return c;
}

unsigned int getuid(void) { return 0; }
unsigned int geteuid(void) { return 0; }
unsigned int getgid(void) { return 0; }
unsigned int getegid(void) { return 0; }

static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    time_t t = *timep;
    result->tm_sec = t % 60;
    t /= 60;
    result->tm_min = t % 60;
    t /= 60;
    result->tm_hour = t % 24;
    t /= 24;

    result->tm_wday = (t + 4) % 7;

    int year = 1970;
    while (1) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        if (t < days_in_year) break;
        t -= days_in_year;
        year++;
    }
    result->tm_year = year - 1900;
    result->tm_yday = t;

    int mon = 0;
    while (1) {
        int dim = days_in_month[mon];
        if (mon == 1 && is_leap_year(year)) dim++;
        if (t < dim) break;
        t -= dim;
        mon++;
    }
    result->tm_mon = mon;
    result->tm_mday = t + 1;
    result->tm_isdst = 0;

    return result;
}

static struct tm tm_buffer;

struct tm *gmtime(const time_t *timep) {
    return gmtime_r(timep, &tm_buffer);
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    return gmtime_r(timep, result);
}

struct tm *localtime(const time_t *timep) {
    return gmtime(timep);
}

time_t mktime(struct tm *tm) {
    time_t days = 0;
    int year = tm->tm_year + 1900;

    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    for (int m = 0; m < tm->tm_mon; m++) {
        days += days_in_month[m];
        if (m == 1 && is_leap_year(year)) days++;
    }

    days += tm->tm_mday - 1;

    return days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

static void append_str(char **dst, size_t *remaining, const char *src) {
    while (*src && *remaining > 1) {
        **dst = *src++;
        (*dst)++;
        (*remaining)--;
    }
    **dst = '\0';
}

static void append_num(char **dst, size_t *remaining, int val, int width) {
    char buf[16];
    int i = 0;

    if (val < 0) val = 0;

    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    while (i < width) buf[i++] = '0';

    while (i > 0 && *remaining > 1) {
        **dst = buf[--i];
        (*dst)++;
        (*remaining)--;
    }
    **dst = '\0';
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    if (max == 0) return 0;

    char *dst = s;
    size_t remaining = max;

    while (*fmt && remaining > 1) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'Y':
                    append_num(&dst, &remaining, tm->tm_year + 1900, 4);
                    break;
                case 'm':
                    append_num(&dst, &remaining, tm->tm_mon + 1, 2);
                    break;
                case 'd':
                    append_num(&dst, &remaining, tm->tm_mday, 2);
                    break;
                case 'H':
                    append_num(&dst, &remaining, tm->tm_hour, 2);
                    break;
                case 'M':
                    append_num(&dst, &remaining, tm->tm_min, 2);
                    break;
                case 'S':
                    append_num(&dst, &remaining, tm->tm_sec, 2);
                    break;
                case 'n':
                    append_str(&dst, &remaining, "\n");
                    break;
                case 't':
                    append_str(&dst, &remaining, "\t");
                    break;
                case '%':
                    append_str(&dst, &remaining, "%");
                    break;
                case '\0':
                    fmt--;
                    break;
                default:
                    append_str(&dst, &remaining, "%");
                    if (remaining > 1) {
                        append_str(&dst, &remaining, (char[]){*fmt, '\0'});
                    }
                    break;
            }
        } else {
            append_str(&dst, &remaining, (char[]){*fmt, '\0'});
        }
        fmt++;
    }

    return max - remaining;
}

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

int raise(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        _exit(128 + sig);
    }
    return -1;
}
