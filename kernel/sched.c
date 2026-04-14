// файл: kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "idt.h"
#include "../include/string.h"
#include "../include/io.h"

static process_t processes[MAX_PROCESSES];
process_t *current = NULL;
static process_t *ready_queue = NULL;
static process_t *sleep_queue = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;
volatile int sched_need_resched = 0;

// Ассемблерная обёртка для запуска потока
// Устанавливает сегментные регистры и вызывает entry(arg)
extern void thread_entry_wrapper(void);
__asm__ (
    ".global thread_entry_wrapper\n"
    "thread_entry_wrapper:\n"
    "    movw $0x10, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %fs\n"
    "    movw %ax, %gs\n"
    "    popq %rdi\n"
    "    popq %rax\n"
    "    call *%rax\n"
    "    xorl %edi, %edi\n"
    "    call sched_exit\n"
);

struct interrupt_frame {
    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rip, cs, rflags, rsp, ss;
};

static void idle_loop(void) {
    while (1) __asm__ volatile ("hlt");
}

// Вспомогательные функции очередей
static void enqueue_ready(process_t *p) {
    if (!p || p->state != PROC_READY) return;
    p->next = NULL;
    if (!ready_queue) {
        ready_queue = p;
    } else {
        process_t *last = ready_queue;
        while (last->next) last = last->next;
        last->next = p;
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
    process_t *prev = ready_queue, *cur = ready_queue->next;
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
    p->next = NULL;
    if (!sleep_queue || p->sleep_until < sleep_queue->sleep_until) {
        p->next = sleep_queue;
        sleep_queue = p;
        return;
    }
    process_t *prev = sleep_queue, *cur = sleep_queue->next;
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
    process_t *prev = sleep_queue, *cur = sleep_queue->next;
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
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state == PROC_UNUSED) {
            memset(&processes[i], 0, sizeof(process_t));
            return &processes[i];
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
    for (int i = 0; i < 32; i++) p->fds[i].used = 0;
}

void sched_init(void) {
    memset(processes, 0, sizeof(processes));
    ready_queue = sleep_queue = NULL;
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
    idle->heap_start = idle->heap_end = 0;

    struct interrupt_frame *frame = (struct interrupt_frame*)idle->kstack_top - 1;
    memset(frame, 0, sizeof(*frame));
    frame->rip = (u64)idle_loop;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = idle->kstack_top;
    frame->ss = 0x10;
    idle->kstack_top = (u64)frame;

    for (int i = 0; i < 32; i++) idle->fds[i].used = 0;
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

    struct interrupt_frame *frame = (struct interrupt_frame*)p->kstack_top - 1;
    memset(frame, 0, sizeof(*frame));
    frame->rip = (u64)thread_entry_wrapper;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = (u64)frame;
    frame->ss = 0x10;

    u64 *stack = (u64*)frame;
    *--stack = (u64)entry;
    *--stack = (u64)arg;
    frame->rsp = (u64)stack;

    p->kstack_top = (u64)frame;
    p->heap_start = p->heap_end = 0;
    for (int i = 0; i < 32; i++) p->fds[i].used = 0;
    enqueue_ready(p);
    process_count++;
    return p->pid;
}

void sched_start(void) {
    process_t *first = dequeue_ready();
    if (!first) return;
    first->state = PROC_RUNNING;
    first->ticks_left = TIME_SLICE;
    current = first;

    // Загружаем контекст первого процесса, включая сегментные регистры
    __asm__ volatile (
        "movq %0, %%rsp\n"
        "movq %1, %%cr3\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "popq %%r15\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%r11\n"
        "popq %%r10\n"
        "popq %%r9\n"
        "popq %%r8\n"
        "popq %%rbp\n"
        "popq %%rdi\n"
        "popq %%rsi\n"
        "popq %%rdx\n"
        "popq %%rcx\n"
        "popq %%rbx\n"
        "popq %%rax\n"
        "iretq\n"
        :
        : "r"(first->kstack_top), "r"(first->cr3)
        : "memory", "ax"
    );
}

process_t* sched_pick_next(void) {
    if (!current) return NULL;
    if (current->state == PROC_RUNNING) {
        current->state = PROC_READY;
        enqueue_ready(current);
    }
    u32 now = get_ticks();
    while (sleep_queue && sleep_queue->sleep_until <= now) {
        process_t *p = sleep_queue;
        sleep_queue = sleep_queue->next;
        p->next = NULL;
        p->state = PROC_READY;
        enqueue_ready(p);
    }
    process_t *next = dequeue_ready();
    if (!next) next = current;
    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE;
    current = next;
    sched_need_resched = 0;
    return next;
}

void sched_exit(int code) {
    if (!current || current->pid == 1) return;
    current->exit_code = code;
    current->state = PROC_ZOMBIE;
    if (current->waiting_for) {
        current->waiting_for->state = PROC_READY;
        enqueue_ready(current->waiting_for);
    }
    free_process(current);
    process_count--;
    sched_need_resched = 1;
    __asm__ volatile ("sti; hlt; cli");
    while (1) __asm__ volatile ("hlt");
}

void sched_yield(void) {
    sched_need_resched = 1;
    __asm__ volatile ("sti; hlt; cli");
}

void sched_sleep(u32 ms) {
    if (!current || current->pid == 1) return;
    u32 now = get_ticks();
    current->sleep_until = now + ms;
    current->state = PROC_SLEEPING;
    remove_from_ready(current);
    enqueue_sleep(current);
    sched_need_resched = 1;
    __asm__ volatile ("sti; hlt; cli");
}

void sched_tick(void) {
    if (!current) return;
    if (current->ticks_left > 0) current->ticks_left--;
    if (current->ticks_left == 0 && current->state == PROC_RUNNING)
        sched_need_resched = 1;
}

u32 sched_get_pid(void) { return current ? current->pid : 0; }
u32 sched_get_ppid(void) { return current ? current->ppid : 0; }
int sched_get_processes(process_t** buf, int max) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state != PROC_UNUSED) count++;
    if (buf && max > 0) {
        int idx = 0;
        for (int i = 0; i < MAX_PROCESSES && idx < max; i++)
            if (processes[i].state != PROC_UNUSED)
                buf[idx++] = &processes[i];
    }
    return count;
}
int sched_kill(int pid) {
    if (pid <= 0) return -1;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].pid == (u32)pid && processes[i].state != PROC_ZOMBIE) {
            processes[i].state = PROC_ZOMBIE;
            processes[i].exit_code = -1;
            free_process(&processes[i]);
            process_count--;
            return 0;
        }
    return -1;
}
process_t* sched_current(void) { return current; }
int sched_waitpid(u32 pid, int* status) {
    process_t *target = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].pid == pid) {
            target = &processes[i];
            break;
        }
    if (!target) return -1;
    if (target->state == PROC_ZOMBIE) {
        if (status) *status = target->exit_code;
        target->state = PROC_UNUSED;
        process_count--;
        return pid;
    }
    current->waiting_for = target;
    current->state = PROC_BLOCKED;
    sched_need_resched = 1;
    __asm__ volatile ("sti; hlt; cli");
    if (status) *status = target->exit_code;
    target->state = PROC_UNUSED;
    process_count--;
    return pid;
}
