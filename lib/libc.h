#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include "../include/types.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define LONG_MAX  9223372036854775807L
#define LONG_MIN  (-LONG_MAX - 1L)
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULONG_MAX 18446744073709551615UL
#define ULLONG_MAX 18446744073709551615ULL

typedef long ssize_t;
typedef long off_t;

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 32767

extern int errno;

struct stat {
    unsigned long st_size;
    int st_mode;
    int st_blocks;
};

struct dirent {
    u16 mode;
    u16 uid;
    u16 gid;
    u8  nlink;
    u8  reserved[3];
    u32 size;
    u32 blocks;
    u32 direct[10];
    u32 indirect;
    u32 atime;
    u32 mtime;
    u32 ctime;
    char name[28];
    u8 is_dir;
    u8 pad[1];
} __attribute__((packed));

typedef struct _FILE {
    int fd;
    int eof;
    int error;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

long syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6);

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int ioctl(int fd, unsigned long request, void *arg);

int mkdir(const char *path, int mode);
int rmdir(const char *path);
int unlink(const char *path);
int rename(const char *old, const char *new);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int readdir(const char *path, struct dirent *entries, int *count);

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

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

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int isxdigit(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int iscntrl(int c);
int isgraph(int c);
int isascii(int c);
int toupper(int c);
int tolower(int c);
int toascii(int c);

long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
int atoi(const char *nptr);
long atol(const char *nptr);
double atof(const char *nptr);
void srand(unsigned int seed);
int rand(void);
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void abort(void);
void exit(int status);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
int getchar(void);
int putchar(int c);
int puts(const char *s);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int ungetc(int c, FILE *stream);

int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
int vsprintf(char *str, const char *fmt, va_list args);
int vsnprintf(char *str, size_t size, const char *fmt, va_list args);
int vfprintf(FILE *stream, const char *fmt, va_list args);

int scanf(const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int fscanf(FILE *stream, const char *fmt, ...);
int vscanf(const char *fmt, va_list args);
int vsscanf(const char *str, const char *fmt, va_list args);
int vfscanf(FILE *stream, const char *fmt, va_list args);

int fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
int waitpid(int pid, int *status, int options);
void _exit(int status);
int getpid(void);
int getppid(void);
unsigned int sleep(unsigned int seconds);
int kill(int pid, int sig);

char *getenv(const char *name);
int putenv(char *string);
int setenv(const char *name, const char *value, int overwrite);
void unsetenv(const char *name);

unsigned int time(void);
unsigned int getticks(void);

int disk_list(void* disks, int max);
int partition_mount(const char* dev, const char* point);
int partition_umount(void);
int partition_format(const char* dev, const char* fstype);
int disk_create_table(const char *dev, int gpt);
int partition_create(const char *dev, unsigned long size_mb, int type);
int partition_delete(const char *dev);

int socket(int domain, int type, int protocol);
int connect(int sockfd, unsigned int ip, unsigned short port);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
int close_socket(int sockfd);
unsigned int gethostbyname(const char *name);

void clear_screen(void);
void set_color(int fg, int bg);

#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

struct sockaddr_un {
    u16 sun_family;
    char sun_path[108];
};

struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    u32 sin_addr;
};

int bind(int fd, const void *addr, int addrlen);
int listen(int fd, int backlog);
int accept(int fd, void *addr, int *addrlen);

#endif
