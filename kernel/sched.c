#include "sched.h"
#include "memory.h"
#include "idt.h"
#include "../include/string.h"

static process_t processes[MAX_PROCESSES];
static process_t *current = NULL;
static process_t *idle = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;

extern void context_switch(u64 *old_rsp, u64 new_rsp);

static void idle_process(void) {
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void sched_init(void) {
    memset(processes, 0, sizeof(processes));
    
    // Создаем idle процесс
    idle = &processes[0];
    idle->pid = next_pid++;
    strcpy(idle->name, "idle");
    idle->state = PROCESS_READY;
    idle->kstack = kmalloc(STACK_SIZE);
    idle->rsp = (u64)idle->kstack + STACK_SIZE - 8;
    
    current = idle;
    process_count = 1;
}

int sched_create_process(const char *name, void (*entry)(void)) {
    if (process_count >= MAX_PROCESSES) return -1;
    
    process_t *p = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_ZOMBIE || processes[i].state == 0) {
            p = &processes[i];
            break;
        }
    }
    if (!p) return -1;
    
    memset(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = current ? current->pid : 0;
    strcpy(p->name, name);
    p->state = PROCESS_READY;
    p->time_slice = TIME_SLICE;
    
    p->kstack = kmalloc(STACK_SIZE);
    if (!p->kstack) return -1;
    
    u64 *stack = (u64*)((u64)p->kstack + STACK_SIZE);
    
    *--stack = 0; // r15
    *--stack = 0; // r14
    *--stack = 0; // r13
    *--stack = 0; // r12
    *--stack = 0; // r11
    *--stack = 0; // r10
    *--stack = 0; // r9
    *--stack = 0; // r8
    *--stack = 0; // rbp
    *--stack = 0; // rsi
    *--stack = 0; // rdi
    *--stack = 0; // rdx
    *--stack = 0; // rcx
    *--stack = 0; // rbx
    *--stack = 0; // rax
    *--stack = 0x202; // rflags (IF=1)
    *--stack = (u64)entry; // rip
    
    p->rsp = (u64)stack;
    p->rbp = (u64)stack;
    
    process_count++;
    return p->pid;
}

void sched_exit(int code) {
    (void)code;
    if (!current || current == idle) return;
    
    current->state = PROCESS_ZOMBIE;
    process_count--;
    
    if (current->kstack) kfree(current->kstack);
    
    sched_yield();
}

void sched_yield(void) {
    if (!current) return;
    
    process_t *next = idle;
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_READY && &processes[i] != current) {
            next = &processes[i];
            break;
        }
    }
    
    if (next != current) {
        process_t *prev = current;
        current = next;
        
        prev->state = PROCESS_READY;
        next->state = PROCESS_RUNNING;
        next->time_slice = TIME_SLICE;
        
        context_switch(&prev->rsp, next->rsp);
    }
}

void sched_sleep(u32 ticks) {
    if (!current || current == idle) return;
    current->state = PROCESS_WAITING;
    current->sleep_until = system_ticks + ticks;
    sched_yield();
}

process_t *sched_current(void) {
    return current;
}

void sched_tick(void) {
    if (!current || current == idle) return;
    
    current->time_slice--;
    if (current->time_slice <= 0) {
        sched_yield();
    }
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_WAITING && 
            processes[i].sleep_until <= system_ticks) {
            processes[i].state = PROCESS_READY;
        }
    }
}
