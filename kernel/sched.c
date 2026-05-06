// kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "idt.h"
#include "../include/string.h"
#include "../include/io.h"
#include "../drivers/vga.h"

#define TIME_SLICE_MS       10
#define KERNEL_STACK_SIZE   4096    // 4096 — проверено, работает
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
static process_t *ready_queue_tail = NULL;
static process_t *sleep_queue = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;
volatile int sched_need_resched = 0;
static volatile int sched_locked = 0;
static volatile int sched_initialized = 0;

static volatile u32 pit_ticks = 0;
static volatile u64 tsc_offset = 0;
static volatile u64 tsc_freq_hz = 0;
static volatile int tsc_calibrated = 0;

u64 kstack_top_offset_value = 0;
u64 cr3_offset_value = 0;

static inline u64 rdtsc(void) {
    u32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

static inline void cpu_relax(void) {
    __asm__ volatile ("pause");
}

u32 get_ticks(void) {
    if (!tsc_calibrated) return pit_ticks;
    u64 tsc_now = rdtsc();
    if (tsc_now < tsc_offset) return pit_ticks;
    u64 delta_tsc = tsc_now - tsc_offset;
    return pit_ticks + (u32)((delta_tsc * 1000) / tsc_freq_hz);
}

static void calibrate_tsc(void) {
    u32 start_tick = pit_ticks;
    while (pit_ticks == start_tick) cpu_relax();

    u64 tsc_start = rdtsc();
    start_tick = pit_ticks;

    u32 target = start_tick + 100;
    while (pit_ticks < target) cpu_relax();

    u64 tsc_end = rdtsc();
    u32 pit_delta = pit_ticks - start_tick;

    if (pit_delta > 0) {
        tsc_freq_hz = (tsc_end - tsc_start) * PIT_TARGET_HZ / pit_delta;
        tsc_offset = tsc_start;
        tsc_calibrated = 1;
    }
}

u64 get_microseconds(void) {
    if (!tsc_calibrated) return (u64)pit_ticks * (1000000 / PIT_TARGET_HZ);
    return (get_ticks() * 1000);
}

u32 get_seconds(void) {
    return get_ticks() / PIT_TARGET_HZ;
}

static void pit_handler(void) {
    pit_ticks++;
    if ((pit_ticks % PIT_TARGET_HZ) == 0) {
        tsc_offset = rdtsc();
    }
    if (sched_initialized) {
        sched_tick();
    }
    outb(0x20, 0x20);
}

static void pit_init(void) {
    u16 divisor = PIT_DIVIDER;
    outb(PIT_COMMAND_PORT, 0x36);
    io_wait();
    outb(PIT_CHANNEL0_PORT, (u8)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0_PORT, (u8)((divisor >> 8) & 0xFF));

    idt_register_irq(PIT_IRQ, pit_handler);
    irq_unmask(PIT_IRQ);
}

static void enqueue_ready(process_t *p) {
    if (!p || p->state != PROC_READY) return;
    p->next = NULL;
    if (!ready_queue) {
        ready_queue = p;
        ready_queue_tail = p;
    } else {
        ready_queue_tail->next = p;
        ready_queue_tail = p;
    }
}

static process_t* dequeue_ready(void) {
    process_t *p = ready_queue;
    if (p) {
        ready_queue = ready_queue->next;
        if (!ready_queue) ready_queue_tail = NULL;
        p->next = NULL;
    }
    return p;
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

static void idle_loop(void) {
    while (1) {
        __asm__ volatile ("hlt");
    }
}

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
    "1:  hlt\n"
    "    jmp 1b\n"
);

int sched_init(void) {
    __asm__ volatile ("cli");

    memset(processes, 0, sizeof(processes));
    ready_queue = NULL;
    ready_queue_tail = NULL;
    sleep_queue = NULL;
    current = NULL;
    next_pid = 1;
    process_count = 0;
    pit_ticks = 0;
    tsc_freq_hz = 0;
    tsc_offset = 0;
    tsc_calibrated = 0;
    sched_locked = 0;
    sched_initialized = 0;

    kstack_top_offset_value = (u64)&((process_t*)0)->kstack_top;
    cr3_offset_value = (u64)&((process_t*)0)->cr3;

    process_t *idle = find_free_proc();
    if (!idle) return -1;

    idle->pid = alloc_pid();
    strcpy(idle->name, "idle");
    idle->state = PROC_READY;
    idle->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    idle->cr3 = (u64)0x1000;
    idle->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!idle->kstack) {
        idle->state = PROC_UNUSED;
        return -1;
    }
    idle->kstack_top = idle->kstack + KERNEL_STACK_SIZE - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame*)idle->kstack_top;
    memset(frame, 0, sizeof(*frame));
    frame->rip = (u64)idle_loop;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = idle->kstack_top + sizeof(struct interrupt_frame);
    frame->ss = 0x10;

    enqueue_ready(idle);
    current = idle;
    process_count = 1;

    pit_init();
    sched_initialized = 1;

    // Калибровка TSC через опрос PIT без прерываний
    {
        u32 start_ticks = pit_ticks;
        outb(0x43, 0x00);
        u8 lo = inb(0x40);
        u8 hi = inb(0x40);
        u16 prev = lo | (hi << 8);

        while (pit_ticks == start_ticks) {
            outb(0x43, 0x00);
            lo = inb(0x40);
            hi = inb(0x40);
            u16 cur = lo | (hi << 8);
            if (cur > prev) pit_ticks++;
            prev = cur;
        }

        u64 tsc_start = rdtsc();
        u32 cal_start = pit_ticks;
        u32 target = cal_start + 100;

        while (pit_ticks < target) {
            outb(0x43, 0x00);
            lo = inb(0x40);
            hi = inb(0x40);
            u16 cur = lo | (hi << 8);
            if (cur > prev) pit_ticks++;
            prev = cur;
        }

        u64 tsc_end = rdtsc();
        u32 delta = pit_ticks - cal_start;

        if (delta > 0) {
            tsc_freq_hz = (tsc_end - tsc_start) * PIT_TARGET_HZ / delta;
            tsc_offset = tsc_start;
            tsc_calibrated = 1;
        }
    }

    return 0;
}

int sched_create_kthread(const char* name, void (*entry)(void*), void* arg) {
    if (!sched_initialized) return -1;

    __asm__ volatile ("cli");

    process_t *p = find_free_proc();
    if (!p) {
        __asm__ volatile ("sti");
        return -1;
    }

    p->pid = alloc_pid();
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->name[31] = '\0';
    p->state = PROC_READY;
    p->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    p->cr3 = current ? current->cr3 : (u64)0x1000;

    p->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!p->kstack) {
        p->state = PROC_UNUSED;
        __asm__ volatile ("sti");
        return -1;
    }
    p->kstack_top = p->kstack + KERNEL_STACK_SIZE - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame*)p->kstack_top;
    memset(frame, 0, sizeof(*frame));
    frame->rip = (u64)thread_entry_wrapper;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = p->kstack_top + sizeof(struct interrupt_frame);
    frame->ss = 0x10;

    u64 *stack = (u64*)(p->kstack_top + sizeof(struct interrupt_frame));
    *--stack = (u64)entry;
    *--stack = (u64)arg;
    frame->rsp = (u64)stack;

    for (int i = 0; i < 32; i++) {
        p->fds[i].used = 0;
    }

    p->heap_start = 0x40000000;
    p->heap_end = 0x40000000;

    enqueue_ready(p);
    process_count++;

    __asm__ volatile ("sti");
    return p->pid;
}

process_t* sched_schedule(void) {
    if (sched_locked || !sched_initialized) return current;
    sched_locked = 1;

    process_t *prev = current;
    process_t *next;

    u32 now = get_ticks();
    process_t *sp = sleep_queue;
    process_t *sp_prev = NULL;

    while (sp) {
        if (sp->sleep_until <= now) {
            process_t *waking = sp;
            sp = sp->next;
            if (sp_prev) sp_prev->next = sp;
            else sleep_queue = sp;

            waking->next = NULL;
            waking->state = PROC_READY;
            waking->sleep_until = 0;
            enqueue_ready(waking);
        } else {
            sp_prev = sp;
            sp = sp->next;
        }
    }

    next = dequeue_ready();
    if (!next) next = prev;

    if (next == prev) {
        sched_need_resched = 0;
        sched_locked = 0;
        return next;
    }

    if (prev && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        enqueue_ready(prev);
    }

    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    current = next;
    sched_need_resched = 0;
    sched_locked = 0;
    return next;
}

void sched_tick(void) {
    if (!current || !sched_initialized) return;
    if (current->ticks_left > 0) current->ticks_left--;
    if (current->ticks_left == 0 && current->state == PROC_RUNNING) {
        sched_need_resched = 1;
    }
}

void sched_yield(void) {
    if (!sched_initialized || sched_locked) return;
    __asm__ volatile ("cli");
    sched_need_resched = 1;
    sched_schedule();
    __asm__ volatile ("sti");
}

void sched_sleep(u32 ms) {
    if (!current || !sched_initialized) return;
    __asm__ volatile ("cli");

    u32 ticks_to_sleep = (ms * PIT_TARGET_HZ) / 1000;
    if (ticks_to_sleep == 0) ticks_to_sleep = 1;

    current->state = PROC_SLEEPING;
    current->sleep_until = get_ticks() + ticks_to_sleep;

    process_t **pp = &sleep_queue;
    while (*pp && (*pp)->sleep_until <= current->sleep_until) {
        pp = &(*pp)->next;
    }
    current->next = *pp;
    *pp = current;

    sched_need_resched = 1;
    sched_schedule();
    __asm__ volatile ("sti");
}

void sched_exit(int code) {
    __asm__ volatile ("cli");
    if (current && sched_initialized) {
        current->state = PROC_ZOMBIE;
        current->exit_code = code;
        process_count--;
    }
    sched_need_resched = 1;
    sched_schedule();
    while (1) __asm__ volatile ("hlt");
}

int sched_waitpid(u32 pid, int *status) {
    if (!sched_initialized) return -1;
    while (1) {
        __asm__ volatile ("cli");
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == pid && processes[i].state == PROC_ZOMBIE) {
                if (status) *status = processes[i].exit_code;
                processes[i].state = PROC_UNUSED;
                __asm__ volatile ("sti");
                return pid;
            }
        }
        __asm__ volatile ("sti");
        sched_yield();
    }
}

u32 sched_get_pid(void) { return current ? current->pid : 0; }
u32 sched_get_ppid(void) { return current ? current->ppid : 0; }
process_t* sched_current(void) { return current; }

int sched_get_processes(process_t** buf, int max) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES && count < max; i++) {
        if (processes[i].state != PROC_UNUSED && processes[i].state != PROC_ZOMBIE) {
            buf[count++] = &processes[i];
        }
    }
    return count;
}

int sched_kill(int pid) {
    if (!sched_initialized) return -1;
    __asm__ volatile ("cli");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_UNUSED) {
            processes[i].state = PROC_ZOMBIE;
            processes[i].exit_code = -1;
            __asm__ volatile ("sti");
            return 0;
        }
    }
    __asm__ volatile ("sti");
    return -1;
}

int sched_start(void) {
    if (!current || !sched_initialized) {
        vga_write("SCHED: not initialized\n");
        return -1;
    }
    vga_write("SCHED: enabling interrupts\n");
    for (volatile int i = 0; i < 1000000; i++) cpu_relax();
    __asm__ volatile ("sti");
    while (1) {
        __asm__ volatile ("hlt");
        if (sched_need_resched && sched_initialized && !sched_locked) {
            __asm__ volatile ("cli");
            sched_schedule();
            __asm__ volatile ("sti");
        }
    }
    return 0;
}
