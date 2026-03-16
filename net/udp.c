#ifndef UDP_H
#define UDP_H

#include "../include/types.h"

typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} udp_hdr_t;

int udp_send(u32 dst_ip, u16 src_port, u32 dst_ip2, u16 dst_port, u8 *data, int len);
int udp_recv(u8 *buf, int len);

#endif
