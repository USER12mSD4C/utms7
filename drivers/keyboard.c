// drivers/keyboard.c
#include "keyboard.h"
#include "../include/io.h"
#include "../kernel/idt.h"

extern void sched_yield(void);

#define KEYBOARD_DATA   0x60
#define BUFFER_SIZE     128

static volatile u8 kbd_buffer[BUFFER_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;
static volatile int kbd_shift = 0;
static volatile int kbd_caps = 0;

static const u8 sc_ascii[] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0
};

static const u8 sc_ascii_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*',0,' ',0
};

void keyboard_handler(void) {
    u8 sc = inb(KEYBOARD_DATA);

    // Key release events
    if (sc == 0xAA || sc == 0xB6) {  // Shift release
        kbd_shift = 0;
        return;
    }

    // Key press events
    if (sc & 0x80) return;

    if (sc == 0x2A || sc == 0x36) {  // Shift press
        kbd_shift = 1;
        return;
    }

    if (sc == 0x3A) {  // Caps Lock press
        kbd_caps = !kbd_caps;
        return;
    }

    if (sc < 58) {
        u8 c = kbd_shift ? sc_ascii_shift[sc] : sc_ascii[sc];

        // Apply Caps Lock (toggle case for letters)
        if (kbd_caps && !kbd_shift && c >= 'a' && c <= 'z') {
            c -= 32;  // lowercase to uppercase
        } else if (kbd_caps && kbd_shift && c >= 'A' && c <= 'Z') {
            c += 32;  // uppercase to lowercase (Caps + Shift = lowercase)
        }

        if (c) {
            int next = (kbd_head + 1) % BUFFER_SIZE;
            if (next != kbd_tail) {
                kbd_buffer[kbd_head] = c;
                kbd_head = next;
            }
        }
    }
}

int keyboard_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    kbd_shift = 0;
    kbd_caps = 0;

    while (inb(0x64) & 0x02) { __asm__ volatile ("pause"); }
    outb(0x60, 0xF4);

    int t = 100000;
    while (t--) {
        if (inb(0x64) & 1) {
            u8 ack = inb(0x60);
            (void)ack;
            break;
        }
    }

    idt_register_irq(1, keyboard_handler);
    return 0;
}

int keyboard_data_ready(void) {
    return kbd_head != kbd_tail;
}

u8 keyboard_getc(void) {
    while (kbd_head == kbd_tail) {
        sched_yield();
    }
    u8 c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % BUFFER_SIZE;
    return c;
}

int keyboard_get_modifiers(void) { return kbd_shift | (kbd_caps << 1); }
