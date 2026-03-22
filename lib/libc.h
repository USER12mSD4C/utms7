// lib/libc.h
#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

// Типы
typedef long ssize_t;
typedef long off_t;

// Структуры
struct stat {
    unsigned long st_size;
    int st_mode;
    int st_blocks;
};

struct dirent {
    char name[256];
    unsigned int size;
    int is_dir;
};

// Системные вызовы (НЕ static, чтобы не конфликтовать с syscall.h)
long syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6);

// ==================== STDIO ====================
int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

// ==================== STDIO (printf family) ====================
int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list args);

// ==================== FILESYSTEM ====================
int mkdir(const char *path, int mode);
int rmdir(const char *path);
int unlink(const char *path);
int rename(const char *old, const char *new);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int readdir(const char *path, struct dirent *entries, int *count);

// ==================== MEMORY ====================
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

// ==================== STRING ====================
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strlen(const char *s);
char *strdup(const char *s);
char *strtok(char *str, const char *delim);

// ==================== MEMORY ====================
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

// ==================== PROCESS ====================
int fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
int waitpid(int pid, int *status, int options);
void _exit(int status);
int getpid(void);
int getppid(void);
unsigned int sleep(unsigned int seconds);
int kill(int pid, int sig);

// ==================== ENVIRONMENT ====================
char *getenv(const char *name);
int putenv(char *string);
int setenv(const char *name, const char *value, int overwrite);
void unsetenv(const char *name);

// ==================== TIME ====================
unsigned int time(void);
unsigned int get_ticks(void);

// ==================== NETWORK ====================
int socket(int domain, int type, int protocol);
int connect(int sockfd, unsigned int ip, unsigned short port);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
int close_socket(int sockfd);
unsigned int gethostbyname(const char *name);

#endif
