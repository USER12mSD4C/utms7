// apps/init.c
#include "../lib/libc.h"

int main(void) {
    printf("[INIT]: Userspace init started (PID %d)\n", getpid());
    printf("[INIT]: starting UFS...");
    fs_register("ufs");
    set_color(0x0A, 0x00);
    printf("OK\n");
    set_color(0x07, 0x00);

    while (1) {
        printf("[INIT]: Spawning userspace shell...");
        int pid = fork();
        if (pid == 0) {
            char *argv[] = { "/bin/sfsh", NULL };
            execve("/bin/sfsh", argv, NULL);
            set_color(0x0C, 0x00);
            printf("Failed to execute /bin/sfsh\n");
            _exit(-1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            set_color(0x0C, 0x00);
            printf("Shell exited with status %d, restarting in 2 seconds...\n", status);
            sleep(2);
        } else {
            set_color(0x0C, 0x00);
            printf("Fork failed, retrying in 5 seconds...\n");
            sleep(5);
        }
    }
    return 0;
}
