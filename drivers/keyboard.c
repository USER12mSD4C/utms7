#include "keyboard.h"
#include "../include/io.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static u8 key_buffer[256];
static int buffer_head = 0;
static int buffer_tail = 0;

static const u8 normal_keys[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' '
};

static const u8 shift_keys[] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' '
};

static u8 shift_pressed = 0;
static u8 extended = 0;
static u8 ctrl_pressed = 0;
static u8 alt_pressed = 0;

int keyboard_init(void) {
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = 0;
    extended = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    return 0;
}

void keyboard_handler_c(void) {
    u8 status = inb(KEYBOARD_STATUS_PORT);
    
    if (status & 1) {
        u8 scancode = inb(KEYBOARD_DATA_PORT);
        
        if (scancode == 0xE0) {
            extended = 1;
            return;
        }
        
        if (scancode & 0x80) {
            u8 release = scancode & 0x7F;
            if (release == 0x2A || release == 0x36) shift_pressed = 0;
            if (release == 0x1D) ctrl_pressed = 0;
            if (release == 0x38) alt_pressed = 0;
            extended = 0;
            return;
        }
        
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
        }
        else if (scancode == 0x1D) {
            ctrl_pressed = 1;
        }
        else if (scancode == 0x38) {
            alt_pressed = 1;
        }
        else if (extended) {
            u8 special = 0;
            switch(scancode) {
                case 0x48: special = KEY_UP; break;
                case 0x50: special = KEY_DOWN; break;
                case 0x4B: special = KEY_LEFT; break;
                case 0x4D: special = KEY_RIGHT; break;
                case 0x49: special = KEY_PGUP; break;
                case 0x51: special = KEY_PGDN; break;
                case 0x47: special = KEY_HOME; break;
                case 0x4F: special = KEY_END; break;
                case 0x52: special = KEY_INSERT; break;
                case 0x53: special = KEY_DELETE; break;
                case 0x3B: special = KEY_F1; break;
                case 0x3C: special = KEY_F2; break;
                case 0x3D: special = KEY_F3; break;
                case 0x3E: special = KEY_F4; break;
                case 0x3F: special = KEY_F5; break;
                case 0x40: special = KEY_F6; break;
                case 0x41: special = KEY_F7; break;
                case 0x42: special = KEY_F8; break;
                case 0x43: special = KEY_F9; break;
                case 0x44: special = KEY_F10; break;
                case 0x57: special = KEY_F11; break;
                case 0x58: special = KEY_F12; break;
            }
            
            if (special) {
                int next = (buffer_head + 1) % 256;
                if (next != buffer_tail) {
                    key_buffer[buffer_head] = special;
                    buffer_head = next;
                }
            }
            extended = 0;
        }
        else {
            u8 ascii = 0;
            
            // Ctrl+буква
            if (ctrl_pressed && scancode >= 0x1E && scancode <= 0x26) {
                ascii = scancode - 0x1E + 1;
            }
            else if (scancode < sizeof(normal_keys)) {
                if (shift_pressed) {
                    ascii = shift_keys[scancode];
                } else {
                    ascii = normal_keys[scancode];
                }
            }
            
            if (ascii) {
                int next = (buffer_head + 1) % 256;
                if (next != buffer_tail) {
                    key_buffer[buffer_head] = ascii;
                    buffer_head = next;
                }
            }
        }
    }
}

int keyboard_data_ready(void) {
    return buffer_head != buffer_tail;
}

u8 keyboard_getc(void) {
    while (buffer_head == buffer_tail) {
        __asm__ volatile ("nop");
    }
    
    u8 c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % 256;
    return c;
}

u8 keyboard_get_scancode(void) {
    if (inb(KEYBOARD_STATUS_PORT) & 1) {
        return inb(KEYBOARD_DATA_PORT);
    }
    return 0;
}

int keyboard_scancode_ready(void) {
    return inb(KEYBOARD_STATUS_PORT) & 1;
}

// Для автоматической регистрации в kinit
static const char __kbd_name[] __attribute__((section(".kinit.modules"))) = "keyboard_init";
static void* __kbd_func __attribute__((section(".kinit.modules"))) = keyboard_init;
