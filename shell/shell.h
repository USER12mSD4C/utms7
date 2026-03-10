#ifndef SHELL_H
#define SHELL_H

// структура команды
typedef struct {
    char name[32];
    void (*func)(int argc, char** argv);
    char desc[64];
} shell_command_t;

void shell_run(void);
int shell_register_command(const char* name, void (*func)(int argc, char** argv), const char* desc);
void shell_unregister_command(const char* name);

#endif
