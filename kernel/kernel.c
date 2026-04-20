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

void kernel_main(void *mb_info) {
    vga_init();
    vga_clear();
    vga_write("hello world, UTMS7 is booting...\n");

    ski((u64)mb_info);

    while(1) {
        __asm__ volatile ("hlt");
    }
}
