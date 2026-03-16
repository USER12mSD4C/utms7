#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../kernel/memory.h"

#define MAX_SOCKETS 16
#define TCP_WINDOW 65535
#define TCP_MSS 1460
#define TCP_TIMEOUT 5000

static tcp_socket_t sockets[MAX_SOCKETS];
static u32 system_ticks = 0;

typedef struct {
    u32 src_ip;
    u32 dst_ip;
    u8 zero;
    u8 protocol;
    u16 tcp_len;
} tcp_pseudo_t;

void tcp_init(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        sockets[i].used = 0;
        sockets[i].rx_buffer = NULL;
    }
}

int tcp_socket_create(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            sockets[i].used = 1;
            sockets[i].state = TCP_STATE_CLOSED;
            sockets[i].seq = 1000 + i * 1000;
            sockets[i].ack = 0;
            sockets[i].rx_buffer_size = 65536;
            sockets[i].rx_buffer = kmalloc(sockets[i].rx_buffer_size);
            if (!sockets[i].rx_buffer) return -1;
            sockets[i].rx_buffer_head = 0;
            sockets[i].rx_buffer_tail = 0;
            sockets[i].rx_buffer_count = 0;
            sockets[i].timestamp = 0;
            sockets[i].retries = 0;
            return i;
        }
    }
    return -1;
}

static u16 tcp_checksum(ip_hdr_t *ip, tcp_hdr_t *tcp, int tcp_len) {
    tcp_pseudo_t pseudo;
    u32 sum = 0;
    u16 *p;
    
    pseudo.src_ip = ip->src;
    pseudo.dst_ip = ip->dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_len = (tcp_len << 8) | (tcp_len >> 8);
    
    p = (u16*)&pseudo;
    for (int i = 0; i < sizeof(pseudo)/2; i++) {
        sum += p[i];
    }
    
    p = (u16*)tcp;
    for (int i = 0; i < tcp_len/2; i++) {
        sum += p[i];
    }
    
    if (tcp_len & 1) {
        sum += *((u8*)tcp + tcp_len - 1);
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

static int tcp_send_packet(tcp_socket_t *s, u8 flags, u8 *data, int len) {
    int tcp_len = sizeof(tcp_hdr_t) + len;
    int ip_len = sizeof(ip_hdr_t) + tcp_len;
    
    u8 *packet = kmalloc(ip_len);
    if (!packet) return -1;
    
    ip_hdr_t *ip = (ip_hdr_t*)packet;
    tcp_hdr_t *tcp = (tcp_hdr_t*)(packet + sizeof(ip_hdr_t));
    
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = (ip_len << 8) | (ip_len >> 8);
    ip->id = (s->seq << 8) | (s->seq >> 8);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->checksum = 0;
    ip->src = s->local_ip;
    ip->dst = s->remote_ip;
    
    tcp->src_port = (s->local_port << 8) | (s->local_port >> 8);
    tcp->dst_port = (s->remote_port << 8) | (s->remote_port >> 8);
    tcp->seq = (s->seq << 24) | ((s->seq << 8) & 0xFF0000) | ((s->seq >> 8) & 0xFF00) | (s->seq >> 24);
    tcp->ack = (s->ack << 24) | ((s->ack << 8) & 0xFF0000) | ((s->ack >> 8) & 0xFF00) | (s->ack >> 24);
    tcp->offset = (sizeof(tcp_hdr_t)/4) << 4;
    tcp->flags = flags;
    tcp->window = (TCP_WINDOW << 8) | (TCP_WINDOW >> 8);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;
    
    if (len > 0) {
        memcpy(packet + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t), data, len);
    }
    
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    tcp->checksum = tcp_checksum(ip, tcp, tcp_len);
    
    int res = ip_send_packet(s->remote_ip, IP_PROTO_TCP, 
                            packet + sizeof(ip_hdr_t), tcp_len,
                            net_get_mac(), s->local_ip);
    
    if (res >= 0) {
        if (flags & TCP_FLAG_SYN) s->seq++;
        if (flags & TCP_FLAG_FIN) s->seq++;
        if (len > 0) s->seq += len;
        s->timestamp = system_ticks;
    }
    
    kfree(packet);
    return res;
}

int tcp_connect(int sock, u32 ip, u16 port) {
    if (sock < 0 || sock >= MAX_SOCKETS || !sockets[sock].used) return -1;
    
    tcp_socket_t *s = &sockets[sock];
    s->remote_ip = ip;
    s->remote_port = port;
    s->local_port = 1024 + sock;
    s->state = TCP_STATE_SYN_SENT;
    s->retries = 0;
    
    return tcp_send_packet(s, TCP_FLAG_SYN, NULL, 0);
}

int tcp_send(int sock, u8 *data, int len) {
    if (sock < 0 || sock >= MAX_SOCKETS || !sockets[sock].used) return -1;
    
    tcp_socket_t *s = &sockets[sock];
    if (s->state != TCP_STATE_ESTABLISHED) return -1;
    
    int sent = 0;
    while (sent < len) {
        int chunk = (len - sent) > TCP_MSS ? TCP_MSS : (len - sent);
        int res = tcp_send_packet(s, TCP_FLAG_PSH | TCP_FLAG_ACK, data + sent, chunk);
        if (res < 0) return sent;
        sent += chunk;
    }
    
    return sent;
}

int tcp_recv(int sock, u8 *buf, int len) {
    if (sock < 0 || sock >= MAX_SOCKETS || !sockets[sock].used) return -1;
    
    tcp_socket_t *s = &sockets[sock];
    if (s->rx_buffer_count == 0) return 0;
    
    int total = 0;
    while (s->rx_buffer_count > 0 && total < len) {
        u8 *data = s->rx_buffer + s->rx_buffer_head;
        int available = s->rx_buffer_count;
        int to_copy = (available < len - total) ? available : len - total;
        
        memcpy(buf + total, data, to_copy);
        total += to_copy;
        
        s->rx_buffer_head = (s->rx_buffer_head + to_copy) % s->rx_buffer_size;
        s->rx_buffer_count -= to_copy;
    }
    
    return total;
}

void tcp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    if (len < sizeof(tcp_hdr_t)) return;
    
    tcp_hdr_t *tcp = (tcp_hdr_t*)packet;
    u16 src_port = (tcp->src_port << 8) | (tcp->src_port >> 8);
    u16 dst_port = (tcp->dst_port << 8) | (tcp->dst_port >> 8);
    u32 seq = (tcp->seq << 24) | ((tcp->seq << 8) & 0xFF0000) | ((tcp->seq >> 8) & 0xFF00) | (tcp->seq >> 24);
    u32 ack = (tcp->ack << 24) | ((tcp->ack << 8) & 0xFF0000) | ((tcp->ack >> 8) & 0xFF00) | (tcp->ack >> 24);
    int header_len = (tcp->offset >> 4) * 4;
    int data_len = len - header_len;
    u8 *data = packet + header_len;
    u8 flags = tcp->flags;
    
    tcp_socket_t *s = NULL;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].used &&
            sockets[i].local_port == dst_port &&
            sockets[i].remote_ip == src_ip &&
            sockets[i].remote_port == src_port) {
            s = &sockets[i];
            break;
        }
    }
    
    if (!s) return;
    
    if (flags & TCP_FLAG_ACK) {
        s->ack = ack;
    }
    
    switch (s->state) {
        case TCP_STATE_SYN_SENT:
            if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
                s->state = TCP_STATE_ESTABLISHED;
                s->ack = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            if (data_len > 0) {
                if (seq == s->ack) {
                    if (s->rx_buffer_count + data_len <= s->rx_buffer_size) {
                        int tail_space = s->rx_buffer_size - s->rx_buffer_tail;
                        if (data_len <= tail_space) {
                            memcpy(s->rx_buffer + s->rx_buffer_tail, data, data_len);
                            s->rx_buffer_tail += data_len;
                        } else {
                            memcpy(s->rx_buffer + s->rx_buffer_tail, data, tail_space);
                            memcpy(s->rx_buffer, data + tail_space, data_len - tail_space);
                            s->rx_buffer_tail = data_len - tail_space;
                        }
                        s->rx_buffer_count += data_len;
                        s->ack = seq + data_len;
                        tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
                    }
                }
            }
            
            if (flags & TCP_FLAG_FIN) {
                s->state = TCP_STATE_CLOSE_WAIT;
                s->ack = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
    }
}

void tcp_timer_tick(void) {
    system_ticks++;
    
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) continue;
        
        if (system_ticks - sockets[i].timestamp > TCP_TIMEOUT) {
            if (sockets[i].state == TCP_STATE_SYN_SENT) {
                if (sockets[i].retries < 3) {
                    sockets[i].retries++;
                    tcp_send_packet(&sockets[i], TCP_FLAG_SYN, NULL, 0);
                } else {
                    sockets[i].used = 0;
                    if (sockets[i].rx_buffer) kfree(sockets[i].rx_buffer);
                }
            }
        }
    }
}
