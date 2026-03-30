// drivers/keyboard.c
#include "keyboard.h"
#include "../include/io.h"
#include "../kernel/sched.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEY_BUFFER_SIZE 128

static u8 key_buffer[KEY_BUFFER_SIZE];
static int buffer_head = 0;
static int buffer_tail = 0;

static const u8 normal_keys[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0x08,
    0x09,'q','w','e','r','t','y','u','i','o','p','[',']',0x0A,
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

static const u8 shift_keys[] = {
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0x08,
    0x09,'Q','W','E','R','T','Y','U','I','O','P','{','}',0x0A,
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

static u8 shift = 0;
static u8 ctrl = 0;
static u8 alt = 0;
static u8 extended = 0;

int keyboard_init(void) {
    buffer_head = buffer_tail = 0;
    shift = ctrl = alt = extended = 0;
    
    outb(0x64, 0xAD);
    for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
    outb(0x64, 0xAE);
    for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
    outb(KEYBOARD_DATA_PORT, 0xFF);
    for (int i = 0; i < 10000; i++) __asm__ volatile ("pause");
    outb(KEYBOARD_DATA_PORT, 0xF4);
    
    return 0;
}

int keyboard_get_modifiers(void) {
    int mods = 0;
    if (shift) mods |= KEY_MOD_SHIFT;
    if (ctrl) mods |= KEY_MOD_CTRL;
    if (alt) mods |= KEY_MOD_ALT;
    return mods;
}

void keyboard_handler_c(void) {
    u8 status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 1)) return;
    
    u8 scancode = inb(KEYBOARD_DATA_PORT);
    
    if (scancode == 0xE0) {
        extended = 1;
        return;
    }
    
    if (scancode & 0x80) {
        u8 release = scancode & 0x7F;
        if (release == 0x2A || release == 0x36) shift = 0;
        if (release == 0x1D) ctrl = 0;
        if (release == 0x38) alt = 0;
        extended = 0;
        return;
    }
    
    if (scancode == 0x2A || scancode == 0x36) {
        shift = 1;
        extended = 0;
        return;
    }
    if (scancode == 0x1D) {
        ctrl = 1;
        extended = 0;
        return;
    }
    if (scancode == 0x38) {
        alt = 1;
        extended = 0;
        return;
    }
    
    u8 ascii = 0;
    
    if (extended) {
        switch(scancode) {
            case 0x48: ascii = KEY_UP; break;
            case 0x50: ascii = KEY_DOWN; break;
            case 0x4B: ascii = KEY_LEFT; break;
            case 0x4D: ascii = KEY_RIGHT; break;
            case 0x47: ascii = KEY_HOME; break;
            case 0x4F: ascii = KEY_END; break;
            case 0x49: ascii = KEY_PGUP; break;
            case 0x51: ascii = KEY_PGDN; break;
            case 0x52: ascii = KEY_INSERT; break;
            case 0x53: ascii = KEY_DELETE; break;
            default: ascii = 0;
        }
        extended = 0;
    } else {
        if (scancode < sizeof(normal_keys)) {
            ascii = shift ? shift_keys[scancode] : normal_keys[scancode];
            
            if (ctrl && ascii >= 'a' && ascii <= 'z') {
                ascii = ascii - 'a' + 1;
            } else if (ctrl && ascii >= 'A' && ascii <= 'Z') {
                ascii = ascii - 'A' + 1;
            }
        }
    }
    
    if (ascii) {
        int next = (buffer_head + 1) % KEY_BUFFER_SIZE;
        if (next != buffer_tail) {
            key_buffer[buffer_head] = ascii;
            buffer_head = next;
        }
    }
}

int keyboard_data_ready(void) {
    return buffer_head != buffer_tail;
}

u8 keyboard_getc(void) {
    while (buffer_head == buffer_tail) {
        __asm__ volatile ("pause");
    }
    u8 c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

static const char __keyboard_name[] __attribute__((section(".module_name"))) = "keyboard";
static int (*__keyboard_entry)(void) __attribute__((section(".module_entry"))) = keyboard_init;
