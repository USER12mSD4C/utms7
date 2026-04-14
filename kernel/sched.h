// kernel/sched.h
#ifndef SCHED_H
#define SCHED_H

#include "../include/types.h"

#define MAX_PROCESSES 64
#define STACK_SIZE 16384
#define TIME_SLICE 10
#define KERNEL_STACK_SIZE 4096
#define OFFSET_KSTACK_TOP  40   // смещение поля kstack_top в process_t
#define OFFSET_CR3         32   // смещение поля cr3

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
    PROC_BLOCKED
} proc_state_t;

typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rbx;
    u64 rip, cs, rflags, rsp, ss;
    u64 rax;
} __attribute__((packed)) proc_regs_t;

typedef struct process_t {
    u32 pid;
    u32 ppid;
    char name[32];
    proc_state_t state;

    u64 cr3;
    u64 kstack;
    u64 kstack_top;
    u64 ustack;
    u64 ustack_top;

    proc_regs_t regs;

    u32 ticks_left;
    u32 sleep_until;

    u64 user_rip;
    u64 user_rsp;
    u64 heap_start;
    u64 heap_end;

    struct {
        int used;
        int type;
        union {
            struct {
                char path[256];
                u32 pos;
            } file;
            struct {
                int pipe_id;
                int end;
            } pipe;
            struct {
                int sock_id;
            } socket;
        } data;
    } fds[32];

    int exit_code;
    struct process_t* waiting_for;
    struct process_t *next;

} process_t;

extern process_t *current;
extern volatile int sched_need_resched;

void sched_init(void);
void sched_start(void);
int sched_create_kthread(const char* name, void (*entry)(void*), void* arg);
void sched_exit(int code);
void sched_yield(void);
void sched_sleep(u32 ms);
void sched_tick(void);
process_t* sched_pick_next(void);
u32 sched_get_pid(void);
u32 sched_get_ppid(void);
int sched_get_processes(process_t** buf, int max);
int sched_kill(int pid);
process_t* sched_current(void);
int sched_waitpid(u32 pid, int* status);

#endif
