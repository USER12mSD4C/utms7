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
    u8 *buffer;
    u32 buffer_len;
    u32 buffer_capacity;
} udp_socket_t;

static udp_socket_t udp_sockets[UDP_MAX_PORTS];

static u16 udp_checksum(ip_hdr_t *ip, udp_hdr_t *udp, int udp_len) {
    u32 sum = 0;
    sum += (ip->src >> 16) & 0xFFFF;
    sum += ip->src & 0xFFFF;
    sum += (ip->dst >> 16) & 0xFFFF;
    sum += ip->dst & 0xFFFF;
    sum += ip->protocol;
    sum += udp_len;
    
    u16 *p = (u16*)udp;
    for (int i = 0; i < udp_len / 2; i++) sum += p[i];
    if (udp_len & 1) sum += *((u8*)udp + udp_len - 1);
    
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

void udp_init(void) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        udp_sockets[i].used = 0;
        udp_sockets[i].port = 0;
        udp_sockets[i].buffer = NULL;
        udp_sockets[i].buffer_len = 0;
        udp_sockets[i].buffer_capacity = 0;
    }
    vga_write("UDP initialized\n");
}

int udp_send(u32 dst_ip, u16 src_port, u16 dst_port, u8 *data, int len) {
    int udp_len = sizeof(udp_hdr_t) + len;
    u8 *packet = kmalloc(udp_len);
    if (!packet) return -1;
    
    udp_hdr_t *udp = (udp_hdr_t*)packet;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(udp_len);
    udp->checksum = 0;
    
    if (len > 0 && data) {
        memcpy(packet + sizeof(udp_hdr_t), data, len);
    }
    
    ip_hdr_t pseudo_ip;
    pseudo_ip.src = net_get_ip();
    pseudo_ip.dst = dst_ip;
    pseudo_ip.protocol = IP_PROTO_UDP;
    udp->checksum = udp_checksum(&pseudo_ip, udp, udp_len);
    
    int res = ip_send_packet(dst_ip, IP_PROTO_UDP, packet, udp_len);
    kfree(packet);
    return res;
}

int udp_bind(u16 port) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].port = port;
            udp_sockets[i].buffer_len = 0;
            udp_sockets[i].buffer_capacity = UDP_BUFFER_SIZE;
            udp_sockets[i].buffer = kmalloc(udp_sockets[i].buffer_capacity);
            if (!udp_sockets[i].buffer) {
                udp_sockets[i].used = 0;
                return -1;
            }
            return i;
        }
    }
    return -1;
}

void udp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    (void)src_ip;
    (void)dst_ip;
    if (len < sizeof(udp_hdr_t)) return;
    
    udp_hdr_t *udp = (udp_hdr_t*)packet;
    u16 dst_port = ntohs(udp->dst_port);
    int udp_len = ntohs(udp->length);
    int data_len = udp_len - sizeof(udp_hdr_t);
    u8 *data = packet + sizeof(udp_hdr_t);
    
    if (data_len < 0 || udp_len > len) return;
    
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == dst_port) {
            if (data_len > udp_sockets[i].buffer_capacity) {
                u8 *new_buf = kmalloc(data_len);
                if (!new_buf) return;
                if (udp_sockets[i].buffer) kfree(udp_sockets[i].buffer);
                udp_sockets[i].buffer = new_buf;
                udp_sockets[i].buffer_capacity = data_len;
            }
            
            memcpy(udp_sockets[i].buffer, data, data_len);
            udp_sockets[i].buffer_len = data_len;
            return;
        }
    }
}

int udp_recv(u8 *buf, int len) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].buffer_len > 0) {
            int copy_len = udp_sockets[i].buffer_len;
            if (copy_len > len) copy_len = len;
            
            memcpy(buf, udp_sockets[i].buffer, copy_len);
            udp_sockets[i].buffer_len = 0;
            
            return copy_len;
        }
    }
    return 0;
}
