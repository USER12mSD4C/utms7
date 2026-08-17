// kernel/sched.h
#ifndef SCHED_H
#define SCHED_H

#include "../include/types.h"

#define PROC_UNUSED     0
#define PROC_READY      1
#define PROC_RUNNING    2
#define PROC_SLEEPING   3
#define PROC_BLOCKED    4
#define PROC_ZOMBIE     5

#define FD_TYPE_FILE 0
#define FD_TYPE_DRM  1
#define FD_TYPE_TCP  2
#define FD_TYPE_UNIX 3

#define TIME_SLICE_MS   10
#define MAX_PROCESSES   64

typedef struct {
    char path[256];
    u32 pos;
    u8* buf;
    u32 buf_size;
    u32 buf_capacity;
    u8 dirty;
    u8 flags;
} file_data_t;

typedef struct {
    int used;
    int type;
    union {
        file_data_t file;
        void *unix_sock;
        void *tcp_sock;
    } data;
} fd_entry_t;

typedef struct process {
    u32 pid;
    u32 ppid;
    char name[32];
    u8 state;
    u64 kstack;
    u64 kstack_top;
    u64 cr3;
    u32 ticks_left;
    u32 sleep_until;
    int exit_code;
    struct process *waiting_for;
    struct process *next;
    u64 heap_start;
    u64 heap_end;
    fd_entry_t fds[32];
    u64 user_rip;
    u64 user_rsp;
    u8 fpu_context[512] __attribute__((aligned(16)));
} process_t;

struct interrupt_frame {
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;

    u64 error_code;
    u64 vector;

    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

int sched_init(void);
int sched_create_kthread(const char* name, void (*entry)(void*), void* arg);
int sched_create_process(const char* name, u8* elf_data, u32 elf_size);
int spawn_userspace_init(void);
int sched_start(void);
void sched_yield(void);
void sched_sleep(u32 ms);
void sched_exit(int code);
int sched_waitpid(u32 pid, int* status);
int sched_kill(int pid);
u32 sched_get_pid(void);
u32 sched_get_ppid(void);
process_t* sched_current(void);
int sched_get_processes(process_t** buf, int max);
void sched_tick(void);
u32 get_ticks(void);
u64 get_microseconds(void);
u32 get_seconds(void);
int sched_clone(u64 user_rip, u64 user_rsp);

extern volatile int sched_need_resched;

u64 sched_do_switch(struct interrupt_frame *frame);

void sched_block_on(void *channel);
void sched_wakeup(void *channel);

int sched_fork(void *frame);

#endif
