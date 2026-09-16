#include "../lib/libc.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: chroot <new-root> [command]\n");
        return 1;
    }

    if (chroot(argv[1]) != 0) {
        perror("chroot");
        return 1;
    }

    if (chdir("/") != 0) {
        perror("chdir");
        return 1;
    }

    if (argc > 2) {
        execvp(argv[2], &argv[2]);
        perror("execvp");
        return 1;
    } else {
        char* sh_args[] = {"/bin/sfsh", NULL};
        execvp("/bin/sfsh", sh_args);
        perror("execvp sfsh");
        return 1;
    }
}
