#ifndef TCP_H
#define TCP_H

#include "../include/types.h"

#define TCP_FLAG_FIN 1
#define TCP_FLAG_SYN 2
#define TCP_FLAG_RST 4
#define TCP_FLAG_PSH 8
#define TCP_FLAG_ACK 16
#define TCP_FLAG_URG 32

#define TCP_STATE_CLOSED 0
#define TCP_STATE_LISTEN 1
#define TCP_STATE_SYN_SENT 2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT1 5
#define TCP_STATE_FIN_WAIT2 6
#define TCP_STATE_CLOSE_WAIT 7
#define TCP_STATE_CLOSING 8
#define TCP_STATE_LAST_ACK 9
#define TCP_STATE_TIME_WAIT 10

typedef struct {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8 offset;
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urg_ptr;
} tcp_hdr_t;

typedef struct {
    int used;
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    u32 seq;
    u32 ack;
    int state;
    u8 *rx_buffer;
    u32 rx_buffer_size;
    u32 rx_buffer_head;
    u32 rx_buffer_tail;
    u32 rx_buffer_count;
    u32 timestamp;
    int retries;
} tcp_socket_t;

void tcp_init(void);
int tcp_socket_create(void);
int tcp_connect(int sock, u32 ip, u16 port);
int tcp_send(int sock, u8 *data, int len);
int tcp_recv(int sock, u8 *buf, int len);
void tcp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip);
void tcp_timer_tick(void);

#endif
