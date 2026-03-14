#include "../include/shell_api.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/vesa.h"
#include "../kernel/kapi.h"
#include "../kernel/memory.h"
#include "../apps/uwr.h"

// Встроенные команды

static int cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print("\nAvailable commands:\n");
    // TODO: вывести список всех команд
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
    shell_print("Memory used: ");
    shell_print_num(kapi_memory_used());
    shell_print(" bytes\n");
    shell_print("Memory free: ");
    shell_print_num(kapi_memory_free());
    shell_print(" bytes\n");
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
    shell_print("System ticks: ");
    shell_print_num(kapi_get_ticks());
    shell_print("\n");
    return 0;
}

static int cmd_uwr(int argc, char** argv) {
    const char* filename = NULL;
    int use_vesa = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vesa") == 0 || strcmp(argv[i], "-v") == 0) {
            use_vesa = 1;
        } else {
            filename = argv[i];
        }
    }
    
    uwr_main(filename, use_vesa);
    return 0;
}

void commands_init(void) {
    shell_register_command("help", cmd_help, "Show this help");
    shell_register_command("clear", cmd_clear, "Clear screen");
    shell_register_command("mem", cmd_mem, "Show memory usage");
    shell_register_command("echo", cmd_echo, "Echo arguments");
    shell_register_command("ticks", cmd_ticks, "Show system ticks");
    shell_register_command("uwr", cmd_uwr, "UWR text editor [--vesa]");
}
