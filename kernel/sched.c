#include "sched.h"
#include "memory.h"
#include "idt.h"
#include "../include/string.h"
#include "paging.h"

#define SIGINT 2

static process_t processes[MAX_PROCESSES];
static process_t *current = NULL;
static process_t *idle = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;

extern void context_switch(u64 *old_rsp, u64 new_rsp, u64 new_cr3);

static void idle_process(void) {
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static u64* create_address_space(void) {
    u64* pml4 = kmalloc(4096);
    if (!pml4) return NULL;
    memset(pml4, 0, 4096);
    
    u64* kernel_pml4 = (u64*)0x1000;
    for (int i = 256; i < 512; i++) {
        pml4[i] = kernel_pml4[i];
    }
    
    return pml4;
}

static void free_address_space(u64* pml4) {
    if (!pml4 || pml4 == (u64*)0x1000) return;
    
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PAGE_PRESENT) {
            u64* pdpt = (u64*)(pml4[i] & ~0xFFF);
            for (int j = 0; j < 512; j++) {
                if (pdpt[j] & PAGE_PRESENT) {
                    u64* pd = (u64*)(pdpt[j] & ~0xFFF);
                    for (int k = 0; k < 512; k++) {
                        if (pd[k] & PAGE_PRESENT) {
                            u64* pt = (u64*)(pd[k] & ~0xFFF);
                            for (int l = 0; l < 512; l++) {
                                if (pt[l] & PAGE_PRESENT) {
                                    u64 phys = pt[l] & ~0xFFF;
                                    kfree((void*)phys);
                                }
                            }
                            kfree(pt);
                        }
                    }
                    kfree(pd);
                }
            }
            kfree(pdpt);
        }
    }
    kfree(pml4);
}

void sched_init(void) {
    memset(processes, 0, sizeof(processes));
    
    idle = &processes[0];
    idle->pid = next_pid++;
    strcpy(idle->name, "idle");
    idle->state = PROCESS_READY;
    idle->kstack = kmalloc(STACK_SIZE);
    if (!idle->kstack) return;
    
    idle->rsp = (u64)idle->kstack + STACK_SIZE - 8;
    idle->cr3 = 0;
    idle->ppid = 0;
    idle->time_slice = TIME_SLICE;
    idle->sleep_until = 0;
    idle->heap_start = 0;
    idle->heap_end = 0;
    idle->user_rsp = 0;
    idle->user_rip = 0;
    
    for (int i = 0; i < 32; i++) {
        idle->fd_table[i].used = 0;
    }
    
    current = idle;
    process_count = 1;
}

int sched_create_process(const char *name, void (*entry)(void)) {
    if (process_count >= MAX_PROCESSES) return -1;
    
    process_t *p = NULL;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_ZOMBIE || processes[i].state == 0) {
            p = &processes[i];
            break;
        }
    }
    if (!p) return -1;
    
    memset(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->name[31] = '\0';
    p->state = PROCESS_READY;
    p->time_slice = TIME_SLICE;
    p->sleep_until = 0;
    
    p->kstack = kmalloc(STACK_SIZE);
    if (!p->kstack) return -1;
    
    p->cr3 = (u64)create_address_space();
    if (!p->cr3) {
        kfree(p->kstack);
        return -1;
    }
    
    u64 *stack = (u64*)((u64)p->kstack + STACK_SIZE);
    
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0x202;
    *--stack = (u64)entry;
    
    p->rsp = (u64)stack;
    p->rbp = (u64)stack;
    p->heap_start = 0;
    p->heap_end = 0;
    p->user_rsp = 0;
    p->user_rip = 0;
    
    for (int i = 0; i < 32; i++) {
        p->fd_table[i].used = 0;
    }
    
    process_count++;
    return p->pid;
}

int sched_create_user_process(const char *name, u8 *elf_data, u32 elf_size) {
    if (process_count >= MAX_PROCESSES) return -1;
    
    process_t *p = NULL;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_ZOMBIE || processes[i].state == 0) {
            p = &processes[i];
            break;
        }
    }
    if (!p) return -1;
    
    memset(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->name[31] = '\0';
    p->state = PROCESS_READY;
    p->time_slice = TIME_SLICE;
    p->sleep_until = 0;
    
    p->kstack = kmalloc(STACK_SIZE);
    if (!p->kstack) return -1;
    
    p->cr3 = (u64)create_address_space();
    if (!p->cr3) {
        kfree(p->kstack);
        return -1;
    }
    
    extern int elf_load_user(u8*, void*);
    if (elf_load_user(elf_data, p) != 0) {
        kfree(p->kstack);
        free_address_space((u64*)p->cr3);
        return -1;
    }
    
    u64 *stack = (u64*)((u64)p->kstack + STACK_SIZE);
    
    *--stack = 0x23;
    *--stack = p->user_rsp;
    *--stack = 0x202;
    *--stack = 0x1B;
    *--stack = p->user_rip;
    
    p->rsp = (u64)stack;
    p->rbp = (u64)stack;
    
    for (int i = 0; i < 3; i++) {
        p->fd_table[i].used = 1;
        p->fd_table[i].type = 0;
        if (i == 0) strcpy(p->fd_table[i].file.path, "/dev/stdin");
        if (i == 1) strcpy(p->fd_table[i].file.path, "/dev/stdout");
        if (i == 2) strcpy(p->fd_table[i].file.path, "/dev/stderr");
        p->fd_table[i].file.pos = 0;
    }
    
    process_count++;
    return p->pid;
}

void sched_exit(int code) {
    (void)code;
    if (!current || current == idle) return;
    
    current->state = PROCESS_ZOMBIE;
    process_count--;
    
    if (current->kstack) {
        kfree(current->kstack);
        current->kstack = NULL;
    }
    
    if (current->cr3 != 0) {
        free_address_space((u64*)current->cr3);
        current->cr3 = 0;
    }
    
    sched_yield();
}

void sched_yield(void) {
    if (!current) return;
    
    u32 start_idx = (current - processes) + 1;
    process_t *next = idle;
    
    for (u32 i = 0; i < MAX_PROCESSES; i++) {
        u32 idx = (start_idx + i) % MAX_PROCESSES;
        if (processes[idx].state == PROCESS_READY) {
            next = &processes[idx];
            break;
        }
    }
    
    if (next != current) {
        process_t *prev = current;
        current = next;
        
        prev->state = PROCESS_READY;
        next->state = PROCESS_RUNNING;
        next->time_slice = TIME_SLICE;
        
        context_switch(&prev->rsp, next->rsp, next->cr3);
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
    
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_WAITING && 
            processes[i].sleep_until <= system_ticks) {
            processes[i].state = PROCESS_READY;
        }
    }
}

int sched_get_processes(process_t** buf, int max) {
    if (!buf) return process_count;
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES && count < max; i++) {
        if (processes[i].state != 0) {
            buf[count] = &processes[i];
            count++;
        }
    }
    return count;
}

int sched_kill(int pid) {
    if (pid <= 0) return -1;
    
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) {
            if (processes[i].state == PROCESS_ZOMBIE) return -1;
            
            processes[i].state = PROCESS_ZOMBIE;
            
            if (processes[i].kstack) {
                kfree(processes[i].kstack);
                processes[i].kstack = NULL;
            }
            
            if (processes[i].cr3 != 0) {
                free_address_space((u64*)processes[i].cr3);
                processes[i].cr3 = 0;
            }
            
            process_count--;
            return 0;
        }
    }
    return -1;
}

void sched_signal(int pid, int sig) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROCESS_ZOMBIE) {
            if (sig == SIGINT) {
                // Помечаем процесс для завершения
                processes[i].state = PROCESS_ZOMBIE;
                if (processes[i].kstack) {
                    kfree(processes[i].kstack);
                    processes[i].kstack = NULL;
                }
                if (processes[i].cr3 != 0) {
                    free_address_space((u64*)processes[i].cr3);
                    processes[i].cr3 = 0;
                }
                process_count--;
            }
            break;
        }
    }
}

int sched_get_current_pid(void) {
    if (current) return current->pid;
    return -1;
}
