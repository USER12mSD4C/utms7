#include "../include/shell_api.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/vesa.h"
#include "../kernel/kapi.h"
#include "../kernel/memory.h"
#include "../kernel/sched.h"
#include "../apps/uwr.h"

static int cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    
    shell_command_t *commands = NULL;
    int cmd_count = 0;
    
    shell_print("\nUTMS Shell Commands\n");
    shell_print("==================\n\n");
    
    shell_print("  help      - show help\n");
    shell_print("  clear     - clear screen\n");
    shell_print("  mem       - show memory\n");
    shell_print("  echo      - echo args\n");
    shell_print("  ticks     - system ticks\n");
    shell_print("  uwr       - text editor\n");
    shell_print("  ps        - list processes\n");
    shell_print("  kill      - kill process\n");
    shell_print("  ls        - list directory\n");
    shell_print("  cd        - change directory\n");
    shell_print("  pwd       - print working directory\n");
    shell_print("  mkdir     - create directory\n");
    shell_print("  mk        - create file\n");
    shell_print("  rm        - remove file/dir\n");
    shell_print("  cat       - show file\n");
    shell_print("  df        - fs usage\n");
    shell_print("  mkfs.ufs  - format disk\n");
    shell_print("  mount     - mount ufs\n");
    shell_print("  umount    - unmount ufs\n");
    shell_print("  disks     - list all disks\n");
    shell_print("  lsblk     - list block devices\n");
    shell_print("  udisk     - partition manager\n");
    
    return 0;
}

static int cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_clear();
    if (vesa_is_active()) vesa_clear(0, 0, 0);
    return 0;
}

static int cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print("used: ");
    shell_print_num(kapi_memory_used());
    shell_print(" free: ");
    shell_print_num(kapi_memory_free());
    shell_print("\n");
    return 0;
}

static int cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        shell_print(argv[i]);
        if (i < argc - 1) shell_print(" ");
    }
    shell_print("\n");
    return 0;
}

static int cmd_ticks(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print_num(kapi_get_ticks());
    shell_print("\n");
    return 0;
}

static int cmd_uwr(int argc, char** argv) {
    const char* filename = NULL;
    int use_vesa = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            use_vesa = 1;
        } else {
            filename = argv[i];
        }
    }
    
    uwr_main(filename, use_vesa);
    return 0;
}

static int cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    
    int count = sched_get_processes(NULL, 0);
    if (count == 0) {
        shell_print("no processes\n");
        return 0;
    }
    
    process_t* procs[count];
    sched_get_processes(procs, count);
    
    shell_print("PID  PPID  STATE  NAME\n");
    for (int i = 0; i < count; i++) {
        shell_print_num(procs[i]->pid);
        shell_print("   ");
        shell_print_num(procs[i]->ppid);
        shell_print("   ");
        
        switch(procs[i]->state) {
            case PROCESS_READY: shell_print("R     "); break;
            case PROCESS_RUNNING: shell_print("RUN   "); break;
            case PROCESS_WAITING: shell_print("W     "); break;
            case PROCESS_ZOMBIE: shell_print("Z     "); break;
            default: shell_print("?     "); break;
        }
        
        shell_print(procs[i]->name);
        shell_print("\n");
    }
    return 0;
}

static int cmd_kill(int argc, char** argv) {
    if (argc < 2) {
        shell_print("usage: kill <pid>\n");
        return -1;
    }
    
    int pid = 0;
    char* p = argv[1];
    while (*p) {
        if (*p < '0' || *p > '9') {
            shell_print("invalid pid\n");
            return -1;
        }
        pid = pid * 10 + (*p - '0');
        p++;
    }
    
    if (sched_kill(pid) == 0) {
        shell_print("killed ");
        shell_print_num(pid);
        shell_print("\n");
        return 0;
    } else {
        shell_print("kill failed\n");
        return -1;
    }
}

void commands_init(void) {
    shell_register_command("help", cmd_help, "show help");
    shell_register_command("clear", cmd_clear, "clear screen");
    shell_register_command("mem", cmd_mem, "show memory");
    shell_register_command("echo", cmd_echo, "echo args");
    shell_register_command("ticks", cmd_ticks, "system ticks");
    shell_register_command("uwr", cmd_uwr, "text editor");
    shell_register_command("ps", cmd_ps, "list processes");
    shell_register_command("kill", cmd_kill, "kill process");
}
