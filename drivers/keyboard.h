#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

#define KEY_BUFFER_SIZE 256
#define KEY_UP      0xE0
#define KEY_DOWN    0xE1
#define KEY_LEFT    0xE2
#define KEY_RIGHT   0xE3
#define KEY_PGUP    0xE6
#define KEY_PGDN    0xE7

int keyboard_init(void);
u8 keyboard_getc(void);
int keyboard_data_ready(void);
void keyboard_handler_c(void);
u8 keyboard_get_scancode(void);
int keyboard_scancode_ready(void);

#endif
