#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

#define KEY_BUFFER_SIZE 128

#define KEY_UP      0xE0
#define KEY_DOWN    0xE1
#define KEY_LEFT    0xE2
#define KEY_RIGHT   0xE3
#define KEY_HOME    0xE4
#define KEY_END     0xE5
#define KEY_PGUP    0xE6
#define KEY_PGDN    0xE7
#define KEY_INSERT  0xE8
#define KEY_DELETE  0xE9
#define KEY_F1      0xEA
#define KEY_F2      0xEB
#define KEY_F3      0xEC
#define KEY_F4      0xED
#define KEY_F5      0xEE
#define KEY_F6      0xEF
#define KEY_F7      0xF0
#define KEY_F8      0xF1
#define KEY_F9      0xF2
#define KEY_F10     0xF3
#define KEY_F11     0xF4
#define KEY_F12     0xF5

int keyboard_init(void);
u8 keyboard_getc(void);
int keyboard_data_ready(void);
void keyboard_handler_c(void);
u8 keyboard_get_scancode(void);
int keyboard_scancode_ready(void);

#endif
