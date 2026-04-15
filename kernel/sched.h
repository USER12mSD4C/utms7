// файл: kernel/sched.h
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

void sched_init(void);
int sched_create_kthread(const char* name, void (*entry)(void*), void* arg);
void sched_start(void);
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

#endif
