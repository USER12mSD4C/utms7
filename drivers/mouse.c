#include "mouse.h"
#include "../include/io.h"

static int mouse_present = 0;

int mouse_init(u32 width, u32 height, int is_graphic) {
    (void)width; (void)height; (void)is_graphic;
    mouse_present = 0; // Пока мышь не обнаружена
    return 0;
}

int mouse_present_check(void) {
    return mouse_present;
}
// ... остальной код
