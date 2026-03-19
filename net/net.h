#ifndef NET_H
#define NET_H

#include "../include/types.h"

void net_init(void);
void net_handle_packet(u8 *packet, int len);
void net_eth_send(u8 *dst_mac, u16 type, u8 *data, int len);
u8* net_get_mac(void);
u32 net_get_ip(void);
void net_set_ip(u32 ip);
void net_set_netmask(u32 mask);
void net_set_gateway(u32 gw);
void net_set_dns(u32 dns);
u32 net_get_dns(void);
int net_is_ready(void);

#endif
