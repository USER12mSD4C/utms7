#ifndef SHELL_API_H
#define SHELL_API_H

#include "types.h"

#define CMD_MAX_NAME 32
#define CMD_MAX_DESC 64
#define CMD_MAX_ARGS 16

typedef struct {
    char name[CMD_MAX_NAME];
    int (*func)(int argc, char** argv);
    char desc[CMD_MAX_DESC];
} shell_command_t;

// Инициализация shell
void shell_init(void);

// Регистрация команд
int shell_register_command(const char* name, int (*func)(int argc, char** argv), const char* desc);
int shell_unregister_command(const char* name);

// Выполнение команды
int shell_execute(const char* cmd_line);

// Запуск основного цикла
void shell_run(void);

// Вспомогательные функции для команд
char** shell_split_args(char* str, int* argc);
void shell_print(const char* str);
void shell_print_num(u32 num);
void shell_print_hex(u32 num);

#endif
