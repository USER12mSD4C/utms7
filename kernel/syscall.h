#ifndef SYSCALL_H
#define SYSCALL_H

#include "../include/types.h"

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
#define SYS_ioctl       27
#define SYS_clone       28
#define SYS_disk_list   30
#define SYS_partition_mount 37
#define SYS_partition_umount 38
#define SYS_partition_format 39
#define SYS_socket      40
#define SYS_connect     41
#define SYS_disk_table  42
#define SYS_partition_create 43
#define SYS_partition_delete 44
#define SYS_send        45
#define SYS_recv        46
#define SYS_gethostbyname 47
#define SYS_bind        48
#define SYS_listen      49
#define SYS_accept      50
#define SYS_ps          51
#define SYS_gettime     52
#define SYS_clear       53
#define SYS_setcolor    54
#define SYS_meminfo     55
#define SYS_fork        57

typedef struct {
    u64 rax;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 r10;
    u64 r8;
    u64 r9;
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 rbx;
    u64 rbp;
    u64 rcx;
    u64 r11;
    u64 user_rsp;
} syscall_frame_t;

typedef syscall_frame_t trap_frame_t;

int syscall_init(void);
long syscall_handler_c(trap_frame_t* frame, long num);

#endif
