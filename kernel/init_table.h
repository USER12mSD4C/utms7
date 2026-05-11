// kernel/init_table.h
// Формат: X(имя, функция, критичность, аргументы...)

// === ЭТАП 1: Ядро ===
X("paging",     paging_init,     1)
X("timer",      timer_init,      1)
X("sched",      sched_init,      1)
X("syscalls",   syscall_init,    1)

// === ЭТАП 2: Оборудование ===
X("PCI",        pci_init,        1)
X("disk",       disk_init,       1)
X("keyboard",   keyboard_init,   1)
X("net",        net_init,        0)

// === ЭТАП 3: Файловая система ===
X("UFS mount",  ufs_mount,       0, 2048, 0)

// === ЭТАП 4: Команды и оболочка ===
X("disk_commands", disk_commands_init, 0)
X("fs_commands",   fs_commands_init,   0)
X("shell_init",    shell_init,        0)
X("commands_init", commands_init,     0)

// === ЭТАП 5: Модули ===
X("kinit",      kinit_run_all,   0)

// === ЭТАП 6: Поток оболочки (после всего) ===
// Используем функцию-обертку, которая всегда возвращает 0
X("shell thread", shell_start_thread, 0)

// === ЭТАП 7: Запуск планировщика ===
X("sched_start",  sched_start,   1)
