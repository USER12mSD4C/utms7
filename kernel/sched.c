#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "idt.h"
#include "../include/string.h"
#include "../include/io.h"

static process_t processes[MAX_PROCESSES];
static process_t *current = NULL;
static process_t *ready_queue = NULL;
static process_t *sleep_queue = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;

static void enqueue_ready(process_t *p) {
    if (!p || p->state != PROC_READY) return;
    if (!ready_queue) {
        ready_queue = p;
        p->next = NULL;
    } else {
        process_t *last = ready_queue;
        while (last->next) last = last->next;
        last->next = p;
        p->next = NULL;
    }
}

static process_t* dequeue_ready(void) {
    if (!ready_queue) return NULL;
    process_t *p = ready_queue;
    ready_queue = ready_queue->next;
    p->next = NULL;
    return p;
}

static void remove_from_ready(process_t *p) {
    if (!ready_queue) return;
    if (ready_queue == p) {
        ready_queue = ready_queue->next;
        p->next = NULL;
        return;
    }
    process_t *prev = ready_queue;
    process_t *cur = ready_queue->next;
    while (cur) {
        if (cur == p) {
            prev->next = cur->next;
            p->next = NULL;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static void enqueue_sleep(process_t *p) {
    if (!p || p->state != PROC_SLEEPING) return;
    if (!sleep_queue || p->sleep_until < sleep_queue->sleep_until) {
        p->next = sleep_queue;
        sleep_queue = p;
        return;
    }
    process_t *prev = sleep_queue;
    process_t *cur = sleep_queue->next;
    while (cur && cur->sleep_until <= p->sleep_until) {
        prev = cur;
        cur = cur->next;
    }
    prev->next = p;
    p->next = cur;
}

static void remove_from_sleep(process_t *p) {
    if (!sleep_queue) return;
    if (sleep_queue == p) {
        sleep_queue = sleep_queue->next;
        p->next = NULL;
        return;
    }
    process_t *prev = sleep_queue;
    process_t *cur = sleep_queue->next;
    while (cur) {
        if (cur == p) {
            prev->next = cur->next;
            p->next = NULL;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static u32 alloc_pid(void) {
    u32 pid = next_pid++;
    if (next_pid >= 10000) next_pid = 1;
    return pid;
}

static process_t* find_free_proc(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            memset(&processes[i], 0, sizeof(process_t));
            return &processes[i];
        }
    }
    return NULL;
}

static void free_process(process_t *p) {
    if (!p) return;
    if (p->kstack) {
        kfree((void*)p->kstack);
        p->kstack = 0;
    }
    if (p->cr3 && p->cr3 != (u64)0x1000) {
        free_address_space((u64*)p->cr3);
        p->cr3 = 0;
    }
    for (int i = 0; i < 32; i++) {
        p->fds[i].used = 0;
    }
}

void sched_init(void) {
    memset(processes, 0, sizeof(processes));
    ready_queue = NULL;
    sleep_queue = NULL;
    current = NULL;
    next_pid = 1;
    process_count = 0;
    
    process_t *idle = find_free_proc();
    if (!idle) return;
    
    idle->pid = alloc_pid();
    strcpy(idle->name, "idle");
    idle->state = PROC_READY;
    idle->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    idle->kstack_top = idle->kstack + KERNEL_STACK_SIZE;
    idle->cr3 = (u64)0x1000;
    
    enqueue_ready(idle);
    current = idle;
    process_count = 1;
}

int sched_create_kthread(const char* name, void (*entry)(void*), void* arg) {
    process_t *p = find_free_proc();
    if (!p) return -1;
    
    p->pid = alloc_pid();
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->state = PROC_READY;
    p->ticks_left = TIME_SLICE;
    
    p->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!p->kstack) {
        p->state = PROC_UNUSED;
        return -1;
    }
    p->kstack_top = p->kstack + KERNEL_STACK_SIZE;
    p->cr3 = (u64)0x1000;
    
    u64 *stack = (u64*)p->kstack_top;
    *--stack = (u64)arg;
    *--stack = (u64)sched_exit;
    *--stack = (u64)entry;
    
    p->regs.rsp = (u64)stack;
    p->regs.rip = (u64)entry;
    p->regs.rflags = 0x202;
    p->regs.cs = 0x08;
    p->regs.ss = 0x10;
    
    enqueue_ready(p);
    process_count++;
    return p->pid;
}

void sched_exit(int code) {
    (void)code;
    if (!current || current->pid == 1) return;
    
    current->state = PROC_ZOMBIE;
    free_process(current);
    process_count--;
    sched_yield();
}

void sched_yield(void) {
    if (!current) return;
    
    if (current->state == PROC_RUNNING) {
        current->state = PROC_READY;
        enqueue_ready(current);
    }
    
    process_t *next = dequeue_ready();
    if (!next) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state == PROC_READY) {
                next = &processes[i];
                remove_from_ready(next);
                break;
            }
        }
        if (!next) return;
    }
    
    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE;
    
    process_t *prev = current;
    current = next;
    
    if (prev != next) {
        __asm__ volatile (
            "push %%rbx\n"
            "push %%rbp\n"
            "push %%r12\n"
            "push %%r13\n"
            "push %%r14\n"
            "push %%r15\n"
            "mov %%rsp, %0\n"
            "mov %1, %%rsp\n"
            "mov %2, %%cr3\n"
            "pop %%r15\n"
            "pop %%r14\n"
            "pop %%r13\n"
            "pop %%r12\n"
            "pop %%rbp\n"
            "pop %%rbx\n"
            : "=m"(prev->regs.rsp)
            : "r"(next->regs.rsp), "r"(next->cr3)
            : "memory"
        );
    }
}

void sched_sleep(u32 ms) {
    if (!current || current->pid == 1) return;
    u32 now = get_ticks();
    current->sleep_until = now + ms;
    current->state = PROC_SLEEPING;
    remove_from_ready(current);
    enqueue_sleep(current);
    sched_yield();
}

void sched_tick(void) {
    if (!current) return;
    
    static u32 last_tick = 0;
    u32 now = get_ticks();
    if (now != last_tick) {
        last_tick = now;
        while (sleep_queue && sleep_queue->sleep_until <= now) {
            process_t *p = sleep_queue;
            sleep_queue = sleep_queue->next;
            p->next = NULL;
            p->state = PROC_READY;
            enqueue_ready(p);
        }
    }
    
    if (current->ticks_left > 0) {
        current->ticks_left--;
    }
    if (current->ticks_left == 0 || current->state != PROC_RUNNING) {
        sched_yield();
    }
}

u32 sched_get_pid(void) {
    return current ? current->pid : 0;
}

u32 sched_get_ppid(void) {
    return current ? current->ppid : 0;
}

int sched_get_processes(process_t** buf, int max) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_UNUSED) count++;
    }
    if (buf && max > 0) {
        int idx = 0;
        for (int i = 0; i < MAX_PROCESSES && idx < max; i++) {
            if (processes[i].state != PROC_UNUSED) {
                buf[idx++] = &processes[i];
            }
        }
    }
    return count;
}

int sched_kill(int pid) {
    if (pid <= 0) return -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == (u32)pid && processes[i].state != PROC_ZOMBIE) {
            processes[i].state = PROC_ZOMBIE;
            processes[i].exit_code = -1;
            free_process(&processes[i]);
            process_count--;
            return 0;
        }
    }
    return -1;
}
