// kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "idt.h"
#include "paging.h"
#include "../include/string.h"
#include "../include/io.h"
#include "../drivers/drm.h"
#include "gdt.h"
#include "syscall.h"

#define TIME_SLICE_MS       10
#define KERNEL_STACK_SIZE   8192
#define MAX_PROCESSES       64
#define PIT_BASE_FREQ       1193182
#define PIT_DIVIDER         1193
#define PIT_TARGET_HZ       1000
#define PIT_COMMAND_PORT    0x43
#define PIT_CHANNEL0_PORT   0x40
#define PIT_IRQ             0

extern int ufs_ismounted(void);
extern int ufs_read(const char* path, u8** buf, u32* size);
extern int shell_start_thread(void);
extern u64* create_address_space(void);
extern u64* copy_address_space(u64* src_pml4);
extern void free_address_space(u64* pml4);
extern u64 elf_load(u8 *data, u32 size, u64* pml4, u64* out_max_vaddr);
extern u64 binfmt_load(u8 *data, u32 size, u64* pml4, u64* out_max_vaddr);
extern int paging_map_for_process(u64* pml4, u64 phys_addr, u64 virt_addr, u64 flags);

static process_t processes[MAX_PROCESSES] __attribute__((aligned(16)));
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

static u8 clean_fpu_state[512] __attribute__((aligned(16)));

static inline void save_fpu(void* buffer) {
    __asm__ volatile("fxsave %0" : "=m"(*(char*)buffer) : : "memory");
}

static inline void restore_fpu(void* buffer) {
    __asm__ volatile("fxrstor %0" : : "m"(*(char*)buffer) : "memory");
}

u64 get_ticks_internal(void) {
    return pit_ticks;
}

u32 get_ticks(void) {
    if (!tsc_calibrated || tsc_freq_hz == 0) return pit_ticks;
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

u64 sched_do_switch(struct interrupt_frame *frame) {
    outb(0xE9, 'S');
    if (!sched_initialized) return (u64)frame;
    if (!current) return (u64)frame;

    current->kstack_top = (u64)frame;

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

    if (current->state == PROC_RUNNING && current->pid != 0) {
        current->state = PROC_READY;
        enqueue_ready(current);
    }

    process_t *next = dequeue_ready();
    if (!next) {
        next = &processes[0];
    }

    if (next == current) {
        next->state = PROC_RUNNING;
        sched_need_resched = 0;
        return (u64)frame;
    }

    process_t *prev = current;
    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);

    if (prev->state != PROC_UNUSED && prev->state != PROC_ZOMBIE) {
        save_fpu(prev->fpu_context);
    }
    restore_fpu(next->fpu_context);

    tss_set_rsp0(next->kstack + KERNEL_STACK_SIZE);

    if (prev->cr3 != next->cr3) {
        __asm__ volatile ("mov %0, %%cr3" : : "r"(next->cr3) : "memory");
    }

    sched_need_resched = 0;
    current = next;

    return next->kstack_top;
}

void sched_do_switch_yield(void) {
    if (!sched_initialized) return;
    if (sched_locked) return;

    __asm__ volatile ("int $0x80");
}

void sched_thread_entry(void (*entry)(void*), void* arg) {
    __asm__ volatile ("sti");
    entry(arg);
    sched_exit(0);
}

static u32 alloc_pid(void) {
    u32 pid = next_pid++;
    if (next_pid >= 10000) next_pid = 1;
    return pid;
}

static void idle_loop(void) {
    while (1) {
        __asm__ volatile ("sti; hlt");
        if (sched_need_resched) {
            sched_need_resched = 0;
            __asm__ volatile ("int $0x80");
        }
    }
}

static void pit_handler(void) {
    outb(0xE9, 'T'); // Маячок: pit_handler вызван
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

static void calibrate_tsc(void) {
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

    u64 tsc_start;
    u32 tsc_low, tsc_high;
    __asm__ volatile ("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    tsc_start = ((u64)tsc_high << 32) | tsc_low;

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

    __asm__ volatile ("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    u64 tsc_end = ((u64)tsc_high << 32) | tsc_low;
    u32 delta = pit_ticks - cal_start;

    if (delta > 0 && (tsc_end - tsc_start) > 0) {
        tsc_freq_hz = (tsc_end - tsc_start) * PIT_TARGET_HZ / delta;
        if (tsc_freq_hz > 0) {
            tsc_offset = tsc_start;
            tsc_calibrated = 1;
        }
    }
}

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

    __asm__ volatile("fninit");
    __asm__ volatile("fxsave %0" : "=m"(*(char*)clean_fpu_state) : : "memory");

    process_t *idle = &processes[0];
    idle->pid = 0;
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
    frame->vector = 128;
    frame->rip = (u64)idle_loop;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = stack_top;
    frame->ss = 0x10;

    memcpy(idle->fpu_context, clean_fpu_state, 512);

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

    u64 stack_top = p->kstack + KERNEL_STACK_SIZE;
    p->kstack_top = stack_top - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame *)p->kstack_top;
    memset(frame, 0, sizeof(*frame));

    frame->rax = 0;
    frame->rbx = 0;
    frame->rcx = 0;
    frame->rdx = 0;
    frame->rsi = (u64)arg;
    frame->rdi = (u64)entry;
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
    frame->rsp = stack_top - 8;
    frame->ss = 0x10;

    memcpy(p->fpu_context, clean_fpu_state, 512);

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

void sched_yield(void) {
    if (!sched_initialized) return;

    sched_need_resched = 1;
    __asm__ volatile ("int $0x80");
}

void sched_sleep(u32 ms) {
    if (!current || !sched_initialized) return;

    u32 ticks_to_sleep = (ms * PIT_TARGET_HZ) / 1000;
    if (ticks_to_sleep == 0) ticks_to_sleep = 1;

    current->state = PROC_SLEEPING;
    current->sleep_until = get_ticks_internal() + ticks_to_sleep;

    __asm__ volatile ("cli");
    process_t **pp = &sleep_queue;
    while (*pp && (*pp)->sleep_until <= current->sleep_until) {
        pp = &(*pp)->next;
    }
    current->next = *pp;
    *pp = current;
    __asm__ volatile ("sti");

    sched_do_switch_yield();
}

void sched_exit(int code) {
    if (!current || !sched_initialized) return;

    __asm__ volatile ("cli");
    current->state = PROC_ZOMBIE;
    current->exit_code = code;
    process_count--;
    __asm__ volatile ("sti");

    sched_do_switch_yield();

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
        print("SCHED: not initialized\n");
        return -1;
    }
    print("SCHED: running\n");

    return 0;
}

int sched_clone(u64 user_rip, u64 user_rsp) {
    if (!sched_initialized) return -1;

    __asm__ volatile ("cli");

    process_t *parent = current;
    if (!parent) {
        __asm__ volatile ("sti");
        return -1;
    }

    process_t *child = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            child = &processes[i];
            memset(child, 0, sizeof(process_t));
            break;
        }
    }

    if (!child) {
        __asm__ volatile ("sti");
        return -1;
    }

    child->pid = alloc_pid();
    child->ppid = parent->pid;
    strncpy(child->name, parent->name, 27);
    child->name[27] = '\0';
    child->state = PROC_READY;
    child->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);

    if (parent->cr3 == (u64)0x1000) {
        child->cr3 = parent->cr3;
    } else {
        u64* cas = copy_address_space((u64*)parent->cr3);
        if (!cas) {
            child->state = PROC_UNUSED;
            __asm__ volatile ("sti");
            return -1;
        }
        child->cr3 = (u64)cas;
    }

    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->user_rip = user_rip;
    child->user_rsp = user_rsp;

    for (int i = 0; i < 32; i++) {
        child->fds[i] = parent->fds[i];
    }

    memcpy(child->fpu_context, parent->fpu_context, 512);

    child->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!child->kstack) {
        if (child->cr3 != (u64)0x1000) free_address_space((u64*)child->cr3);
        child->state = PROC_UNUSED;
        __asm__ volatile ("sti");
        return -1;
    }

    u64 stack_top = child->kstack + KERNEL_STACK_SIZE;
    child->kstack_top = stack_top - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame *)child->kstack_top;
    memset(frame, 0, sizeof(*frame));

    frame->rax = 0;
    frame->rbx = 0;
    frame->rcx = 0;
    frame->rdx = 0;
    frame->rsi = 0;
    frame->rdi = 0;
    frame->rbp = 0;
    frame->r8  = 0;
    frame->r9  = 0;
    frame->r10 = 0;
    frame->r11 = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;

    frame->error_code = 0;
    frame->vector = 128;
    frame->rip = user_rip;
    frame->cs = 0x2B;
    frame->rflags = 0x202;
    frame->rsp = user_rsp;
    frame->ss = 0x23;

    enqueue_ready(child);
    process_count++;

    __asm__ volatile ("sti");
    return child->pid;
}

int sched_create_process(const char* name, u8* elf_data, u32 elf_size) {
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

    u64* pml4 = create_address_space();
    if (!pml4) {
        p->state = PROC_UNUSED;
        __asm__ volatile ("sti");
        return -1;
    }
    p->cr3 = (u64)pml4;

    u64 max_vaddr = 0;
    u64 entry = binfmt_load(elf_data, elf_size, pml4, &max_vaddr);
    if (entry == 0) {
        free_address_space(pml4);
        p->state = PROC_UNUSED;
        __asm__ volatile ("sti");
        return -1;
    }
    p->user_rip = entry;
    p->heap_start = (max_vaddr + 4095) & ~4095ULL;
    p->heap_end = p->heap_start;

    u64 user_stack_top = 0x0000004000000000ULL;
    u64 stack_pages = 64;
    for (u64 i = 0; i < stack_pages; i++) {
        u64 phys = (u64)pmm_alloc_page();
        if (!phys) {
            free_address_space(pml4);
            p->state = PROC_UNUSED;
            __asm__ volatile ("sti");
            return -1;
        }
        u64 virt = user_stack_top - (stack_pages - i) * 4096;
        if (paging_map_for_process(pml4, phys, virt, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            pmm_free_page((void*)phys);
            free_address_space(pml4);
            p->state = PROC_UNUSED;
            __asm__ volatile ("sti");
            return -1;
        }
    }

    u64 old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");

    u64 rsp = user_stack_top;
    rsp &= ~0xFULL;

    rsp -= (strlen(name) + 1);
    memcpy((void*)rsp, name, strlen(name) + 1);
    u64 prog_name_ptr = rsp;

    rsp &= ~0xFULL;

    u8 random_bytes[16] = {0};
    rsp -= 16;
    memcpy((void*)rsp, random_bytes, 16);
    u64 at_random_ptr = rsp;

    u64 auxv[] = {
        6, 4096,
        25, at_random_ptr,
        9, entry,
        0, 0
    };
    rsp -= sizeof(auxv);
    memcpy((void*)rsp, auxv, sizeof(auxv));

    u64 envp_null = 0;
    rsp -= 8;
    memcpy((void*)rsp, &envp_null, 8);

    u64 argv_null = 0;
    rsp -= 8;
    memcpy((void*)rsp, &argv_null, 8);

    rsp -= 8;
    memcpy((void*)rsp, &prog_name_ptr, 8);

    u64 argc = 1;
    rsp -= 8;
    memcpy((void*)rsp, &argc, 8);

    rsp &= ~0xFULL;
    rsp -= 8;

    __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");

    p->user_rsp = rsp;

    p->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!p->kstack) {
        free_address_space(pml4);
        p->state = PROC_UNUSED;
        __asm__ volatile ("sti");
        return -1;
    }

    u64 stack_top = p->kstack + KERNEL_STACK_SIZE;
    p->kstack_top = stack_top - sizeof(struct interrupt_frame);

    struct interrupt_frame *frame = (struct interrupt_frame *)p->kstack_top;
    memset(frame, 0, sizeof(*frame));

    frame->rax = 0;
    frame->rbx = 0;
    frame->rcx = 0;
    frame->rdx = 0;
    frame->rsi = rsp + 8;
    frame->rdi = 1;
    frame->rbp = 0;
    frame->r8  = 0;
    frame->r9  = 0;
    frame->r10 = 0;
    frame->r11 = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;

    frame->error_code = 0;
    frame->vector = 128;
    frame->rip = entry;
    frame->cs = 0x2B;
    frame->rflags = 0x202;
    frame->rsp = rsp;
    frame->ss = 0x23;

    memcpy(p->fpu_context, clean_fpu_state, 512);

    for (int i = 0; i < 32; i++) {
        p->fds[i].used = 0;
    }

    enqueue_ready(p);
    process_count++;

    __asm__ volatile ("sti");
    return p->pid;
}

void sched_block_on(void *channel) {
    if (!current || !sched_initialized) return;
    __asm__ volatile ("cli");
    current->state = PROC_BLOCKED;
    current->waiting_for = (struct process *)channel;
    __asm__ volatile ("sti");
    sched_do_switch_yield();
}

void sched_wakeup(void *channel) {
    if (!sched_initialized) return;
    __asm__ volatile ("cli");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_BLOCKED && processes[i].waiting_for == (struct process *)channel) {
            processes[i].state = PROC_READY;
            processes[i].waiting_for = NULL;
            enqueue_ready(&processes[i]);
        }
    }
    __asm__ volatile ("sti");
}

int spawn_userspace_init(void) {
    u8 *init_data = NULL;
    u32 init_size = 0;

    if (ufs_ismounted()) {
        if (ufs_read("/init", &init_data, &init_size) == 0) {
            int pid = sched_create_process("init", init_data, init_size);
            kfree(init_data);
            if (pid >= 0) return 0;
        }
    }

    extern int get_module_data(const char* name, u8** buf, u32* size);
    if (get_module_data("init", &init_data, &init_size) == 0) {
        print("[init] UFS empty, loaded userspace /init from Multiboot2 RAM module\n");
        int pid = sched_create_process("init", init_data, init_size);
        kfree(init_data);
        if (pid >= 0) return 0;
    }

    print("[init] /init not found on UFS and no Multiboot2 module found, system halted.\n");
    return 0;
}

int sched_fork(void *frame_ptr) {
    if (!sched_initialized || !frame_ptr) return -1;

    __asm__ volatile ("cli");

    process_t *parent = current;
    if (!parent) {
        __asm__ volatile ("sti");
        return -1;
    }

    process_t *child = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            child = &processes[i];
            memset(child, 0, sizeof(process_t));
            break;
        }
    }

    if (!child) {
        __asm__ volatile ("sti");
        return -1;
    }

    child->pid = alloc_pid();
    child->ppid = parent->pid;
    strncpy(child->name, parent->name, 31);
    child->name[31] = '\0';
    child->state = PROC_READY;
    child->ticks_left = TIME_SLICE_MS / (1000 / PIT_TARGET_HZ);

    if (parent->cr3 == (u64)0x1000) {
        child->cr3 = parent->cr3;
    } else {
        u64* cas = copy_address_space((u64*)parent->cr3);
        if (!cas) {
            child->state = PROC_UNUSED;
            __asm__ volatile ("sti");
            return -1;
        }
        child->cr3 = (u64)cas;
    }

    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;

    for (int i = 0; i < 32; i++) {
        child->fds[i] = parent->fds[i];
    }

    memcpy(child->fpu_context, parent->fpu_context, 512);

    child->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!child->kstack) {
        if (child->cr3 != (u64)0x1000) free_address_space((u64*)child->cr3);
        child->state = PROC_UNUSED;
        __asm__ volatile ("sti");
        return -1;
    }

    u64 stack_top = child->kstack + KERNEL_STACK_SIZE;
    u64 child_frame_addr = (stack_top - sizeof(struct interrupt_frame)) & ~0xFULL;
    child->kstack_top = child_frame_addr;

    struct interrupt_frame *child_frame = (struct interrupt_frame *)child_frame_addr;
    trap_frame_t *parent_frame = (trap_frame_t *)frame_ptr;

    memset(child_frame, 0, sizeof(struct interrupt_frame));

    // Копируем регистры родителя, чтобы child продолжил выполнение с тем же контекстом
    child_frame->rax = 0; // Child всегда получает 0 из fork()
    child_frame->rbx = parent_frame->rbx;
    child_frame->rcx = 0;
    child_frame->rdx = parent_frame->rdx;
    child_frame->rsi = parent_frame->rsi;
    child_frame->rdi = parent_frame->rdi;
    child_frame->rbp = parent_frame->rbp;
    child_frame->r8  = parent_frame->r8;
    child_frame->r9  = parent_frame->r9;
    child_frame->r10 = parent_frame->r10;
    child_frame->r11 = 0;
    child_frame->r12 = parent_frame->r12;
    child_frame->r13 = parent_frame->r13;
    child_frame->r14 = parent_frame->r14;
    child_frame->r15 = parent_frame->r15;

    child_frame->error_code = 0;
    child_frame->vector = 128;

    child_frame->rip = parent_frame->rip;
    child_frame->cs = 0x2B;
    child_frame->rflags = parent_frame->rflags | 0x202;
    child_frame->rsp = parent_frame->rsp;
    child_frame->ss = 0x23;

    child->user_rip = child_frame->rip;
    child->user_rsp = child_frame->rsp;

    enqueue_ready(child);
    process_count++;

    __asm__ volatile ("sti");
    return child->pid;
}
