#ifndef SCHED_H
#define SCHED_H

#include "../include/types.h"

#define PROC_UNUSED     0
#define PROC_READY      1
#define PROC_RUNNING    2
#define PROC_SLEEPING   3
#define PROC_BLOCKED    4
#define PROC_ZOMBIE     5

#define TIME_SLICE_MS   10
#define MAX_PROCESSES   64

typedef struct {
    char path[256];
    u32 pos;
} file_data_t;

typedef struct {
    int used;
    int type;
    union {
        file_data_t file;
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
} process_t;

/*
 * interrupt_frame — накладывается на стек после входа в прерывание.
 *
 * Стек растёт вниз. При входе в прерывание процессор кладёт:
 *   [ss] rsp rflags cs rip [error_code]
 * Затем isr.asm делает push vector, push 0, и SAVE_REGS:
 *   push rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15
 *
 * После всех push'ей RSP указывает на r15 (последний push).
 * Поэтому ПЕРВОЕ поле структуры должно быть r15 (оно по адресу RSP).
 * ПОСЛЕДНЕЕ поле — ss (оно по наибольшему адресу).
 */

struct interrupt_frame {
    u64 rax;   // RSP + 0
    u64 rbx;   // RSP + 8
    u64 rcx;   // RSP + 16
    u64 rdx;   // RSP + 24
    u64 rsi;   // RSP + 32
    u64 rdi;   // RSP + 40
    u64 rbp;   // RSP + 48
    u64 r8;    // RSP + 56
    u64 r9;    // RSP + 64
    u64 r10;   // RSP + 72
    u64 r11;   // RSP + 80
    u64 r12;   // RSP + 88
    u64 r13;   // RSP + 96
    u64 r14;   // RSP + 104
    u64 r15;   // RSP + 112

    u64 error_code;  // RSP + 120 (наш push 0)
    u64 vector;      // RSP + 128 (наш push vector)

    u64 rip;    // RSP + 136
    u64 cs;     // RSP + 144
    u64 rflags; // RSP + 152
    u64 rsp;    // RSP + 160
    u64 ss;     // RSP + 168
};

int sched_init(void);
int sched_create_kthread(const char* name, void (*entry)(void*), void* arg);
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

extern volatile int sched_need_resched;

u64 sched_do_switch(struct interrupt_frame *frame);

#endif
