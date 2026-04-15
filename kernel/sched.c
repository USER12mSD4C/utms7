// файл: kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "idt.h"
#include "../include/string.h"
#include "../include/io.h"

// === Конфигурация ===
#define TIME_SLICE_MS       10
#define KERNEL_STACK_SIZE   4096
#define MAX_PROCESSES       64

// PIT: частота 1193182 Гц, делитель для ~1000 Гц = 1193 (реальная частота 1193182/1193 ≈ 999.9)
#define PIT_BASE_FREQ       1193182
#define PIT_DIVIDER         1193
#define PIT_TARGET_HZ       1000
#define PIT_COMMAND_PORT    0x43
#define PIT_CHANNEL0_PORT   0x40
#define PIT_IRQ             0

// === Структуры ===
struct interrupt_frame {
    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rip, cs, rflags, rsp, ss;
};

// Глобальные переменные планировщика
static process_t processes[MAX_PROCESSES];
process_t *current = NULL;
static process_t *ready_queue = NULL;
static process_t *sleep_queue = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;
volatile int sched_need_resched = 0;

// Точное время
static volatile u32 pit_ticks = 0;
static volatile u64 tsc_offset = 0;
static volatile u64 tsc_freq_hz = 0;
static volatile u32 last_second_ticks = 0;

// === Вспомогательные функции для работы с TSC ===
static inline u64 rdtsc(void) {
    u32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

// Калибровка TSC с помощью PIT (за 1 секунду)
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

// === Функции получения точного времени ===
u32 get_ticks(void) {
    if (!tsc_freq_hz) return pit_ticks * (1000 / PIT_TARGET_HZ);
    u64 tsc_now = rdtsc();
    u64 delta_tsc = tsc_now - tsc_offset;
    u32 ms = (delta_tsc * 1000) / tsc_freq_hz;
    return ms;
}

u64 get_microseconds(void) {
    if (!tsc_freq_hz) return 0;
    u64 tsc_now = rdtsc();
    u64 delta_tsc = tsc_now - tsc_offset;
    return (delta_tsc * 1000000) / tsc_freq_hz;
}

u32 get_seconds(void) {
    return get_ticks() / 1000;
}

// === Прототип обработчика PIT ===
static void pit_handler(void);

// === PIT инициализация ===
static void pit_init(void) {
    u16 divisor = PIT_DIVIDER;
    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, divisor & 0xFF);
    outb(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xFF);

    idt_register_irq(PIT_IRQ, pit_handler);
    irq_unmask(PIT_IRQ);

    calibrate_tsc();
}

// Обработчик PIT (IRQ0)
static void pit_handler(void) {
    pit_ticks++;
    if (pit_ticks % PIT_TARGET_HZ == 0) {
        tsc_offset = rdtsc();
    }
    sched_tick();
    if (sched_need_resched) {
        // Переключение контекста будет выполнено после возврата из прерывания
    }
    outb(0x20, 0x20);
}

// === Управление очередями с защитой от прерываний ===
static inline void disable_irq(void) { __asm__ volatile ("cli"); }
static inline void enable_irq(void)  { __asm__ volatile ("sti"); }

static void enqueue_ready(process_t *p) {
    if (!p || p->state != PROC_READY) return;
    disable_irq();
    p->next = NULL;
    if (!ready_queue) {
        ready_queue = p;
    } else {
        process_t *last = ready_queue;
        while (last->next) last = last->next;
        last->next = p;
    }
    enable_irq();
}

static process_t* dequeue_ready(void) {
    disable_irq();
    process_t *p = ready_queue;
    if (p) {
        ready_queue = ready_queue->next;
        p->next = NULL;
    }
    enable_irq();
    return p;
}

static void remove_from_ready(process_t *p) {
    if (!ready_queue) return;
    disable_irq();
    if (ready_queue == p) {
        ready_queue = ready_queue->next;
        p->next = NULL;
        enable_irq();
        return;
    }
    process_t *prev = ready_queue, *cur = ready_queue->next;
    while (cur) {
        if (cur == p) {
            prev->next = cur->next;
            p->next = NULL;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    enable_irq();
}

static void enqueue_sleep(process_t *p) {
    if (!p || p->state != PROC_SLEEPING) return;
    disable_irq();
    p->next = NULL;
    if (!sleep_queue || p->sleep_until < sleep_queue->sleep_until) {
        p->next = sleep_queue;
        sleep_queue = p;
    } else {
        process_t *prev = sleep_queue, *cur = sleep_queue->next;
        while (cur && cur->sleep_until <= p->sleep_until) {
            prev = cur;
            cur = cur->next;
        }
        prev->next = p;
        p->next = cur;
    }
    enable_irq();
}

static void remove_from_sleep(process_t *p) {
    if (!sleep_queue) return;
    disable_irq();
    if (sleep_queue == p) {
        sleep_queue = sleep_queue->next;
        p->next = NULL;
        enable_irq();
        return;
    }
    process_t *prev = sleep_queue, *cur = sleep_queue->next;
    while (cur) {
        if (cur == p) {
            prev->next = cur->next;
            p->next = NULL;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    enable_irq();
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
    for (int i = 0; i < 32; i++) p->fds[i] = 0;
}

// === Ассемблерный переключатель контекста ===
static void switch_to(u64 new_cr3, u64 new_rsp) {
    __asm__ volatile (
        "pushq %%rbp\n"
        "pushq %%rbx\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "pushq %%r15\n"
        "movq %%rsp, %0\n"
        "movq %2, %%rsp\n"
        "movq %1, %%cr3\n"
        "popq %%r15\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%rbx\n"
        "popq %%rbp\n"
        : "=m"(current->kstack_top)
        : "r"(new_cr3), "r"(new_rsp)
        : "memory"
    );
}

// idle-процесс
static void idle_loop(void) {
    while (1) __asm__ volatile ("hlt");
}

// Обёртка для запуска потока ядра
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

// === Инициализация планировщика ===
void sched_init(void) {
    memset(processes, 0, sizeof(processes));
    ready_queue = sleep_queue = NULL;
    current = NULL;
    next_pid = 1;
    process_count = 0;
    pit_ticks = 0;
    tsc_freq_hz = 0;
    tsc_offset = 0;

    // Создаём idle-процесс
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

    for (int i = 0; i < 32; i++) idle->fds[i] = 0;
    enqueue_ready(idle);
    current = idle;
    process_count = 1;

    pit_init();
}

// === Создание потока ядра ===
int sched_create_kthread(const char* name, void (*entry)(void*), void* arg) {
    process_t *p = find_free_proc();
    if (!p) return -1;
    p->pid = alloc_pid();
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->state = PROC_READY;
    p->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
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
    for (int i = 0; i < 32; i++) p->fds[i] = 0;
    enqueue_ready(p);
    process_count++;
    return p->pid;
}

// === Переключение на следующий процесс ===
void sched_switch(void) {
    if (sched_need_resched == 0) return;
    disable_irq();
    process_t *prev = current;

    u32 now = get_ticks();
    while (sleep_queue && sleep_queue->sleep_until <= now) {
        process_t *p = sleep_queue;
        sleep_queue = sleep_queue->next;
        p->next = NULL;
        p->state = PROC_READY;
        enqueue_ready(p);
    }

    process_t *next = dequeue_ready();
    if (!next) next = prev;
    if (next == prev) {
        sched_need_resched = 0;
        enable_irq();
        return;
    }

    if (prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        enqueue_ready(prev);
    }

    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    current = next;
    sched_need_resched = 0;

    u64 new_cr3 = next->cr3;
    u64 new_rsp = next->kstack_top;
    enable_irq();

    switch_to(new_cr3, new_rsp);
}

// Вызывается из обработчика таймера
void sched_tick(void) {
    if (!current) return;
    if (current->ticks_left > 0) current->ticks_left--;
    if (current->ticks_left == 0 && current->state == PROC_RUNNING) {
        sched_need_resched = 1;
    }
}

// === Системные вызовы планировщика ===
void sched_yield(void) {
    sched_need_resched = 1;
    __asm__ volatile ("int $0x20");
}

void sched_sleep(u32 ms) {
    if (!current || current->pid == 1) return;
    u32 now = get_ticks();
    current->sleep_until = now + ms;
    current->state = PROC_SLEEPING;
    remove_from_ready(current);
    enqueue_sleep(current);
    sched_need_resched = 1;
    __asm__ volatile ("int $0x20");
}

void sched_exit(int code) {
    if (!current || current->pid == 1) return;
    current->exit_code = code;
    current->state = PROC_ZOMBIE;
    if (current->waiting_for) {
        current->waiting_for->state = PROC_READY;
        enqueue_ready(current->waiting_for);
        current->waiting_for = NULL;
    }
    free_process(current);
    process_count--;
    sched_need_resched = 1;
    __asm__ volatile ("int $0x20");
    while (1) __asm__ volatile ("hlt");
}

int sched_waitpid(u32 pid, int *status) {
    process_t *target = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].pid == pid && processes[i].state != PROC_UNUSED) {
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
    __asm__ volatile ("int $0x20");

    if (status) *status = target->exit_code;
    target->state = PROC_UNUSED;
    process_count--;
    return pid;
}

u32 sched_get_pid(void) { return current ? current->pid : 0; }
u32 sched_get_ppid(void) { return current ? current->ppid : 0; }
process_t* sched_current(void) { return current; }

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

void sched_start(void) {
    if (!current) return;
    __asm__ volatile ("sti");
    sched_yield();
    while(1) __asm__ volatile ("hlt");
}
