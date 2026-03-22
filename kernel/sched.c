// kernel/sched.c
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "idt.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "elf.h"

static process_t processes[MAX_PROCESSES];
static process_t *current = NULL;
static process_t *ready_queue = NULL;
static process_t *sleep_queue = NULL;
static u32 next_pid = 1;
static u32 process_count = 0;

extern void switch_to_process(process_t* prev, process_t* next);
extern void enter_userspace(u64 rip, u64 rsp, u64 cr3);

// ==================== ОЧЕРЕДИ ====================

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

// ==================== УПРАВЛЕНИЕ ПРОЦЕССАМИ ====================

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

// ==================== ПЛАНИРОВЩИК ====================

void sched_init(void) {
    memset(processes, 0, sizeof(processes));
    ready_queue = NULL;
    sleep_queue = NULL;
    current = NULL;
    next_pid = 1;
    process_count = 0;
    
    // Создаём idle-процесс
    process_t *idle = find_free_proc();
    if (!idle) return;
    
    idle->pid = alloc_pid();
    strcpy(idle->name, "idle");
    idle->state = PROC_READY;
    idle->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    idle->kstack_top = idle->kstack + KERNEL_STACK_SIZE;
    
    // У idle нет CR3 — используем ядерное
    idle->cr3 = (u64)0x1000;
    
    enqueue_ready(idle);
    current = idle;
}

void sched_tick(void) {
    if (!current) return;
    
    // Обновляем таймеры сна
    static u32 last_tick = 0;
    u32 now = get_ticks();
    
    if (now != last_tick) {
        last_tick = now;
        
        // Пробуждаем процессы
        while (sleep_queue && sleep_queue->sleep_until <= now) {
            process_t *p = sleep_queue;
            sleep_queue = sleep_queue->next;
            p->next = NULL;
            
            p->state = PROC_READY;
            enqueue_ready(p);
        }
    }
    
    // Уменьшаем квант времени
    if (current->ticks_left > 0) {
        current->ticks_left--;
    }
    
    // Если квант истёк или процесс не running (мог заснуть)
    if (current->ticks_left == 0 || current->state != PROC_RUNNING) {
        sched_yield();
    }
}

void sched_yield(void) {
    if (!current) return;
    
    // Сохраняем текущий процесс
    if (current->state == PROC_RUNNING) {
        current->state = PROC_READY;
        enqueue_ready(current);
    }
    
    // Выбираем следующий
    process_t *next = dequeue_ready();
    if (!next) {
        // Нет готовых процессов — используем idle
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state == PROC_READY) {
                next = &processes[i];
                remove_from_ready(next);
                break;
            }
        }
        if (!next) return;  // нет ни одного процесса
    }
    
    next->state = PROC_RUNNING;
    next->ticks_left = TIME_SLICE;
    
    process_t *prev = current;
    current = next;
    
    if (prev != next) {
        switch_to_process(prev, next);
    }
}

void sched_sleep(u32 ms) {
    if (!current || current->pid == 1) return;  // idle не спит
    
    u32 now = get_ticks();
    current->sleep_until = now + ms;
    current->state = PROC_SLEEPING;
    
    remove_from_ready(current);
    enqueue_sleep(current);
    
    sched_yield();
}

void sched_wakeup(u32 pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state == PROC_SLEEPING) {
            processes[i].state = PROC_READY;
            remove_from_sleep(&processes[i]);
            enqueue_ready(&processes[i]);
            return;
        }
    }
}

// ==================== СОЗДАНИЕ ПРОЦЕССОВ ====================

int sched_create_kthread(const char* name, void (*entry)(void*), void* arg) {
    process_t *p = find_free_proc();
    if (!p) return -1;
    
    p->pid = alloc_pid();
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->state = PROC_READY;
    p->ticks_left = TIME_SLICE;
    
    // Выделяем стек ядра
    p->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!p->kstack) {
        p->state = PROC_UNUSED;
        return -1;
    }
    p->kstack_top = p->kstack + KERNEL_STACK_SIZE;
    
    // Используем ядерное адресное пространство
    p->cr3 = (u64)0x1000;
    
    // Настраиваем стек для первого запуска
    u64 *stack = (u64*)p->kstack_top;
    
    // Сохраняем аргумент
    *--stack = (u64)arg;
    
    // Адрес возврата (выйти из потока)
    *--stack = (u64)sched_exit;
    
    // RIP
    *--stack = (u64)entry;
    
    // Сохраняем RSP
    p->regs.rsp = (u64)stack;
    p->regs.rip = (u64)entry;
    
    // Флаги
    p->regs.rflags = 0x202;
    
    // Сегменты (0x08 = code, 0x10 = data)
    p->regs.cs = 0x08;
    p->regs.ss = 0x10;
    
    enqueue_ready(p);
    process_count++;
    
    return p->pid;
}

int sched_create_user(const char* name, const char* elf_path, char** argv, char** envp) {
    process_t *p = find_free_proc();
    if (!p) return -1;
    
    // Загружаем ELF
    u8 *elf_data;
    u32 elf_size;
    if (ufs_read(elf_path, &elf_data, &elf_size) != 0) {
        p->state = PROC_UNUSED;
        return -1;
    }
    
    // Создаём адресное пространство
    u64* pml4 = create_address_space();
    if (!pml4) {
        kfree(elf_data);
        p->state = PROC_UNUSED;
        return -1;
    }
    p->cr3 = (u64)pml4;
    
    // Загружаем ELF в адресное пространство
    u64 entry = elf_load(elf_data, elf_size, pml4);
    kfree(elf_data);
    
    if (entry == 0) {
        free_address_space(pml4);
        p->state = PROC_UNUSED;
        return -1;
    }
    
    // Выделяем стек ядра
    p->kstack = (u64)kmalloc(KERNEL_STACK_SIZE);
    if (!p->kstack) {
        free_address_space(pml4);
        p->state = PROC_UNUSED;
        return -1;
    }
    p->kstack_top = p->kstack + KERNEL_STACK_SIZE;
    
    // Выделяем пользовательский стек (8 страниц)
    u64 ustack_base = 0x7FFFFFF000;
    for (int i = 0; i < 8; i++) {
        u64 phys = (u64)kmalloc(4096);
        paging_map_for_process(pml4, phys, ustack_base - (i+1)*4096, 
                               PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    p->ustack = ustack_base;
    p->ustack_top = ustack_base;
    
    // Настраиваем стек для пользовательского процесса
    u64 *ustack = (u64*)ustack_base;
    
    // Аргументы и окружение
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    
    // envp
    int envc = 0;
    if (envp) {
        while (envp[envc]) envc++;
    }
    
    // Считаем, сколько памяти нужно для строк
    u64 args_size = 0;
    for (int i = 0; i < argc; i++) args_size += strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++) args_size += strlen(envp[i]) + 1;
    
    // Выделяем место для строк (на стеке)
    u64 args_top = ustack_base - args_size;
    
    // Копируем строки
    char *args_ptr = (char*)args_top;
    u64 argv_ptrs[argc + 1];
    u64 envp_ptrs[envc + 1];
    
    for (int i = 0; i < argc; i++) {
        argv_ptrs[i] = (u64)args_ptr;
        strcpy(args_ptr, argv[i]);
        args_ptr += strlen(argv[i]) + 1;
    }
    argv_ptrs[argc] = 0;
    
    for (int i = 0; i < envc; i++) {
        envp_ptrs[i] = (u64)args_ptr;
        strcpy(args_ptr, envp[i]);
        args_ptr += strlen(envp[i]) + 1;
    }
    envp_ptrs[envc] = 0;
    
    // Копируем массивы указателей на стек
    u64 stack_top = ustack_base - 16;
    stack_top -= (envc + 1) * 8;
    memcpy((void*)stack_top, envp_ptrs, (envc + 1) * 8);
    
    stack_top -= (argc + 1) * 8;
    memcpy((void*)stack_top, argv_ptrs, (argc + 1) * 8);
    
    stack_top -= 8;  // для argv[0]?
    *(u64*)stack_top = (u64)stack_top + 8;
    
    // argc
    stack_top -= 8;
    *(u64*)stack_top = argc;
    
    // Адрес возврата (exit)
    stack_top -= 8;
    *(u64*)stack_top = 0;  // exit
    
    // RIP
    stack_top -= 8;
    *(u64*)stack_top = entry;
    
    p->regs.rsp = stack_top;
    p->regs.rip = entry;
    p->regs.rflags = 0x202;
    p->regs.cs = 0x1B;  // user code
    p->regs.ss = 0x23;  // user data
    
    p->pid = alloc_pid();
    p->ppid = current ? current->pid : 0;
    strncpy(p->name, name, 31);
    p->state = PROC_READY;
    p->ticks_left = TIME_SLICE;
    
    // Стандартные FD
    p->fds[0].used = 1;
    p->fds[0].type = 0;
    strcpy(p->fds[0].data.file.path, "/dev/stdin");
    
    p->fds[1].used = 1;
    p->fds[1].type = 0;
    strcpy(p->fds[1].data.file.path, "/dev/stdout");
    
    p->fds[2].used = 1;
    p->fds[2].type = 0;
    strcpy(p->fds[2].data.file.path, "/dev/stderr");
    
    enqueue_ready(p);
    process_count++;
    
    return p->pid;
}

void sched_exit(int code) {
    if (!current || current->pid == 1) return;
    
    current->exit_code = code;
    current->state = PROC_ZOMBIE;
    
    // Разбудить родителя, если ждёт
    if (current->waiting_for) {
        current->waiting_for->state = PROC_READY;
        enqueue_ready(current->waiting_for);
    }
    
    // Освобождаем ресурсы
    free_process(current);
    
    sched_yield();
}

int sched_waitpid(u32 pid, int* status) {
    process_t *target = NULL;
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state == PROC_ZOMBIE) {
            target = &processes[i];
            break;
        }
    }
    
    if (!target) {
        // Нет такого зомби — ждём
        process_t *p = current;
        p->waiting_for = NULL;
        
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == pid && processes[i].state != PROC_ZOMBIE) {
                p->waiting_for = &processes[i];
                p->state = PROC_BLOCKED;
                sched_yield();
                break;
            }
        }
        
        if (!p->waiting_for) return -1;
        
        // Проснулись — ищем зомби
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
    }
    
    if (target && status) {
        *status = target->exit_code;
    }
    
    // Окончательно удаляем
    if (target) {
        target->state = PROC_UNUSED;
        process_count--;
    }
    
    return pid;
}

void sched_kill(u32 pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_ZOMBIE) {
            processes[i].state = PROC_ZOMBIE;
            processes[i].exit_code = -1;
            free_process(&processes[i]);
            return;
        }
    }
}

process_t* sched_current(void) {
    return current;
}

u32 sched_get_pid(void) {
    return current ? current->pid : 0;
}

u32 sched_get_ppid(void) {
    return current ? current->ppid : 0;
}

int sched_get_current_pid(void) {
    if (current) return current->pid;
    return -1;
}

int sched_get_processes(process_t** buf, int max) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_UNUSED) {
            count++;
        }
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
        if (processes[i].pid == pid && processes[i].state != PROC_ZOMBIE) {
            processes[i].state = PROC_ZOMBIE;
            processes[i].exit_code = -1;
            
            // Освобождаем ресурсы
            if (processes[i].kstack) {
                kfree((void*)processes[i].kstack);
                processes[i].kstack = 0;
            }
            
            if (processes[i].cr3 && processes[i].cr3 != (u64)0x1000) {
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
    if (sig == 2) {  // SIGINT
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == pid && processes[i].state != PROC_ZOMBIE) {
                processes[i].state = PROC_ZOMBIE;
                processes[i].exit_code = -1;
                free_process(&processes[i]);
                break;
            }
        }
    }
}
