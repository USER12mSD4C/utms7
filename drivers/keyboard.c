// drivers/keyboard.c
#include "keyboard.h"
#include "../include/io.h"
#include "../kernel/idt.h"

#define KEYBOARD_DATA   0x60
#define BUFFER_SIZE     128

static volatile u8 kbd_buffer[BUFFER_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;

// Самая простая таблица скан-кодов
static const u8 sc_ascii[] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=',0,
    0, 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0
};

void keyboard_handler(void) {
    u8 sc = inb(KEYBOARD_DATA);

    if (sc & 0x80) return;  // отпускание - игнорируем

    if (sc < 58) {
        u8 c = sc_ascii[sc];
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

    // Ждем готовности клавиатуры
    while (inb(0x64) & 0x02) { __asm__ volatile ("pause"); }

    // Включаем клавиатуру
    outb(0x60, 0xF4);

    // Ждем ACK
    int t = 100000;
    while (t--) {
        if (inb(0x64) & 1) {
            u8 ack = inb(0x60);
            (void)ack;
            break;
        }
    }

    // Регистрируем обработчик
    idt_register_irq(1, keyboard_handler);

    return 0;
}

int keyboard_data_ready(void) {
    return kbd_head != kbd_tail;
}

u8 keyboard_getc(void) {
    while (kbd_head == kbd_tail) {
        __asm__ volatile ("sti; hlt; cli");
    }
    u8 c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % BUFFER_SIZE;
    return c;
}

int keyboard_get_modifiers(void) { return 0; }
