// kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "idt.h"
#include "../include/string.h"
#include "../include/io.h"
#include "../drivers/vesa.h"
#include "gdt.h"

#define TIME_SLICE_MS       10
#define KERNEL_STACK_SIZE   8192
#define MAX_PROCESSES       64
#define PIT_BASE_FREQ       1193182
#define PIT_DIVIDER         1193
#define PIT_TARGET_HZ       1000
#define PIT_COMMAND_PORT    0x43
#define PIT_CHANNEL0_PORT   0x40
#define PIT_IRQ             0

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

// === ВСПОМОГАТЕЛЬНЫЕ ===

u64 get_ticks_internal(void) {
    return pit_ticks;
}

u32 get_ticks(void) {
    if (!tsc_calibrated) return pit_ticks;
    u64 tsc_now;
    u32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    tsc_now = ((u64)high << 32) | low;
    if (tsc_now < tsc_offset) return pit_ticks;
    u64 delta_tsc = tsc_now - tsc_offset;
    return pit_ticks + (u32)((delta_tsc * PIT_TARGET_HZ) / tsc_freq_hz);
}

u64 get_microseconds(void) {
    return (get_ticks() * 1000);
}

u32 get_seconds(void) {
    return get_ticks() / PIT_TARGET_HZ;
}

// === ОЧЕРЕДЬ ГОТОВЫХ ===

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

// === ОСНОВНАЯ ФУНКЦИЯ ПЕРЕКЛЮЧЕНИЯ КОНТЕКСТА ===
// Вызывается ТОЛЬКО из контекста прерывания (из isr.asm)
// Не вызывается напрямую из C кода!

u64 sched_do_switch(struct interrupt_frame *frame) {
    if (!sched_initialized) return (u64)frame;
    if (!current) return (u64)frame;

    current->kstack_top = (u64)frame;

    // Обработка спящих
    u32 now = get_ticks_internal();
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

    if (current->state == PROC_RUNNING && current->pid != 1) {
        current->state = PROC_READY;
        enqueue_ready(current);
    }

    process_t *next = dequeue_ready();
    if (!next) {
        next = &processes[0];
    }

    // Если следующий процесс — тот же самый, не переключаемся
    if (next == current) {
        sched_need_resched = 0;
        return (u64)frame;
    }

    process_t *prev = current;
    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);

    tss_set_rsp0(next->kstack + KERNEL_STACK_SIZE);

    if (prev->cr3 != next->cr3) {
        __asm__ volatile ("mov %0, %%cr3" : : "r"(next->cr3) : "memory");
    }

    sched_need_resched = 0;
    current = next;

    return next->kstack_top;
}

// === ПЕРЕКЛЮЧЕНИЕ КОНТЕКСТА БЕЗ ПРЕРЫВАНИЯ (yield/sleep) ===
// Генерирует программное прерывание для унифицированного переключения

__attribute__((noinline))
void sched_do_switch_yield(void) {
    if (!sched_initialized) return;
    if (sched_locked) return;

    __asm__ volatile ("int $0x80");
}

// === ТОЧКА ВХОДА ДЛЯ НОВЫХ ПОТОКОВ ===
//
void sched_thread_entry(void (*entry)(void*), void* arg) {
    __asm__ volatile ("sti");
    entry(arg);
    sched_exit(0);
}

// === УПРАВЛЕНИЕ PID ===

static u32 alloc_pid(void) {
    u32 pid = next_pid++;
    if (next_pid >= 10000) next_pid = 1;
    return pid;
}

// === IDLE ===

static void idle_loop(void) {
    while (1) {
        __asm__ volatile ("sti; hlt");
        if (sched_need_resched) {
            sched_need_resched = 0;
            __asm__ volatile ("int $0x80");
        }
    }
}

// === ИНИЦИАЛИЗАЦИЯ PIT ===

static void pit_handler(void) {
    pit_ticks++;

    if (!sched_initialized) {
        return;
    }

    sched_tick();
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

// === КАЛИБРОВКА TSC ===
static void calibrate_tsc(void) {
    u32 start_ticks = pit_ticks;
    outb(0x43, 0x00);
    u8 lo = inb(0x40);
    u8 hi = inb(0x40);
    u16 prev = lo | (hi << 8);

    // Ждем первый тик
    while (pit_ticks == start_ticks) {
        outb(0x43, 0x00);
        lo = inb(0x40);
        hi = inb(0x40);
        u16 cur = lo | (hi << 8);
        if (cur > prev) pit_ticks++;
        prev = cur;
    }

    // Засекаем TSC
    u64 tsc_start;
    u32 tsc_low, tsc_high;
    __asm__ volatile ("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    tsc_start = ((u64)tsc_high << 32) | tsc_low;

    u32 cal_start = pit_ticks;
    u32 target = cal_start + 100;  // калибруем 100 мс

    // Ждем 100 тиков
    while (pit_ticks < target) {
        outb(0x43, 0x00);
        lo = inb(0x40);
        hi = inb(0x40);
        u16 cur = lo | (hi << 8);
        if (cur > prev) pit_ticks++;
        prev = cur;
    }

    __asm__ volatile ("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    u64 tsc_end = ((u64)tsc_high << 32) | tsc_low;
    u32 delta = pit_ticks - cal_start;

    if (delta > 0) {
        tsc_freq_hz = (tsc_end - tsc_start) * PIT_TARGET_HZ / delta;
        tsc_offset = tsc_start;
        tsc_calibrated = 1;
    }
}

// === ИНИЦИАЛИЗАЦИЯ ПЛАНИРОВЩИКА ===

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
    sched_locked = 0;
    sched_initialized = 0;

    // Создаем idle процесс
    process_t *idle = &processes[0];
    idle->pid = 1;
    strcpy(idle->name, "idle");
    idle->state = PROC_READY;
    idle->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ) + 1000;
    idle->cr3 = (u64)0x1000;

    idle->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!idle->kstack) {
        return -1;
    }

    u64 stack_top = idle->kstack + KERNEL_STACK_SIZE;
    idle->kstack_top = stack_top - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame *)idle->kstack_top;
    memset(frame, 0, sizeof(*frame));

    // Порядок как в isr.asm: сначала SAVE_REGS (r15..rax), потом error_code, vector, потом аппаратные
    frame->rax = 0;
    frame->rbx = 0;
    frame->rcx = 0;
    frame->rdx = 0;
    frame->rsi = 0;
    frame->rdi = 0;
    frame->rbp = 0;
    frame->r8 = 0;
    frame->r9 = 0;
    frame->r10 = 0;
    frame->r11 = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;
    frame->error_code = 0;
    frame->vector = 128;  // Маркер: переключение контекста
    frame->rip = (u64)idle_loop;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = frame->rsp = stack_top;
    frame->ss = 0x10;

    tss_set_rsp0(stack_top);

    enqueue_ready(idle);
    current = idle;
    current->state = PROC_RUNNING;
    process_count = 1;

    pit_init();
    sched_initialized = 1;

    calibrate_tsc();
    current->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
    sched_need_resched = 0;
    __asm__ volatile ("sti");
    return 0;
}

int sched_create_kthread(const char* name, void (*entry)(void*), void* arg) {
    if (!sched_initialized) return -1;

    __asm__ volatile ("cli");

    process_t *p = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            p = &processes[i];
            memset(p, 0, sizeof(process_t));
            break;
        }
    }

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

    // Вершина стека (старший адрес)
    u64 stack_top = p->kstack + KERNEL_STACK_SIZE;

    // Фрейм размещается у вершины стека
    p->kstack_top = stack_top - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame *)p->kstack_top;
    memset(frame, 0, sizeof(*frame));

    frame->rax = 0;
    frame->rbx = 0;
    frame->rcx = 0;
    frame->rdx = 0;
    frame->rsi = (u64)arg;     // второй аргумент
    frame->rdi = (u64)entry;   // первый аргумент
    frame->rbp = 0;
    frame->r8 = 0;
    frame->r9 = 0;
    frame->r10 = 0;
    frame->r11 = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;
    frame->error_code = 0;
    frame->vector = 128;
    frame->rip = (u64)sched_thread_entry;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = stack_top;   // ← Свободное место ВЫШЕ фрейма
    frame->ss = 0x10;

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


// === ДОБРОВОЛЬНАЯ ПЕРЕДАЧА УПРАВЛЕНИЯ ===

void sched_yield(void) {
    if (!sched_initialized) return;

    // Просто ставим флаг, переключение произойдёт при выходе из прерывания
    // или при следующем вызове int 0x80 из sleep/exit
    sched_need_resched = 1;

    // Если мы не в контексте прерывания, форсируем переключение
    // Проверяем по флагу IF в RFLAGS
    u64 rflags;
    __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));
    if (rflags & 0x200) {  // IF = 1, прерывания разрешены
        // Мы не в обработчике прерывания, можно сделать int 0x80
        // Но ждём следующего тика таймера, чтобы не гонять
    }
}

void sched_sleep(u32 ms) {
    if (!current || !sched_initialized) return;

    u32 ticks_to_sleep = (ms * PIT_TARGET_HZ) / 1000;
    if (ticks_to_sleep == 0) ticks_to_sleep = 1;

    current->state = PROC_SLEEPING;
    current->sleep_until = get_ticks_internal() + ticks_to_sleep;

    // Вставляем в очередь спящих
    __asm__ volatile ("cli");
    process_t **pp = &sleep_queue;
    while (*pp && (*pp)->sleep_until <= current->sleep_until) {
        pp = &(*pp)->next;
    }
    current->next = *pp;
    *pp = current;
    __asm__ volatile ("sti");

    // Переключаем контекст через int 0x80
    sched_do_switch_yield();
}

void sched_exit(int code) {
    if (!current || !sched_initialized) return;

    __asm__ volatile ("cli");
    current->state = PROC_ZOMBIE;
    current->exit_code = code;
    process_count--;
    __asm__ volatile ("sti");

    // Переключаем контекст
    sched_do_switch_yield();

    // Сюда не должны вернуться
    while (1) __asm__ volatile ("hlt");
}

void sched_tick(void) {
    if (!current || !sched_initialized) return;
    if (current->ticks_left > 0) current->ticks_left--;
    if (current->ticks_left == 0 && current->state == PROC_RUNNING) {
        current->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);
        sched_need_resched = 1;
    }
}

// === ОЖИДАНИЕ ЗАВЕРШЕНИЯ ===

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

        // Уступаем
        sched_yield();
    }
}

// === ИНФОРМАЦИОННЫЕ ===

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
        print("SCHED: not initialized\n");
        return -1;
    }
    print("SCHED: running\n");

    return 0;
}
