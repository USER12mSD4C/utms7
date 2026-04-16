// shell/shell.h
#ifndef SHELL_H
#define SHELL_H

#include "../include/types.h"

#define MAX_COMMANDS 64
#define MAX_LINE_LEN 512
#define MAX_HISTORY 16

typedef struct {
    char name[32];
    int (*func)(int argc, char** argv);
    char desc[64];
} shell_command_t;

int shell_run(void);
int shell_register_command(const char* name, int (*func)(int argc, char** argv), const char* desc);
void shell_unregister_command(const char* name);
void shell_print(const char* str);
void shell_print_num(u32 num);
void shell_print_hex(u32 num);
int shell_init(void);
int shell_was_interrupted(void);
void shell_interrupt(void);
void shell_set_interrupt_handler(void (*handler)(void));

#endif
