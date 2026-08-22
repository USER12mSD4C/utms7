// include/netconfig.h
#ifndef NETCONFIG_H
#define NETCONFIG_H

#include "types.h"

typedef struct {
    u32 ip;
    u32 netmask;
    u32 gateway;
    u32 dns;
    int use_dhcp;
} net_config_t;

int net_config_load(const char *path);
void net_config_save(const char *path);
void net_config_set_default(void);
net_config_t* net_config_get(void);

#endif
