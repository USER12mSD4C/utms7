#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

// Номера системных вызовов
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

// Файловые операции
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

// Дисковые операции
#define SYS_disk_list   30
#define SYS_disk_read   31
#define SYS_disk_write  32
#define SYS_partition_list 33
#define SYS_partition_create 34
#define SYS_partition_delete 35
#define SYS_partition_format 36
#define SYS_partition_mount 37
#define SYS_partition_umount 38

// Сетевые операции
#define SYS_socket      40
#define SYS_connect     41
#define SYS_bind        42
#define SYS_listen      43
#define SYS_accept      44
#define SYS_send        45
#define SYS_recv        46
#define SYS_gethostbyname 47
#define SYS_getip       48

// Процессы и память
#define SYS_meminfo     50
#define SYS_ps          51
#define SYS_gettime     52

static inline long syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long rax asm("rax") = num;
    register long rdi asm("rdi") = a1;
    register long rsi asm("rsi") = a2;
    register long rdx asm("rdx") = a3;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    
    __asm__ volatile (
        "syscall"
        : "+r"(rax)
        : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    
    return rax;
}

// Обертки для системных вызовов
static inline long sys_exit(int code) {
    return syscall(SYS_exit, code, 0, 0, 0, 0, 0);
}

static inline long sys_read(int fd, void* buf, unsigned long count) {
    return syscall(SYS_read, fd, (long)buf, count, 0, 0, 0);
}

static inline long sys_write(int fd, const void* buf, unsigned long count) {
    return syscall(SYS_write, fd, (long)buf, count, 0, 0, 0);
}

static inline long sys_open(const char* path, int flags, int mode) {
    return syscall(SYS_open, (long)path, flags, mode, 0, 0, 0);
}

static inline long sys_close(int fd) {
    return syscall(SYS_close, fd, 0, 0, 0, 0, 0);
}

static inline long sys_brk(void* addr) {
    return syscall(SYS_brk, (long)addr, 0, 0, 0, 0, 0);
}

static inline long sys_getpid(void) {
    return syscall(SYS_getpid, 0, 0, 0, 0, 0, 0);
}

static inline long sys_getppid(void) {
    return syscall(SYS_getppid, 0, 0, 0, 0, 0, 0);
}

static inline long sys_sleep(unsigned long ticks) {
    return syscall(SYS_sleep, ticks, 0, 0, 0, 0, 0);
}

static inline long sys_yield(void) {
    return syscall(SYS_yield, 0, 0, 0, 0, 0, 0);
}

static inline void* sys_mmap(void* addr, unsigned long size, int prot, int flags, int fd, unsigned long offset) {
    return (void*)syscall(SYS_mmap, (long)addr, size, prot, flags, fd, offset);
}

static inline long sys_munmap(void* addr, unsigned long size) {
    return syscall(SYS_munmap, (long)addr, size, 0, 0, 0, 0);
}

static inline long sys_exec(const char* path, char* const argv[]) {
    return syscall(SYS_exec, (long)path, (long)argv, 0, 0, 0, 0);
}

static inline long sys_waitpid(int pid, int* status, int options) {
    return syscall(SYS_waitpid, pid, (long)status, options, 0, 0, 0);
}

static inline long sys_kill(int pid) {
    return syscall(SYS_kill, pid, 0, 0, 0, 0, 0);
}

static inline long sys_lseek(int fd, long offset, int whence) {
    return syscall(SYS_lseek, fd, offset, whence, 0, 0, 0);
}

static inline long sys_stat(const char* path, void* statbuf) {
    return syscall(SYS_stat, (long)path, (long)statbuf, 0, 0, 0, 0);
}

static inline long sys_fstat(int fd, void* statbuf) {
    return syscall(SYS_fstat, fd, (long)statbuf, 0, 0, 0, 0);
}

static inline long sys_mkdir(const char* path, int mode) {
    return syscall(SYS_mkdir, (long)path, mode, 0, 0, 0, 0);
}

static inline long sys_rmdir(const char* path) {
    return syscall(SYS_rmdir, (long)path, 0, 0, 0, 0, 0);
}

static inline long sys_unlink(const char* path) {
    return syscall(SYS_unlink, (long)path, 0, 0, 0, 0, 0);
}

static inline long sys_rename(const char* oldpath, const char* newpath) {
    return syscall(SYS_rename, (long)oldpath, (long)newpath, 0, 0, 0, 0);
}

static inline long sys_chdir(const char* path) {
    return syscall(SYS_chdir, (long)path, 0, 0, 0, 0, 0);
}

static inline long sys_getcwd(char* buf, unsigned long size) {
    return syscall(SYS_getcwd, (long)buf, size, 0, 0, 0, 0);
}

static inline long sys_readdir(const char* path, void* entries, unsigned long* count) {
    return syscall(SYS_readdir, (long)path, (long)entries, (long)count, 0, 0, 0);
}

static inline long sys_disk_list(void* disks, int max) {
    return syscall(SYS_disk_list, (long)disks, max, 0, 0, 0, 0);
}

static inline long sys_disk_read(int disk, unsigned long lba, void* buffer) {
    return syscall(SYS_disk_read, disk, lba, (long)buffer, 0, 0, 0);
}

static inline long sys_disk_write(int disk, unsigned long lba, void* buffer) {
    return syscall(SYS_disk_write, disk, lba, (long)buffer, 0, 0, 0);
}

static inline long sys_partition_list(int disk, void* partitions, int max) {
    return syscall(SYS_partition_list, disk, (long)partitions, max, 0, 0, 0);
}

static inline long sys_partition_create(const char* dev, unsigned long size_mb, int type) {
    return syscall(SYS_partition_create, (long)dev, size_mb, type, 0, 0, 0);
}

static inline long sys_partition_delete(const char* dev) {
    return syscall(SYS_partition_delete, (long)dev, 0, 0, 0, 0, 0);
}

static inline long sys_partition_format(const char* dev, const char* fstype) {
    return syscall(SYS_partition_format, (long)dev, (long)fstype, 0, 0, 0, 0);
}

static inline long sys_partition_mount(const char* dev, const char* point) {
    return syscall(SYS_partition_mount, (long)dev, (long)point, 0, 0, 0, 0);
}

static inline long sys_partition_umount(void) {
    return syscall(SYS_partition_umount, 0, 0, 0, 0, 0, 0);
}

static inline long sys_socket(int domain, int type, int protocol) {
    return syscall(SYS_socket, domain, type, protocol, 0, 0, 0);
}

static inline long sys_connect(int fd, unsigned long ip, int port) {
    return syscall(SYS_connect, fd, ip, port, 0, 0, 0);
}

static inline long sys_send(int fd, const void* buf, unsigned long len, int flags) {
    return syscall(SYS_send, fd, (long)buf, len, flags, 0, 0);
}

static inline long sys_recv(int fd, void* buf, unsigned long len, int flags) {
    return syscall(SYS_recv, fd, (long)buf, len, flags, 0, 0);
}

static inline long sys_gethostbyname(const char* name, unsigned long* ip) {
    return syscall(SYS_gethostbyname, (long)name, (long)ip, 0, 0, 0, 0);
}

static inline long sys_getip(void) {
    return syscall(SYS_getip, 0, 0, 0, 0, 0, 0);
}

static inline long sys_meminfo(unsigned long* total, unsigned long* used, unsigned long* free) {
    return syscall(SYS_meminfo, (long)total, (long)used, (long)free, 0, 0, 0);
}

static inline long sys_ps(void* processes, int max) {
    return syscall(SYS_ps, (long)processes, max, 0, 0, 0, 0);
}

static inline long sys_gettime(void) {
    return syscall(SYS_gettime, 0, 0, 0, 0, 0, 0);
}

#endif
