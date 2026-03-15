#ifndef SCHED_H
#define SCHED_H

#include "../include/types.h"

#define MAX_PROCESSES 32
#define STACK_SIZE 16384
#define TIME_SLICE 10

typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct {
    u32 pid;
    u32 ppid;
    char name[32];
    process_state_t state;
    u64 rsp;
    u64 rbp;
    u64 *kstack;
    u64 cr3;
    u32 time_slice;
    u64 sleep_until;
} process_t;

void sched_init(void);
int sched_create_process(const char *name, void (*entry)(void));
void sched_exit(int code);
void sched_yield(void);
void sched_sleep(u32 ticks);
process_t *sched_current(void);
void sched_tick(void);
int sched_get_processes(process_t** buf, int max);
int sched_kill(int pid);

#endif
