#ifndef INCLUDE_SYSCALL_H
#define INCLUDE_SYSCALL_H

#include "../include/types.h"

#define SYS_read            0
#define SYS_write           1
#define SYS_open            2
#define SYS_close           3
#define SYS_stat            4
#define SYS_fstat           5
#define SYS_poll            7
#define SYS_lseek           8
#define SYS_mmap            9
#define SYS_mprotect        10
#define SYS_munmap          11
#define SYS_brk             12
#define SYS_rt_sigaction    13
#define SYS_rt_sigprocmask  14
#define SYS_ioctl           16
#define SYS_access          21
#define SYS_pipe            22
#define SYS_sched_yield     24
#define SYS_dup             32
#define SYS_dup2            33
#define SYS_getpid          39
#define SYS_socket          41
#define SYS_connect         42
#define SYS_accept          43
#define SYS_sendto          44
#define SYS_recvfrom        45
#define SYS_bind            49
#define SYS_listen          50
#define SYS_clone           56
#define SYS_fork            57
#define SYS_execve          59
#define SYS_exit            60
#define SYS_wait4           61
#define SYS_kill            62
#define SYS_fcntl           72
#define SYS_getcwd          79
#define SYS_chdir           80
#define SYS_rename          82
#define SYS_mkdir           83
#define SYS_rmdir           84
#define SYS_unlink          87
#define SYS_symlink         88
#define SYS_readlink        89
#define SYS_getuid          102
#define SYS_getgid          104
#define SYS_geteuid         107
#define SYS_getegid         108
#define SYS_getppid         110
#define SYS_gettid          186
#define SYS_getdents64      217
#define SYS_clock_gettime   228
#define SYS_exit_group      231
#define SYS_openat          257

#define uSYS_disk_list      300
#define uSYS_part_mount     301
#define uSYS_part_umount    302
#define uSYS_part_format    303
#define uSYS_disk_table     304
#define uSYS_part_create    305
#define uSYS_part_delete    306
#define uSYS_gethostbyname  307
#define uSYS_ps             308
#define uSYS_gettime        309
#define uSYS_clear          310
#define uSYS_setcolor       311
#define uSYS_meminfo        312
#define uSYS_fs_register    313
#define uSYS_pci_map        314
#define uSYS_pci_unmap      315
#define uSYS_irq_register   316
#define uSYS_irq_wait       317
#define uSYS_ioport_in      318
#define uSYS_ioport_out     319
#define uSYS_sleep          320

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
} __attribute__((packed)) syscall_frame_t;

typedef syscall_frame_t trap_frame_t;

#endif
