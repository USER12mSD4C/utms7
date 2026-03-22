// commands/builtin.c
#include "../include/shell_api.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/vesa.h"
#include "../kernel/memory.h"
#include "../kernel/sched.h"
#include "../net/net.h"
#include "../net/dns.h"
#include "../net/ip.h"
#include "../net/arp.h"
#include "../fs/ufs.h"
#include "../include/syscall.h"

extern int upac_main(int, char**);
extern void rtl8139_dump_regs(void);
extern int arp_cache_dump(char *buf, int max_len);
extern int icmp_ping(u32 dst_ip, int timeout_ms);
extern int http_get(const char* url, u8** data, u32* size);
extern int icmp_ping_received;

static int cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print("\nUTMS Shell Commands\n");
    shell_print("==================\n\n");
    shell_print("  help      - show help\n");
    shell_print("  clear     - clear screen\n");
    shell_print("  mem       - show memory\n");
    shell_print("  echo      - echo args\n");
    shell_print("  ticks     - system ticks\n");
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
    shell_print("  upac      - package manager\n");
    shell_print("  ping      - ping a host\n");
    shell_print("  ifconfig  - show network config\n");
    shell_print("  route     - show routing table\n");
    shell_print("  arp       - show ARP cache\n");
    shell_print("  nicinfo   - show NIC registers\n");
    shell_print("  dns       - resolve hostname\n");
    shell_print("  nslookup  - DNS lookup\n");
    shell_print("  wget      - download file\n");
    shell_print("  time      - show uptime\n");
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
    shell_print_num(memory_used());
    shell_print(" free: ");
    shell_print_num(memory_free());
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
    extern u32 system_ticks;
    shell_print_num(system_ticks);
    shell_print("\n");
    return 0;
}

static int cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    // Получаем количество процессов
    int count = sched_get_processes(NULL, 0);
    if (count == 0) {
        shell_print("no processes\n");
        return 0;
    }
    
    // Выделяем массив указателей
    process_t* procs[count];
    sched_get_processes(procs, count);
    
    shell_print("PID  PPID  STATE  NAME\n");
    for (int i = 0; i < count; i++) {
        shell_print_num(procs[i]->pid);
        shell_print("   ");
        shell_print_num(procs[i]->ppid);
        shell_print("   ");
        
        // Используем правильные константы из sched.h
        switch(procs[i]->state) {
            case PROC_READY: shell_print("R     "); break;
            case PROC_RUNNING: shell_print("RUN   "); break;
            case PROC_SLEEPING: shell_print("S     "); break;
            case PROC_ZOMBIE: shell_print("Z     "); break;
            case PROC_BLOCKED: shell_print("B     "); break;
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
    
    // sched_kill возвращает int, проверяем
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

static int cmd_upac(int argc, char** argv) {
    return upac_main(argc, argv);
}

static int cmd_ping(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: ping <hostname> or <ip>\n");
        return -1;
    }
    
    u32 ip = 0;
    int a, b, c, d;
    
    if (sscanf(argv[1], "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        ip = (a << 24) | (b << 16) | (c << 8) | d;
    } else {
        shell_print("Resolving ");
        shell_print(argv[1]);
        shell_print("... ");
        ip = dns_lookup(argv[1], net_get_dns());
        if (ip == 0) {
            shell_print("FAILED\n");
            return -1;
        }
        shell_print("OK\n");
    }
    
    shell_print("PING ");
    shell_print(argv[1]);
    shell_print(" (");
    shell_print_num((ip >> 24) & 0xFF);
    shell_print("."); shell_print_num((ip >> 16) & 0xFF);
    shell_print("."); shell_print_num((ip >> 8) & 0xFF);
    shell_print("."); shell_print_num(ip & 0xFF);
    shell_print("): 56 data bytes\n");
    
    for (int i = 0; i < 4; i++) {
        if (icmp_ping(ip, 1000) == 0) {
            shell_print("  64 bytes from ");
            shell_print_num((ip >> 24) & 0xFF);
            shell_print("."); shell_print_num((ip >> 16) & 0xFF);
            shell_print("."); shell_print_num((ip >> 8) & 0xFF);
            shell_print("."); shell_print_num(ip & 0xFF);
            shell_print(": icmp_seq=");
            shell_print_num(i);
            shell_print("\n");
        } else {
            shell_print("  Request timeout for icmp_seq=");
            shell_print_num(i);
            shell_print("\n");
        }
    }
    return 0;
}

static int cmd_ifconfig(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print("Network configuration:\n");
    shell_print("  MAC: ");
    u8* mac = net_get_mac();
    for (int i = 0; i < 6; i++) {
        shell_print_hex(mac[i]);
        if (i < 5) shell_print(":");
    }
    shell_print("\n");
    
    u32 ip = net_get_ip();
    shell_print("  IP:  ");
    shell_print_num((ip >> 24) & 0xFF);
    shell_print("."); shell_print_num((ip >> 16) & 0xFF);
    shell_print("."); shell_print_num((ip >> 8) & 0xFF);
    shell_print("."); shell_print_num(ip & 0xFF);
    shell_print("\n");
    
    u32 gw = net_get_gateway();
    shell_print("  GW:  ");
    shell_print_num((gw >> 24) & 0xFF);
    shell_print("."); shell_print_num((gw >> 16) & 0xFF);
    shell_print("."); shell_print_num((gw >> 8) & 0xFF);
    shell_print("."); shell_print_num(gw & 0xFF);
    shell_print("\n");
    
    u32 mask = net_get_netmask();
    shell_print("  MASK: ");
    shell_print_num((mask >> 24) & 0xFF);
    shell_print("."); shell_print_num((mask >> 16) & 0xFF);
    shell_print("."); shell_print_num((mask >> 8) & 0xFF);
    shell_print("."); shell_print_num(mask & 0xFF);
    shell_print("\n");
    
    u32 dns = net_get_dns();
    shell_print("  DNS: ");
    shell_print_num((dns >> 24) & 0xFF);
    shell_print("."); shell_print_num((dns >> 16) & 0xFF);
    shell_print("."); shell_print_num((dns >> 8) & 0xFF);
    shell_print("."); shell_print_num(dns & 0xFF);
    shell_print("\n");
    
    return 0;
}

static int cmd_route(int argc, char** argv) {
    (void)argc; (void)argv;
    u32 ip = net_get_ip();
    u32 gw = net_get_gateway();
    u32 mask = net_get_netmask();
    u32 network = ip & mask;
    
    shell_print("Kernel IP routing table\n");
    shell_print("Destination     Gateway         Genmask         Iface\n");
    
    shell_print_num((network >> 24) & 0xFF);
    shell_print("."); shell_print_num((network >> 16) & 0xFF);
    shell_print("."); shell_print_num((network >> 8) & 0xFF);
    shell_print("."); shell_print_num(network & 0xFF);
    shell_print("    0.0.0.0         ");
    shell_print_num((mask >> 24) & 0xFF);
    shell_print("."); shell_print_num((mask >> 16) & 0xFF);
    shell_print("."); shell_print_num((mask >> 8) & 0xFF);
    shell_print("."); shell_print_num(mask & 0xFF);
    shell_print("     eth0\n");
    
    shell_print("0.0.0.0         ");
    shell_print_num((gw >> 24) & 0xFF);
    shell_print("."); shell_print_num((gw >> 16) & 0xFF);
    shell_print("."); shell_print_num((gw >> 8) & 0xFF);
    shell_print("."); shell_print_num(gw & 0xFF);
    shell_print("     0.0.0.0         eth0\n");
    
    return 0;
}

static int cmd_arp(int argc, char** argv) {
    (void)argc; (void)argv;
    char buf[512];
    int len = arp_cache_dump(buf, sizeof(buf));
    if (len > 0) {
        shell_print(buf);
    } else {
        shell_print("ARP cache is empty\n");
    }
    return 0;
}

static int cmd_nicinfo(int argc, char** argv) {
    (void)argc; (void)argv;
    rtl8139_dump_regs();
    return 0;
}

static int cmd_dns(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: dns <hostname>\n");
        return -1;
    }
    shell_print("Resolving ");
    shell_print(argv[1]);
    shell_print("... ");
    u32 ip = dns_lookup(argv[1], net_get_dns());
    if (ip == 0) {
        shell_print("FAILED\n");
        return -1;
    }
    shell_print("OK (");
    shell_print_num((ip >> 24) & 0xFF);
    shell_print("."); shell_print_num((ip >> 16) & 0xFF);
    shell_print("."); shell_print_num((ip >> 8) & 0xFF);
    shell_print("."); shell_print_num(ip & 0xFF);
    shell_print(")\n");
    return 0;
}

static int cmd_nslookup(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: nslookup <hostname>\n");
        return -1;
    }
    u32 ip = dns_lookup(argv[1], net_get_dns());
    if (ip == 0) {
        shell_print("Not found\n");
        return -1;
    }
    shell_print(argv[1]);
    shell_print(" -> ");
    shell_print_num((ip >> 24) & 0xFF);
    shell_print("."); shell_print_num((ip >> 16) & 0xFF);
    shell_print("."); shell_print_num((ip >> 8) & 0xFF);
    shell_print("."); shell_print_num(ip & 0xFF);
    shell_print("\n");
    return 0;
}

static int cmd_wget(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: wget <url>\n");
        return -1;
    }
    shell_print("Downloading ");
    shell_print(argv[1]);
    shell_print("...\n");
    
    u8* data;
    u32 size;
    
    if (http_get(argv[1], &data, &size) != 0) {
        shell_print("Download failed\n");
        return -1;
    }
    
    char filename[256];
    const char* last_slash = strrchr(argv[1], '/');
    if (last_slash) {
        strcpy(filename, last_slash + 1);
    } else {
        strcpy(filename, "download.bin");
    }
    
    shell_print("Saving to ");
    shell_print(filename);
    shell_print("... ");
    
    if (ufs_write(filename, data, size) == 0) {
        shell_print("OK (");
        shell_print_num(size);
        shell_print(" bytes)\n");
        kfree(data);
        return 0;
    } else {
        shell_print("FAILED\n");
        kfree(data);
        return -1;
    }
}

static int cmd_time(int argc, char** argv) {
    (void)argc; (void)argv;
    extern u32 get_seconds(void);
    extern u32 get_ticks(void);
    
    u32 sec = get_seconds();
    u32 ms = get_ticks() % 1000;
    
    shell_print("Uptime: ");
    shell_print_num(sec);
    shell_print(".");
    if (ms < 100) shell_print("0");
    if (ms < 10) shell_print("0");
    shell_print_num(ms);
    shell_print(" seconds\n");
    return 0;
}

void commands_init(void) {
    shell_register_command("help", cmd_help, "show help");
    shell_register_command("clear", cmd_clear, "clear screen");
    shell_register_command("mem", cmd_mem, "show memory");
    shell_register_command("echo", cmd_echo, "echo args");
    shell_register_command("ticks", cmd_ticks, "system ticks");
    shell_register_command("ps", cmd_ps, "list processes");
    shell_register_command("kill", cmd_kill, "kill process");
    shell_register_command("upac", cmd_upac, "package manager");
    shell_register_command("ping", cmd_ping, "ping a host");
    shell_register_command("ifconfig", cmd_ifconfig, "show network config");
    shell_register_command("route", cmd_route, "show routing table");
    shell_register_command("arp", cmd_arp, "show ARP cache");
    shell_register_command("nicinfo", cmd_nicinfo, "show NIC registers");
    shell_register_command("dns", cmd_dns, "resolve hostname");
    shell_register_command("nslookup", cmd_nslookup, "DNS lookup");
    shell_register_command("wget", cmd_wget, "download file");
    shell_register_command("time", cmd_time, "show system uptime");
}
