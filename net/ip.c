// net/ip.c
#include "ip.h"
#include "arp.h"
#include "net.h"
#include "ethernet.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"

u16 ip_checksum(u16 *data, int len) {
    u32 sum = 0;
    for (int i = 0; i < len / 2; i++) sum += data[i];
    if (len & 1) sum += *((u8*)data + len - 1);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

int ip_send_packet(u32 dst_ip, u8 proto, u8 *data, int len) {
    u8 dst_mac[6];
    u32 src_ip = net_get_ip();
    u8 *src_mac = net_get_mac();
    u32 gateway = net_get_gateway();
    u32 netmask = net_get_netmask();
    
    u32 target = dst_ip;
    if ((dst_ip & netmask) != (src_ip & netmask) && gateway) target = gateway;
    
    int attempts = 0;
    while (attempts < 3) {
        if (arp_cache_lookup(target, dst_mac) == 0) break;
        arp_send_request(target, src_mac, src_ip);
        for (int i = 0; i < 1000000; i++) __asm__ volatile ("pause");
        attempts++;
    }
    if (attempts >= 3) return -1;
    
    int total = sizeof(ip_hdr_t) + len;
    u8 *pkt = kmalloc(total);
    if (!pkt) return -1;
    
    ip_hdr_t *ip = (ip_hdr_t*)pkt;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(total);
    ip->id = htons(0x0100);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src = src_ip;
    ip->dst = dst_ip;
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    
    memcpy(pkt + sizeof(ip_hdr_t), data, len);
    net_eth_send(dst_mac, ETHERTYPE_IP, pkt, total);
    kfree(pkt);
    return len;
}

void ip_handle_packet(u8 *pkt, int len, void (*tcp)(u8*,int,u32,u32),
                      void (*udp)(u8*,int,u32,u32), void (*icmp)(u8*,int,u32,u32)) {
    if (len < sizeof(ip_hdr_t)) return;
    ip_hdr_t *ip = (ip_hdr_t*)pkt;
    if ((ip->ver_ihl >> 4) != 4) return;
    
    int hlen = (ip->ver_ihl & 0x0F) * 4;
    if (hlen < sizeof(ip_hdr_t) || hlen > len) return;
    u16 total = ntohs(ip->total_len);
    if (total > len) total = len;
    if (ip->dst != net_get_ip() && ip->dst != 0xFFFFFFFF) return;
    
    int dlen = total - hlen;
    u8 *data = pkt + hlen;
    
    switch (ip->protocol) {
        case IP_PROTO_TCP: if (tcp) tcp(data, dlen, ip->src, ip->dst); break;
        case IP_PROTO_UDP: if (udp) udp(data, dlen, ip->src, ip->dst); break;
        case IP_PROTO_ICMP: if (icmp) icmp(data, dlen, ip->src, ip->dst); break;
    }
}
