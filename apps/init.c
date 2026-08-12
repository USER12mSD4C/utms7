// apps/init.c
#include "../lib/libc.h"

int main(void) {
    printf("INIT: Userspace init started (PID %d)\n", getpid());

    while (1) {
        printf("INIT: Spawning userspace shell...\n");
        int pid = fork();
        if (pid == 0) {
            char *argv[] = { "/bin/sh", NULL };
            execve("/bin/sh", argv, NULL);

            printf("INIT: Failed to execute /bin/sh\n");
            _exit(-1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            printf("INIT: Shell exited with status %d, restarting in 2 seconds...\n", status);
            sleep(2);
        } else {
            printf("INIT: Fork failed, retrying in 5 seconds...\n");
            sleep(5);
        }
    }
    return 0;
}
