// net/udp.h
#ifndef UDP_H
#define UDP_H

#include "../include/types.h"

typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed)) udp_hdr_t;

void udp_init(void);
int udp_send(u32 dst_ip, u16 src_port, u16 dst_port, u8 *data, int len);
int udp_recv(u8 *buf, int len);
int udp_bind(u16 port);
void udp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip);

#endif
