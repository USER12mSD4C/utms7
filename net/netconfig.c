#include "../include/netconfig.h"
#include "../include/string.h"
#include "../kernel/vfs.h"
#include "../kernel/memory.h"
#include "../drivers/drm.h"

static net_config_t config;

void net_config_set_default(void) {
    config.ip = 0x0A00020F;      // 10.0.2.15
    config.netmask = 0xFFFFFF00; // 255.255.255.0
    config.gateway = 0x0A000202; // 10.0.2.2
    config.dns = 0x0A000202;     // 10.0.2.2
    config.use_dhcp = 1;
}

int net_config_load(const char *path) {
    u8 *data = NULL;
    u32 size = 0;

    if (vfs_read_entire(path, &data, &size) != 0) {
        return -1;
    }

    char *p = (char*)data;
    char *end = p + size;
    int a, b, c, d;

    while (p < end) {
        if (strncmp(p, "ip=", 3) == 0) {
            p += 3;
            sscanf(p, "%d.%d.%d.%d", &a, &b, &c, &d);
            config.ip = (a << 24) | (b << 16) | (c << 8) | d;
        } else if (strncmp(p, "netmask=", 8) == 0) {
            p += 8;
            sscanf(p, "%d.%d.%d.%d", &a, &b, &c, &d);
            config.netmask = (a << 24) | (b << 16) | (c << 8) | d;
        } else if (strncmp(p, "gateway=", 8) == 0) {
            p += 8;
            sscanf(p, "%d.%d.%d.%d", &a, &b, &c, &d);
            config.gateway = (a << 24) | (b << 16) | (c << 8) | d;
        } else if (strncmp(p, "dns=", 4) == 0) {
            p += 4;
            sscanf(p, "%d.%d.%d.%d", &a, &b, &c, &d);
            config.dns = (a << 24) | (b << 16) | (c << 8) | d;
        } else if (strncmp(p, "dhcp=", 5) == 0) {
            p += 5;
            config.use_dhcp = (*p == '1');
        }

        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    kfree(data);
    return 0;
}

void net_config_save(const char *path) {
    char buf[256];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "ip=%d.%d.%d.%d\n",
        (config.ip >> 24) & 0xFF, (config.ip >> 16) & 0xFF,
        (config.ip >> 8) & 0xFF, config.ip & 0xFF);

    pos += snprintf(buf + pos, sizeof(buf) - pos, "netmask=%d.%d.%d.%d\n",
        (config.netmask >> 24) & 0xFF, (config.netmask >> 16) & 0xFF,
        (config.netmask >> 8) & 0xFF, config.netmask & 0xFF);

    pos += snprintf(buf + pos, sizeof(buf) - pos, "gateway=%d.%d.%d.%d\n",
        (config.gateway >> 24) & 0xFF, (config.gateway >> 16) & 0xFF,
        (config.gateway >> 8) & 0xFF, config.gateway & 0xFF);

    pos += snprintf(buf + pos, sizeof(buf) - pos, "dns=%d.%d.%d.%d\n",
        (config.dns >> 24) & 0xFF, (config.dns >> 16) & 0xFF,
        (config.dns >> 8) & 0xFF, config.dns & 0xFF);

    pos += snprintf(buf + pos, sizeof(buf) - pos, "dhcp=%d\n", config.use_dhcp);

    vfs_write_entire(path, (u8*)buf, pos);
}

net_config_t* net_config_get(void) {
    return &config;
}
