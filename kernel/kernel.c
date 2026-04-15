#include "../drivers/vga.h"
#include "paging.h"
#include "idt.h"
#include "sched.h"
#include "syscall.h"
#include "../drivers/pci.h"
#include "../drivers/disk.h"
#include "../net/net.h"
#include "../fs/ufs.h"
#include "../include/shell_api.h"
#include "../adders/ski.h"
#include "../commands/builtin.h"
#include "../commands/fs.h"

extern void disk_commands_init(void);
extern void commands_init(void);
extern void fs_commands_init(void);
extern void shell_init(void);
extern void shell_run(void);
extern void kinit_run_all(void);

//typedef struct {
//    const char* name;
//    int (*init)(void);
//    int critical;
//} init_entry_t;

//static init_entry_t init_table[] = {
    //{"paging", paging_init, 1},
    //{"timer", timer_init, 1},
    //{"sched", sched_init, 1},
    //{"syscalls", syscall_init, 1},
    //{"PCI", pci_init, 1},
    //{"DISK_C", disk_commands_init, 0},
    //{"FS_C", fs_commands_init, 0},
    //{"SHELL_i", shell_init, 0},
    //{"commands_init", commands_init, 0},
    //{"kinit_chmodules", kinit_run_all, 0},
    //{"net", net_init, 0},
    //{"disk counter", disk_get_disk_count, 0},
    //{"UFS mount", ufs_mount, 0, 2048, 0},
    //{"shell_thread", sched_create_kthread, 0, "shell", (void(*)(void*))shell_run, NULL},
    //{"sched_start", sched_start, 1},
    //{NULL, NULL, 0}
    //};

void kernel_main(void *mb_info) {
    vga_init();
    vga_clear();
    vga_write("hello world, UTMS7 is booting...");

    ski((u64)mb_info);

    while(1) {
        __asm__ volatile ("hlt");
    }
}
