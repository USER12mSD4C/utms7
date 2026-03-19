#ifndef IP_H
#define IP_H

#include "../include/types.h"

#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17
#define IP_PROTO_ICMP 1

typedef struct {
    u8 ver_ihl;
    u8 tos;
    u16 total_len;
    u16 id;
    u16 frag_off;
    u8 ttl;
    u8 protocol;
    u16 checksum;
    u32 src;
    u32 dst;
} ip_hdr_t;

// ICMP структуры
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8

typedef struct {
    u8 type;
    u8 code;
    u16 checksum;
    u16 id;
    u16 seq;
} icmp_hdr_t;

u16 ip_checksum(u16 *data, int len);
int ip_send_packet(u32 dst_ip, u8 protocol, u8 *data, int len, u8 *src_mac, u32 src_ip);
void ip_handle_packet(u8 *packet, int len, 
                      void (*tcp_handler)(u8*, int, u32, u32),
                      void (*udp_handler)(u8*, int, u32, u32),
                      void (*icmp_handler)(u8*, int, u32, u32));

// ICMP функции
int icmp_send_request(u32 dst_ip, u16 id, u16 seq);
void icmp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip);
int icmp_ping(u32 dst_ip, int timeout_ms);

#endif
