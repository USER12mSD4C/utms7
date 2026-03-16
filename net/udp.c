#include "udp.h"
#include "ip.h"
#include "net.h"
#include "../kernel/memory.h"
#include "../include/string.h"

int udp_send(u32 dst_ip, u16 src_port, u32 dst_ip2, u16 dst_port, u8 *data, int len) {
    (void)dst_ip2;
    
    int udp_len = sizeof(udp_hdr_t) + len;
    u8 *packet = kmalloc(udp_len);
    if (!packet) return -1;
    
    udp_hdr_t *udp = (udp_hdr_t*)packet;
    
    udp->src_port = (src_port << 8) | (src_port >> 8);
    udp->dst_port = (dst_port << 8) | (dst_port >> 8);
    udp->length = (udp_len << 8) | (udp_len >> 8);
    udp->checksum = 0;
    
    memcpy(packet + sizeof(udp_hdr_t), data, len);
    
    int res = ip_send_packet(dst_ip, 17, packet, udp_len, net_get_mac(), net_get_ip());
    
    kfree(packet);
    return res;
}

int udp_recv(u8 *buf, int len) {
    (void)buf;
    (void)len;
    return 0;
}
