// kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "idt.h"
#include "../include/string.h"
#include "../include/io.h"

#define TIME_SLICE_MS       10
#define KERNEL_STACK_SIZE   4096
#define MAX_PROCESSES       64
#define PIT_BASE_FREQ       1193182
#define PIT_DIVIDER         1193
#define PIT_TARGET_HZ       1000
#define PIT_COMMAND_PORT    0x43
#define PIT_CHANNEL0_PORT   0x40
#define PIT_IRQ             0

struct interrupt_frame {
    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rip, cs, rflags, rsp, ss;
};

static process_t processes[MAX_PROCESSES];
process_t *current = NULL;
static process_t *ready_queue = NULL;
static process_t *sleep_queue = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;
volatile int sched_need_resched = 0;

static volatile u32 pit_ticks = 0;
static volatile u64 tsc_offset = 0;
static volatile u64 tsc_freq_hz = 0;

u64 kstack_top_offset_value = 0;
u64 cr3_offset_value = 0;

static inline u64 rdtsc(void) {
    u32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

static void calibrate_tsc(void) {
    u32 start_ticks = pit_ticks;
    while (pit_ticks == start_ticks) __asm__ volatile ("pause");
    u64 tsc_start = rdtsc();
    start_ticks = pit_ticks;
    while (pit_ticks - start_ticks < 1000) __asm__ volatile ("pause");
    u64 tsc_end = rdtsc();
    tsc_freq_hz = (tsc_end - tsc_start) * PIT_TARGET_HZ / 1000;
    tsc_offset = tsc_start;
}

u32 get_ticks(void) {
    if (!tsc_freq_hz) return pit_ticks;
    u64 tsc_now = rdtsc();
    u64 delta_tsc = tsc_now - tsc_offset;
    return (delta_tsc * 1000) / tsc_freq_hz;
}

u64 get_microseconds(void) { return 0; }
u32 get_seconds(void) { return get_ticks() / 1000; }

static void pit_handler(void) {
    pit_ticks++;
    if (pit_ticks % PIT_TARGET_HZ == 0) tsc_offset = rdtsc();
    sched_tick();
    outb(0x20, 0x20);
}

static void pit_init(void) {
    u16 divisor = PIT_DIVIDER;
    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, divisor & 0xFF);
    outb(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xFF);
    idt_register_irq(PIT_IRQ, pit_handler);
    irq_unmask(PIT_IRQ);
}

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

static void enqueue_ready(process_t *p) {
    if (!p || p->state != PROC_READY) return;
    cli();
    p->next = NULL;
    if (!ready_queue) ready_queue = p;
    else {
        process_t *last = ready_queue;
        while (last->next) last = last->next;
        last->next = p;
    }
    sti();
}

static process_t* dequeue_ready(void) {
    cli();
    process_t *p = ready_queue;
    if (p) ready_queue = ready_queue->next;
    sti();
    return p;
}

static u32 alloc_pid(void) { u32 pid = next_pid++; if (next_pid >= 10000) next_pid = 1; return pid; }

static process_t* find_free_proc(void) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state == PROC_UNUSED) {
            memset(&processes[i], 0, sizeof(process_t));
            return &processes[i];
        }
    return NULL;
}

static void idle_loop(void) { while (1) __asm__ volatile ("hlt"); }

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

int sched_init(void) {
    __asm__ volatile ("cli");
    memset(processes, 0, sizeof(processes));
    ready_queue = sleep_queue = NULL; current = NULL;
    next_pid = 1; process_count = 0;
    pit_ticks = 0; tsc_freq_hz = 0; tsc_offset = 0;

    kstack_top_offset_value = (u64)&((process_t*)0)->kstack_top;
    cr3_offset_value = (u64)&((process_t*)0)->cr3;

    process_t *idle = find_free_proc();
    if (!idle) return -1;
    idle->pid = alloc_pid(); strcpy(idle->name, "idle");
    idle->state = PROC_READY;
    idle->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    idle->kstack_top = idle->kstack + KERNEL_STACK_SIZE - sizeof(struct interrupt_frame);
    idle->cr3 = (u64)0x1000;

    struct interrupt_frame *frame = (struct interrupt_frame*)idle->kstack_top;
    memset(frame, 0, sizeof(*frame));
    frame->rip = (u64)idle_loop;
    frame->cs = 0x08; frame->rflags = 0x202;
    frame->rsp = idle->kstack_top + sizeof(struct interrupt_frame);
    frame->ss = 0x10;

    enqueue_ready(idle); current = idle; process_count = 1;
    pit_init();
    return 0;
}

int sched_create_kthread(const char* name, void (*entry)(void*), void* arg) {
    process_t *p = find_free_proc();
    if (!p) return -1;
    p->pid = alloc_pid(); p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31); p->state = PROC_READY;
    p->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    p->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!p->kstack) { p->state = PROC_UNUSED; return -1; }
    p->kstack_top = p->kstack + KERNEL_STACK_SIZE - sizeof(struct interrupt_frame);
    p->cr3 = (u64)0x1000;

    struct interrupt_frame *frame = (struct interrupt_frame*)p->kstack_top;
    memset(frame, 0, sizeof(*frame));
    frame->rip = (u64)thread_entry_wrapper;
    frame->cs = 0x08; frame->rflags = 0x202;
    frame->rsp = p->kstack_top + sizeof(struct interrupt_frame);
    frame->ss = 0x10;

    u64 *stack = (u64*)(p->kstack_top + sizeof(struct interrupt_frame));
    *--stack = (u64)entry; *--stack = (u64)arg;
    frame->rsp = (u64)stack;

    for (int i = 0; i < 32; i++) p->fds[i].used = 0;
    enqueue_ready(p); process_count++;
    return p->pid;
}

process_t* sched_schedule(void) {
    process_t *prev = current, *next;
    u32 now = get_ticks();
    while (sleep_queue && sleep_queue->sleep_until <= now) {
        process_t *p = sleep_queue; sleep_queue = sleep_queue->next;
        p->state = PROC_READY; enqueue_ready(p);
    }
    next = dequeue_ready();
    if (!next) next = prev;
    if (next == prev) { sched_need_resched = 0; return next; }
    if (prev->state == PROC_RUNNING) { prev->state = PROC_READY; enqueue_ready(prev); }
    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    current = next;
    sched_need_resched = 0;
    return next;
}

void sched_tick(void) {
    if (!current) return;
    if (current->ticks_left > 0) current->ticks_left--;
    if (current->ticks_left == 0 && current->state == PROC_RUNNING) sched_need_resched = 1;
}

void sched_yield(void) { cli(); sched_need_resched = 1; sched_schedule(); sti(); }
void sched_sleep(u32 ms) { /* заглушка */ }
void sched_exit(int code) { (void)code; }
int sched_waitpid(u32 pid, int *status) { (void)pid; (void)status; return -1; }
u32 sched_get_pid(void) { return current ? current->pid : 0; }
u32 sched_get_ppid(void) { return current ? current->ppid : 0; }
process_t* sched_current(void) { return current; }
int sched_get_processes(process_t** buf, int max) { return 0; }
int sched_kill(int pid) { (void)pid; return -1; }
int sched_start(void) { if (!current) return -1; sti(); while (1) __asm__ volatile ("hlt"); return 0; }
