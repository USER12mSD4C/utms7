// kernel/syscall.h
#ifndef SYSCALL_H
#define SYSCALL_H

#include "../include/types.h"

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

// Сетевые
#define SYS_socket      40
#define SYS_connect     41
#define SYS_send        45
#define SYS_recv        46
#define SYS_gethostbyname 47

// Информационные
#define SYS_meminfo     50
#define SYS_gettime     52
#define SYS_fork        57

int syscall_init(void);
long syscall_handler_c(long num, long a1, long a2, long a3, long a4, long a5, long a6);

#endif
