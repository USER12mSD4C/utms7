#include "udp.h"
#include "ip.h"
#include "net.h"
#include "../kernel/memory.h"
#include "../include/string.h"
#include "../drivers/vga.h"

#define UDP_MAX_PORTS 16
#define UDP_BUFFER_SIZE 2048

typedef struct {
    int used;
    u16 port;
    u8 buffer[UDP_BUFFER_SIZE];
    u32 buffer_len;
} udp_socket_t;

static udp_socket_t udp_sockets[UDP_MAX_PORTS];

void udp_init(void) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        udp_sockets[i].used = 0;
    }
    vga_write("  UDP initialized\n");
}

static u16 udp_checksum(ip_hdr_t *ip, udp_hdr_t *udp, int udp_len) {
    u32 sum = 0;
    u16 *p;
    
    u32 src = ip->src;
    u32 dst = ip->dst;
    u8 protocol = ip->protocol;
    
    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += protocol;
    sum += udp_len;
    
    p = (u16*)udp;
    for (int i = 0; i < udp_len / 2; i++) {
        sum += p[i];
    }
    
    if (udp_len & 1) {
        sum += *((u8*)udp + udp_len - 1);
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

int udp_send(u32 dst_ip, u16 src_port, u32 dst_ip2, u16 dst_port, u8 *data, int len) {
    (void)dst_ip2;
    
    vga_write("  UDP send to ");
    vga_write_num((dst_ip >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((dst_ip >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((dst_ip >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(dst_ip & 0xFF);
    vga_write(":");
    vga_write_num(dst_port);
    vga_write(" len=");
    vga_write_num(len);
    vga_write("\n");
    
    int udp_len = sizeof(udp_hdr_t) + len;
    u8 *packet = kmalloc(udp_len);
    if (!packet) return -1;
    
    udp_hdr_t *udp = (udp_hdr_t*)packet;
    
    udp->src_port = (src_port << 8) | (src_port >> 8);
    udp->dst_port = (dst_port << 8) | (dst_port >> 8);
    udp->length = (udp_len << 8) | (udp_len >> 8);
    udp->checksum = 0;
    
    if (len > 0) {
        memcpy(packet + sizeof(udp_hdr_t), data, len);
    }
    
    ip_hdr_t ip;
    ip.src = net_get_ip();
    ip.dst = dst_ip;
    ip.protocol = IP_PROTO_UDP;
    
    udp->checksum = udp_checksum(&ip, udp, udp_len);
    
    int res = ip_send_packet(dst_ip, IP_PROTO_UDP, packet, udp_len, net_get_mac(), net_get_ip());
    
    kfree(packet);
    return res;
}

void udp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    if (len < sizeof(udp_hdr_t)) {
        vga_write("  UDP packet too short\n");
        return;
    }
    
    udp_hdr_t *udp = (udp_hdr_t*)packet;
    u16 dst_port = (udp->dst_port << 8) | (udp->dst_port >> 8);
    u16 src_port = (udp->src_port << 8) | (udp->src_port >> 8);
    int udp_len = (udp->length << 8) | (udp->length >> 8);
    int data_len = udp_len - sizeof(udp_hdr_t);
    u8 *data = packet + sizeof(udp_hdr_t);
    
    vga_write("  UDP recv from ");
    vga_write_num((src_ip >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((src_ip >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((src_ip >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(src_ip & 0xFF);
    vga_write(":");
    vga_write_num(src_port);
    vga_write(" -> port ");
    vga_write_num(dst_port);
    vga_write(" len=");
    vga_write_num(data_len);
    vga_write("\n");
    
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == dst_port) {
            int copy_len = data_len;
            if (copy_len > UDP_BUFFER_SIZE) copy_len = UDP_BUFFER_SIZE;
            
            memcpy(udp_sockets[i].buffer, data, copy_len);
            udp_sockets[i].buffer_len = copy_len;
            vga_write("  UDP data saved to socket ");
            vga_write_num(i);
            vga_write("\n");
            return;
        }
    }
    
    // Если сокет не найден, создаем временный
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].port = dst_port;
            
            int copy_len = data_len;
            if (copy_len > UDP_BUFFER_SIZE) copy_len = UDP_BUFFER_SIZE;
            
            memcpy(udp_sockets[i].buffer, data, copy_len);
            udp_sockets[i].buffer_len = copy_len;
            vga_write("  UDP created new socket ");
            vga_write_num(i);
            vga_write(" for port ");
            vga_write_num(dst_port);
            vga_write("\n");
            return;
        }
    }
    
    vga_write("  UDP no free sockets\n");
}

int udp_bind(u16 port) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].port = port;
            udp_sockets[i].buffer_len = 0;
            vga_write("  UDP bound port ");
            vga_write_num(port);
            vga_write(" to socket ");
            vga_write_num(i);
            vga_write("\n");
            return i;
        }
    }
    return -1;
}

int udp_recv(u8 *buf, int len) {
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].buffer_len > 0) {
            int copy_len = udp_sockets[i].buffer_len;
            if (copy_len > len) copy_len = len;
            
            memcpy(buf, udp_sockets[i].buffer, copy_len);
            udp_sockets[i].buffer_len = 0;
            
            vga_write("  UDP recv from socket ");
            vga_write_num(i);
            vga_write(": ");
            vga_write_num(copy_len);
            vga_write(" bytes\n");
            
            return copy_len;
        }
    }
    return 0;
}
