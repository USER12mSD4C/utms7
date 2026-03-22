// net/udp.c
#include "udp.h"
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

#define UDP_MAX_PORTS 16
#define UDP_BUFFER_SIZE 8192

typedef struct {
    int used;
    u16 port;
    u8 *buf;
    u32 len;
    u32 cap;
} udp_socket_t;

static udp_socket_t socks[UDP_MAX_PORTS];

static u16 udp_checksum(ip_hdr_t *ip, udp_hdr_t *udp, int len) {
    u32 sum = 0;
    sum += (ip->src >> 16) & 0xFFFF; sum += ip->src & 0xFFFF;
    sum += (ip->dst >> 16) & 0xFFFF; sum += ip->dst & 0xFFFF;
    sum += ip->protocol; sum += len;
    u16 *p = (u16*)udp;
    for (int i = 0; i < len / 2; i++) sum += p[i];
    if (len & 1) sum += *((u8*)udp + len - 1);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

void udp_init(void) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) socks[i].used = 0;
    vga_write("UDP initialized\n");
}

int udp_send(u32 dst, u16 srcp, u16 dstp, u8 *data, int len) {
    int ulen = sizeof(udp_hdr_t) + len;
    u8 *pkt = kmalloc(ulen);
    if (!pkt) return -1;
    udp_hdr_t *udp = (udp_hdr_t*)pkt;
    udp->src_port = htons(srcp);
    udp->dst_port = htons(dstp);
    udp->length = htons(ulen);
    udp->checksum = 0;
    if (len > 0 && data) memcpy(pkt + sizeof(udp_hdr_t), data, len);
    ip_hdr_t ip;
    ip.src = net_get_ip();
    ip.dst = dst;
    ip.protocol = IP_PROTO_UDP;
    udp->checksum = udp_checksum(&ip, udp, ulen);
    int res = ip_send_packet(dst, IP_PROTO_UDP, pkt, ulen);
    kfree(pkt);
    return res;
}

int udp_bind(u16 port) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (!socks[i].used) {
            socks[i].used = 1;
            socks[i].port = port;
            socks[i].len = 0;
            socks[i].cap = UDP_BUFFER_SIZE;
            socks[i].buf = kmalloc(socks[i].cap);
            return i;
        }
    }
    return -1;
}

void udp_handle_packet(u8 *pkt, int len, u32 src, u32 dst) {
    (void)src; (void)dst;
    if (len < sizeof(udp_hdr_t)) return;
    udp_hdr_t *udp = (udp_hdr_t*)pkt;
    u16 dstp = ntohs(udp->dst_port);
    int ulen = ntohs(udp->length);
    int dlen = ulen - sizeof(udp_hdr_t);
    u8 *data = pkt + sizeof(udp_hdr_t);
    if (dlen < 0 || ulen > len) return;
    
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (socks[i].used && socks[i].port == dstp) {
            if (dlen > socks[i].cap) {
                u8 *new = kmalloc(dlen);
                if (!new) return;
                if (socks[i].buf) kfree(socks[i].buf);
                socks[i].buf = new;
                socks[i].cap = dlen;
            }
            memcpy(socks[i].buf, data, dlen);
            socks[i].len = dlen;
            return;
        }
    }
}

int udp_recv(u8 *buf, int len) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (socks[i].used && socks[i].len > 0) {
            int copy = socks[i].len;
            if (copy > len) copy = len;
            memcpy(buf, socks[i].buf, copy);
            socks[i].len = 0;
            return copy;
        }
    }
    return 0;
}
