// kernel/kernel.c
#include "../drivers/drm.h"
#include "../adders/ski.h"

void kernel_main(void *mb_info) {
    drm_parse_multiboot((u64)mb_info);
    drm_init();

    print_clear();
    print("hello world, UTMS7 is booting...\n");

    ski((u64)mb_info);

    while(1) __asm__("hlt");
}
