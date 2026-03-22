// net/tcp.h
#ifndef NET_TCP_H
#define NET_TCP_H

#include "../include/types.h"

#define TCP_MSS 1460
#define TCP_WINDOW 65535
#define TCP_RETRIES 5
#define TCP_TIMEOUT_MS 3000

// TCP состояния
#define TCP_CLOSED       0
#define TCP_LISTEN       1
#define TCP_SYN_SENT     2
#define TCP_SYN_RCVD     3
#define TCP_ESTABLISHED  4
#define TCP_FIN_WAIT_1   5
#define TCP_FIN_WAIT_2   6
#define TCP_CLOSE_WAIT   7
#define TCP_CLOSING      8
#define TCP_LAST_ACK     9
#define TCP_TIME_WAIT    10

// Флаги TCP
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

// TCP заголовок
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
} __attribute__((packed)) tcp_hdr_t;

typedef struct tcp_socket {
    int used;
    int fd;
    
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    
    u32 snd_nxt;      // следующий номер для отправки
    u32 snd_una;      // неподтверждённый
    u32 rcv_nxt;      // следующий ожидаемый номер
    
    u32 snd_wnd;      // окно отправки
    u32 rcv_wnd;      // окно приёма
    
    u8 state;
    u32 timestamp;
    int retries;
    
    // Буфер приёма
    u8 *rcv_buf;
    u32 rcv_buf_size;
    u32 rcv_buf_head;
    u32 rcv_buf_tail;
    u32 rcv_buf_count;
    
    // Буфер отправки
    u8 *snd_buf;
    u32 snd_buf_size;
    u32 snd_buf_head;
    u32 snd_buf_tail;
    u32 snd_buf_count;
    
    struct tcp_socket *next;
} tcp_socket_t;

// Прототипы
void tcp_init(void);
int tcp_socket(void);
int tcp_bind(int fd, u16 port);
int tcp_connect(int fd, u32 ip, u16 port);
int tcp_listen(int fd, int backlog);
int tcp_accept(int fd, u32 *client_ip, u16 *client_port);
int tcp_send(int fd, const u8 *data, int len);
int tcp_recv(int fd, u8 *buf, int len);
int tcp_close(int fd);
void tcp_timer(void);
void tcp_handle_packet(u8 *pkt, int len, u32 src_ip, u32 dst_ip);

#endif
