// lib/libc.c
#include "libc.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

// Определяем номера syscall вручную
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
#define SYS_socket      40
#define SYS_connect     41
#define SYS_send        45
#define SYS_recv        46
#define SYS_meminfo     50
#define SYS_gettime     52
#define SYS_fork        57
#define SYS_gethostbyname 47

// ==================== СИСТЕМНЫЕ ВЫЗОВЫ ====================

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

// ==================== STDIO ====================

int open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & 0x40) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    return syscall(SYS_open, (long)path, flags, mode, 0, 0, 0);
}

int close(int fd) {
    return syscall(SYS_close, fd, 0, 0, 0, 0, 0);
}

ssize_t read(int fd, void *buf, size_t count) {
    return syscall(SYS_read, fd, (long)buf, count, 0, 0, 0);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return syscall(SYS_write, fd, (long)buf, count, 0, 0, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
    return syscall(SYS_lseek, fd, offset, whence, 0, 0, 0);
}

int stat(const char *path, struct stat *buf) {
    return syscall(SYS_stat, (long)path, (long)buf, 0, 0, 0, 0);
}

int fstat(int fd, struct stat *buf) {
    return syscall(SYS_fstat, fd, (long)buf, 0, 0, 0, 0);
}

// ==================== ФАЙЛОВАЯ СИСТЕМА ====================

int mkdir(const char *path, int mode) {
    return syscall(SYS_mkdir, (long)path, mode, 0, 0, 0, 0);
}

int rmdir(const char *path) {
    return syscall(SYS_rmdir, (long)path, 0, 0, 0, 0, 0);
}

int unlink(const char *path) {
    return syscall(SYS_unlink, (long)path, 0, 0, 0, 0, 0);
}

int rename(const char *old, const char *new) {
    return syscall(SYS_rename, (long)old, (long)new, 0, 0, 0, 0);
}

int chdir(const char *path) {
    return syscall(SYS_chdir, (long)path, 0, 0, 0, 0, 0);
}

char *getcwd(char *buf, size_t size) {
    long res = syscall(SYS_getcwd, (long)buf, size, 0, 0, 0, 0);
    return res > 0 ? buf : NULL;
}

int readdir(const char *path, struct dirent *entries, int *count) {
    return syscall(SYS_readdir, (long)path, (long)entries, (long)count, 0, 0, 0);
}

// ==================== ПАМЯТЬ ====================

typedef struct block_header {
    size_t size;
    struct block_header *next;
    int free;
} block_header_t;

#define HEAP_START 0x60000000
#define BLOCK_ALIGN 8

static block_header_t *free_list = NULL;
static void *heap_brk = (void*)HEAP_START;

static void *sbrk(size_t size) {
    void *old = heap_brk;
    long res = syscall(SYS_brk, (long)heap_brk + size, 0, 0, 0, 0, 0);
    if (res == -1) return (void*)-1;
    heap_brk = (void*)((char*)heap_brk + size);
    return old;
}

static void *align_ptr(void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    if (addr & (BLOCK_ALIGN - 1)) {
        addr += BLOCK_ALIGN - (addr & (BLOCK_ALIGN - 1));
    }
    return (void*)addr;
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
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    block_header_t *block = (block_header_t*)((char*)ptr - sizeof(block_header_t));
    if (block->size >= size + sizeof(block_header_t)) {
        if (block->size >= size + sizeof(block_header_t) + BLOCK_ALIGN) {
            size_t new_total = size + sizeof(block_header_t);
            block_header_t *new = (block_header_t*)((char*)block + new_total);
            new->size = block->size - new_total;
            new->next = block->next;
            new->free = 1;
            block->next = new;
            block->size = new_total;
        }
        return ptr;
    }
    
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

// ==================== PRINTF ====================

int vsnprintf(char *str, size_t size, const char *fmt, va_list args) {
    char *start = str;
    const char *p = fmt;
    int count = 0;
    
    while (*p && count < (int)size - 1) {
        if (*p == '%') {
            p++;
            
            int width = 0;
            int left_justify = 0;
            int zero_pad = 0;
            
            if (*p == '-') {
                left_justify = 1;
                p++;
            }
            if (*p == '0') {
                zero_pad = 1;
                p++;
            }
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
            
            switch (*p) {
                case 'd': {
                    int val = va_arg(args, int);
                    if (val < 0) {
                        *str++ = '-';
                        count++;
                        val = -val;
                    }
                    char num[16];
                    int i = 0;
                    if (val == 0) num[i++] = '0';
                    while (val > 0) {
                        num[i++] = '0' + (val % 10);
                        val /= 10;
                    }
                    int len = i;
                    if (!left_justify && width > len) {
                        int pad = width - len;
                        while (pad-- > 0 && count < (int)size - 1) {
                            *str++ = zero_pad ? '0' : ' ';
                            count++;
                        }
                    }
                    while (i > 0 && count < (int)size - 1) {
                        *str++ = num[--i];
                        count++;
                    }
                    if (left_justify && width > len) {
                        int pad = width - len;
                        while (pad-- > 0 && count < (int)size - 1) {
                            *str++ = ' ';
                            count++;
                        }
                    }
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    char num[16];
                    int i = 0;
                    if (val == 0) num[i++] = '0';
                    while (val > 0) {
                        num[i++] = '0' + (val % 10);
                        val /= 10;
                    }
                    while (i > 0 && count < (int)size - 1) {
                        *str++ = num[--i];
                        count++;
                    }
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    char num[16];
                    int i = 0;
                    if (val == 0) num[i++] = '0';
                    while (val > 0) {
                        int d = val % 16;
                        num[i++] = d < 10 ? '0' + d : 'a' + d - 10;
                        val /= 16;
                    }
                    while (i > 0 && count < (int)size - 1) {
                        *str++ = num[--i];
                        count++;
                    }
                    break;
                }
                case 'X': {
                    unsigned int val = va_arg(args, unsigned int);
                    char num[16];
                    int i = 0;
                    if (val == 0) num[i++] = '0';
                    while (val > 0) {
                        int d = val % 16;
                        num[i++] = d < 10 ? '0' + d : 'A' + d - 10;
                        val /= 16;
                    }
                    while (i > 0 && count < (int)size - 1) {
                        *str++ = num[--i];
                        count++;
                    }
                    break;
                }
                case 's': {
                    char *s = va_arg(args, char*);
                    if (!s) s = "(null)";
                    int len = 0;
                    while (s[len]) len++;
                    
                    if (!left_justify && width > len) {
                        int pad = width - len;
                        while (pad-- > 0 && count < (int)size - 1) {
                            *str++ = ' ';
                            count++;
                        }
                    }
                    while (*s && count < (int)size - 1) {
                        *str++ = *s++;
                        count++;
                    }
                    if (left_justify && width > len) {
                        int pad = width - len;
                        while (pad-- > 0 && count < (int)size - 1) {
                            *str++ = ' ';
                            count++;
                        }
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    *str++ = c;
                    count++;
                    break;
                }
                case '%': {
                    *str++ = '%';
                    count++;
                    break;
                }
                default:
                    *str++ = '%';
                    *str++ = *p;
                    count += 2;
                    break;
            }
        } else {
            *str++ = *p;
            count++;
        }
        p++;
    }
    
    *str = '\0';
    return count;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(str, size, fmt, args);
    va_end(args);
    return res;
}

int sprintf(char *str, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(str, 65536, fmt, args);
    va_end(args);
    return res;
}

int printf(const char *fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) write(1, buf, len);
    return len;
}

// ==================== СТРОКОВЫЕ ФУНКЦИИ ====================

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    size_t i;
    for (i = 0; i < n && src[i]; i++) d[i] = src[i];
    for (; i < n; i++) d[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    size_t i;
    for (i = 0; i < n && src[i]; i++) d[i] = src[i];
    d[i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

static char *strtok_pos = NULL;

char *strtok(char *str, const char *delim) {
    if (str) strtok_pos = str;
    if (!strtok_pos || *strtok_pos == '\0') return NULL;
    
    while (*strtok_pos) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*strtok_pos == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim) break;
        strtok_pos++;
    }
    
    char *start = strtok_pos;
    if (*strtok_pos == '\0') {
        strtok_pos = NULL;
        return start;
    }
    
    while (*strtok_pos) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*strtok_pos == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (is_delim) {
            *strtok_pos = '\0';
            strtok_pos++;
            return start;
        }
        strtok_pos++;
    }
    
    strtok_pos = NULL;
    return start;
}

// ==================== MEMORY FUNCTIONS ====================

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = s;
    for (size_t i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const char *p1 = s1;
    const char *p2 = s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const char *p = s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (char)c) return (void*)(p + i);
    }
    return NULL;
}

// ==================== ПРОЦЕССЫ ====================

int fork(void) {
    return syscall(SYS_fork, 0, 0, 0, 0, 0, 0);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall(SYS_exec, (long)path, (long)argv, (long)envp, 0, 0, 0);
}

int execvp(const char *file, char *const argv[]) {
    return execve(file, argv, NULL);
}

int waitpid(int pid, int *status, int options) {
    return syscall(SYS_waitpid, pid, (long)status, options, 0, 0, 0);
}

void _exit(int status) {
    syscall(SYS_exit, status, 0, 0, 0, 0, 0);
    while(1);
}

int getpid(void) {
    return syscall(SYS_getpid, 0, 0, 0, 0, 0, 0);
}

int getppid(void) {
    return syscall(SYS_getppid, 0, 0, 0, 0, 0, 0);
}

unsigned int sleep(unsigned int seconds) {
    syscall(SYS_sleep, seconds * 1000, 0, 0, 0, 0, 0);
    return 0;
}

int kill(int pid, int sig) {
    return syscall(SYS_kill, pid, sig, 0, 0, 0, 0);
}

// ==================== ОКРУЖЕНИЕ ====================

#define ENV_SIZE 64
static char *environment[ENV_SIZE] = { NULL };
static int env_count = 0;

char *getenv(const char *name) {
    for (int i = 0; i < env_count; i++) {
        char *p = environment[i];
        int j = 0;
        while (name[j] && p[j] && name[j] == p[j]) j++;
        if (name[j] == '\0' && p[j] == '=') {
            return p + j + 1;
        }
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
        if (string[j] == '\0' && p[j] == '=') {
            environment[i] = string;
            return 0;
        }
    }
    
    if (env_count < ENV_SIZE - 1) {
        environment[env_count++] = string;
        environment[env_count] = NULL;
        return 0;
    }
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
            for (int k = i; k < env_count - 1; k++) {
                environment[k] = environment[k+1];
            }
            env_count--;
            environment[env_count] = NULL;
            return;
        }
    }
}

// ==================== ВРЕМЯ ====================

unsigned int time(void) {
    return syscall(SYS_gettime, 0, 0, 0, 0, 0, 0) / 1000;
}

unsigned int get_ticks(void) {
    return syscall(SYS_gettime, 0, 0, 0, 0, 0, 0);
}

// ==================== СЕТЬ ====================

int socket(int domain, int type, int protocol) {
    return syscall(SYS_socket, domain, type, protocol, 0, 0, 0);
}

int connect(int sockfd, unsigned int ip, unsigned short port) {
    return syscall(SYS_connect, sockfd, ip, port, 0, 0, 0);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    return syscall(SYS_send, sockfd, (long)buf, len, flags, 0, 0);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return syscall(SYS_recv, sockfd, (long)buf, len, flags, 0, 0);
}

int close_socket(int sockfd) {
    return syscall(SYS_close, sockfd, 0, 0, 0, 0, 0);
}

unsigned int gethostbyname(const char *name) {
    unsigned int ip;
    long res = syscall(SYS_gethostbyname, (long)name, (long)&ip, 0, 0, 0, 0);
    return res == 0 ? ip : 0;
}
