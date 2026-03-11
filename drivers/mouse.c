#include "mouse.h"
#include "../include/io.h"

static mouse_state_t mouse_state;
static int mouse_cycle = 0;
static u8 mouse_byte[3];
static int mouse_present = 0;
static u32 current_width = 80;
static u32 current_height = 25;
static int graphics_mode = 0;

static int mouse_wait(u8 type) {
    int timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (!(inb(0x64) & 0x02)) return 0;
        }
    } else {
        while (timeout--) {
            if (inb(0x64) & 0x01) return 0;
        }
    }
    return -1;
}

static void mouse_write(u8 data) {
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, data);
}

static u8 mouse_read(void) {
    mouse_wait(1);
    return inb(0x60);
}

int mouse_init(u32 width, u32 height, int is_graphic) {
    graphics_mode = is_graphic;
    current_width = width;
    current_height = height;
    
    if (graphics_mode) {
        mouse_state.x = width / 2;
        mouse_state.y = height / 2;
    } else {
        mouse_state.x = (width * 8) / 2;
        mouse_state.y = (height * 16) / 2;
    }
    
    mouse_state.buttons = 0;
    mouse_state.x_delta = 0;
    mouse_state.y_delta = 0;
    mouse_state.z_delta = 0;
    mouse_state.width = width;
    mouse_state.height = height;
    
    outb(0x64, 0xA8);
    for (int i = 0; i < 1000; i++) asm volatile ("pause");
    
    outb(0x64, 0x20);
    u8 status = inb(0x60);
    status |= 0x02;
    outb(0x64, 0x60);
    outb(0x60, status);
    
    mouse_write(0xFF);
    u8 response = mouse_read();
    if (response != 0xFA) {
        mouse_present = 0;
        return -1;
    }
    
    mouse_read();
    
    mouse_write(0xF4);
    response = mouse_read();
    if (response != 0xFA) {
        mouse_present = 0;
        return -1;
    }
    
    mouse_present = 1;
    return 0;
}

void mouse_handler_c(void) {
    if (!mouse_present) {
        outb(0x20, 0x20);
        outb(0xA0, 0x20);
        return;
    }
    
    u8 status = inb(0x64);
    if (!(status & 0x20) || !(status & 1)) {
        outb(0x20, 0x20);
        outb(0xA0, 0x20);
        return;
    }
    
    u8 data = inb(0x60);
    
    switch(mouse_cycle) {
        case 0:
            if (!(data & 0x08)) {
                mouse_cycle = 0;
                break;
            }
            mouse_byte[0] = data;
            mouse_cycle = 1;
            break;
            
        case 1:
            mouse_byte[1] = data;
            mouse_cycle = 2;
            break;
            
        case 2:
            mouse_byte[2] = data;
            mouse_cycle = 0;
            
            mouse_state.buttons = mouse_byte[0] & 0x07;
            
            i16 x_move = mouse_byte[1];
            i16 y_move = mouse_byte[2];
            
            if (mouse_byte[0] & 0x10) x_move = x_move - 256;
            if (mouse_byte[0] & 0x20) y_move = y_move - 256;
            
            mouse_state.x_delta = x_move;
            mouse_state.y_delta = -y_move;
            
            i32 new_x = mouse_state.x + mouse_state.x_delta;
            i32 new_y = mouse_state.y + mouse_state.y_delta;
            
            if (graphics_mode) {
                if (new_x < 0) new_x = 0;
                if (new_x >= (i32)current_width) new_x = current_width - 1;
                if (new_y < 0) new_y = 0;
                if (new_y >= (i32)current_height) new_y = current_height - 1;
            } else {
                if (new_x < 0) new_x = 0;
                if (new_x >= (i32)(current_width * 8)) new_x = (current_width * 8) - 1;
                if (new_y < 0) new_y = 0;
                if (new_y >= (i32)(current_height * 16)) new_y = (current_height * 16) - 1;
            }
            
            mouse_state.x = new_x;
            mouse_state.y = new_y;
            
            break;
    }
    
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

mouse_state_t* mouse_get_state(void) {
    return &mouse_state;
}

void mouse_set_position(u32 x, u32 y) {
    mouse_state.x = x;
    mouse_state.y = y;
}

void mouse_set_limits(u32 width, u32 height) {
    current_width = width;
    current_height = height;
    mouse_state.width = width;
    mouse_state.height = height;
}

int mouse_present_check(void) {
    return mouse_present;
}

int mouse_get_buttons(void) {
    return mouse_state.buttons;
}

int mouse_get_x(void) {
    return mouse_state.x;
}

int mouse_get_y(void) {
    return mouse_state.y;
}

int mouse_get_dx(void) {
    return mouse_state.x_delta;
}

int mouse_get_dy(void) {
    return mouse_state.y_delta;
}

static const char __mouse_name[] __attribute__((section(".module_name"))) = "mouse";
static int (*__mouse_entry)(u32, u32, int) __attribute__((section(".module_entry"))) = mouse_init;
