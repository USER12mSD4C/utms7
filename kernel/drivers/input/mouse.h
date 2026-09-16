#ifndef MOUSE_H
#define MOUSE_H

#include "../include/types.h"

#define MOUSE_LEFT_BUTTON   1
#define MOUSE_RIGHT_BUTTON  2
#define MOUSE_MIDDLE_BUTTON 4

typedef struct {
    i8 x_delta;
    i8 y_delta;
    i8 z_delta;
    u8 buttons;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
} mouse_state_t;

int mouse_init(u32 width, u32 height, int is_graphic);
void mouse_handler_c(void);
mouse_state_t* mouse_get_state(void);
void mouse_set_position(u32 x, u32 y);
void mouse_set_limits(u32 width, u32 height);
int mouse_present_check(void);
int mouse_get_buttons(void);
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_dx(void);
int mouse_get_dy(void);

#endif
