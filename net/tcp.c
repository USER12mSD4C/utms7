// net/tcp.c
#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

#define MAX_SOCKETS 16
#define TCP_WINDOW 65535
#define TCP_MSS 1460
#define TCP_TIMEOUT 5000

static tcp_socket_t socks[MAX_SOCKETS];
static u32 ticks = 0;

typedef struct {
    u32 src_ip;
    u32 dst_ip;
    u8 zero;
    u8 proto;
    u16 len;
} __attribute__((packed)) tcp_pseudo_t;

static u16 tcp_checksum(ip_hdr_t *ip, tcp_hdr_t *tcp, int len) {
    tcp_pseudo_t p;
    u32 sum = 0;
    p.src_ip = ip->src;
    p.dst_ip = ip->dst;
    p.zero = 0;
    p.proto = IP_PROTO_TCP;
    p.len = htons(len);
    u16 *pp = (u16*)&p;
    for (int i = 0; i < sizeof(p)/2; i++) sum += pp[i];
    u16 *tp = (u16*)tcp;
    for (int i = 0; i < len/2; i++) sum += tp[i];
    if (len & 1) sum += *((u8*)tcp + len - 1);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

static int tcp_send_packet(tcp_socket_t *s, u8 flags, u8 *data, int len) {
    int tlen = sizeof(tcp_hdr_t) + len;
    int ilen = sizeof(ip_hdr_t) + tlen;
    u8 *pkt = kmalloc(ilen);
    if (!pkt) return -1;
    
    ip_hdr_t *ip = (ip_hdr_t*)pkt;
    tcp_hdr_t *tcp = (tcp_hdr_t*)(pkt + sizeof(ip_hdr_t));
    
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(ilen);
    ip->id = htons(s->seq & 0xFFFF);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->checksum = 0;
    ip->src = s->local_ip;
    ip->dst = s->remote_ip;
    
    tcp->src_port = htons(s->local_port);
    tcp->dst_port = htons(s->remote_port);
    tcp->seq = htonl(s->seq);
    tcp->ack = htonl(s->ack);
    tcp->offset = (sizeof(tcp_hdr_t)/4) << 4;
    tcp->flags = flags;
    tcp->window = htons(TCP_WINDOW);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;
    
    if (len > 0 && data) memcpy(pkt + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t), data, len);
    
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    tcp->checksum = tcp_checksum(ip, tcp, tlen);
    
    int res = ip_send_packet(s->remote_ip, IP_PROTO_TCP, pkt + sizeof(ip_hdr_t), tlen);
    if (res >= 0) {
        if (flags & TCP_FLAG_SYN) s->seq++;
        if (flags & TCP_FLAG_FIN) s->seq++;
        if (len > 0) s->seq += len;
        s->timestamp = ticks;
    }
    kfree(pkt);
    return res;
}

void tcp_init(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        socks[i].used = 0;
        socks[i].rx_buffer = NULL;
    }
    vga_write("TCP initialized\n");
}

int tcp_socket_create(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socks[i].used) {
            socks[i].used = 1;
            socks[i].state = TCP_STATE_CLOSED;
            socks[i].seq = 1000 + i * 1000;
            socks[i].ack = 0;
            socks[i].local_ip = net_get_ip();
            socks[i].local_port = 1024 + i;
            socks[i].remote_ip = 0;
            socks[i].remote_port = 0;
            socks[i].rx_buffer_size = 65536;
            socks[i].rx_buffer = kmalloc(socks[i].rx_buffer_size);
            if (!socks[i].rx_buffer) { socks[i].used = 0; return -1; }
            socks[i].rx_buffer_head = 0;
            socks[i].rx_buffer_tail = 0;
            socks[i].rx_buffer_count = 0;
            socks[i].timestamp = 0;
            socks[i].retries = 0;
            return i;
        }
    }
    return -1;
}

int tcp_connect(int sock, u32 ip, u16 port) {
    if (sock < 0 || sock >= MAX_SOCKETS || !socks[sock].used) return -1;
    tcp_socket_t *s = &socks[sock];
    s->remote_ip = ip;
    s->remote_port = port;
    s->local_ip = net_get_ip();
    s->state = TCP_STATE_SYN_SENT;
    s->retries = 0;
    s->timestamp = ticks;
    return tcp_send_packet(s, TCP_FLAG_SYN, NULL, 0);
}

int tcp_send(int sock, u8 *data, int len) {
    tcp_socket_t *s = &socks[sock];
    if (!s->used || s->state != TCP_STATE_ESTABLISHED || !data || len <= 0) return -1;
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
    tcp_socket_t *s = &socks[sock];
    if (!s->used || !buf || len <= 0) return -1;
    if (s->rx_buffer_count == 0) return 0;
    int total = 0;
    while (s->rx_buffer_count > 0 && total < len) {
        u8 *data = s->rx_buffer + s->rx_buffer_head;
        int avail = s->rx_buffer_count;
        int copy = avail > (len - total) ? (len - total) : avail;
        memcpy(buf + total, data, copy);
        total += copy;
        s->rx_buffer_head = (s->rx_buffer_head + copy) % s->rx_buffer_size;
        s->rx_buffer_count -= copy;
    }
    return total;
}

void tcp_handle_packet(u8 *pkt, int len, u32 src_ip, u32 dst_ip) {
    if (len < sizeof(tcp_hdr_t)) return;
    tcp_hdr_t *tcp = (tcp_hdr_t*)pkt;
    u16 srcp = ntohs(tcp->src_port);
    u16 dstp = ntohs(tcp->dst_port);
    u32 seq = ntohl(tcp->seq);
    u32 ack = ntohl(tcp->ack);
    int hlen = (tcp->offset >> 4) * 4;
    int dlen = len - hlen;
    u8 *data = pkt + hlen;
    u8 flags = tcp->flags;
    (void)dst_ip;
    
    tcp_socket_t *s = NULL;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (socks[i].used && socks[i].local_port == dstp &&
            socks[i].remote_ip == src_ip && socks[i].remote_port == srcp) {
            s = &socks[i];
            break;
        }
    }
    if (!s) return;
    if (flags & TCP_FLAG_ACK) s->ack = ack;
    
    switch (s->state) {
        case TCP_STATE_SYN_SENT:
            if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
                s->state = TCP_STATE_ESTABLISHED;
                s->ack = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
        case TCP_STATE_ESTABLISHED:
            if (dlen > 0 && seq == s->ack) {
                if (s->rx_buffer_count + dlen <= s->rx_buffer_size) {
                    int tail = s->rx_buffer_size - s->rx_buffer_tail;
                    if (dlen <= tail) {
                        memcpy(s->rx_buffer + s->rx_buffer_tail, data, dlen);
                        s->rx_buffer_tail += dlen;
                    } else {
                        memcpy(s->rx_buffer + s->rx_buffer_tail, data, tail);
                        memcpy(s->rx_buffer, data + tail, dlen - tail);
                        s->rx_buffer_tail = dlen - tail;
                    }
                    s->rx_buffer_count += dlen;
                    s->ack = seq + dlen;
                    tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
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
    ticks++;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socks[i].used) continue;
        if (ticks - socks[i].timestamp > TCP_TIMEOUT) {
            if (socks[i].state == TCP_STATE_SYN_SENT && socks[i].retries < 3) {
                socks[i].retries++;
                socks[i].timestamp = ticks;
                tcp_send_packet(&socks[i], TCP_FLAG_SYN, NULL, 0);
            } else if (socks[i].retries >= 3) {
                socks[i].used = 0;
                if (socks[i].rx_buffer) kfree(socks[i].rx_buffer);
            }
        }
    }
}
