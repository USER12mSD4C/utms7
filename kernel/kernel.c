#include "../drivers/vesa.h"    // ← ДОБАВИТЬ для vesa_init, vesa_set_framebuffer, print, println
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

// Структура тега Multiboot2
struct mb2_tag {
    u32 type;
    u32 size;
};

// Структура тега фреймбуфера
struct mb2_fb {
    u32 type;
    u32 size;
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u8 bpp;
    u8 fb_type;
    u16 reserved;
};

// Быстрый парсинг ТОЛЬКО фреймбуфера (без полного ski)
static void parse_framebuffer(u64 mb_info) {
    if (!mb_info) return;

    u8* ptr = (u8*)(mb_info + 8);  // пропускаем total_size + reserved

    while (1) {
        struct mb2_tag* tag = (struct mb2_tag*)ptr;
        if (tag->type == 0 || tag->size == 0) break;

        if (tag->type == 8) {
            struct mb2_fb* fb = (struct mb2_fb*)ptr;
            vesa_set_framebuffer(fb->addr, fb->width, fb->height, fb->pitch, fb->bpp);
            return;
        }

        ptr += (tag->size + 7) & ~7;
    }
}

void kernel_main(void *mb_info) {
    // 1. СНАЧАЛА парсим фреймбуфер
    parse_framebuffer((u64)mb_info);

    // 2. Инициализируем вывод (vesa или fallback на vga)
    vesa_init();

    // 3. Теперь можно писать на экран
    print_clear();
    print("hello world, UTMS7 is booting...\n");

    // 4. Остальная инициализация
    ski((u64)mb_info);

    while(1) {
        __asm__ volatile ("hlt");
    }
}
