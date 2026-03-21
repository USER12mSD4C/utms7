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

int ip_send_packet(u32 dst_ip, u8 protocol, u8 *data, int len) {
    u8 dst_mac[6];
    int attempts = 0;
    u32 src_ip = net_get_ip();
    u8 *src_mac = net_get_mac();
    u32 gateway = net_get_gateway();
    u32 netmask = net_get_netmask();
    
    // Определяем, в той же ли сети destination
    u32 target_ip = dst_ip;
    int need_gateway = 0;
    
    if (gateway != 0) {
        if ((dst_ip & netmask) != (src_ip & netmask)) {
            target_ip = gateway;
            need_gateway = 1;
        }
    }
    
    while (attempts < 3) {
        if (arp_cache_lookup(target_ip, dst_mac) == 0) break;
        arp_send_request(target_ip, src_mac, src_ip);
        for (int wait = 0; wait < 1000000; wait++) {
            __asm__ volatile ("pause");
            if (arp_cache_lookup(target_ip, dst_mac) == 0) break;
        }
        attempts++;
    }
    if (attempts >= 3) return -1;
    
    int total_len = sizeof(ip_hdr_t) + len;
    u8 *packet = kmalloc(total_len);
    if (!packet) return -1;
    
    ip_hdr_t *ip = (ip_hdr_t*)packet;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(0x0100);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src = src_ip;
    ip->dst = dst_ip;  // В IP-заголовке оригинальный destination
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    
    memcpy(packet + sizeof(ip_hdr_t), data, len);
    net_eth_send(dst_mac, ETHERTYPE_IP, packet, total_len);
    kfree(packet);
    return len;
}

void ip_handle_packet(u8 *packet, int len, 
                      void (*tcp_handler)(u8*, int, u32, u32),
                      void (*udp_handler)(u8*, int, u32, u32),
                      void (*icmp_handler)(u8*, int, u32, u32)) {
    if (len < sizeof(ip_hdr_t)) return;
    ip_hdr_t *ip = (ip_hdr_t*)packet;
    if ((ip->ver_ihl >> 4) != 4) return;
    
    int header_len = (ip->ver_ihl & 0x0F) * 4;
    if (header_len < sizeof(ip_hdr_t) || header_len > len) return;
    
    u16 total_len = ntohs(ip->total_len);
    if (total_len > len) total_len = len;
    if (ip->dst != net_get_ip() && ip->dst != 0xFFFFFFFF) return;
    
    int data_len = total_len - header_len;
    u8 *data = packet + header_len;
    
    switch (ip->protocol) {
        case IP_PROTO_TCP:
            if (tcp_handler) tcp_handler(data, data_len, ip->src, ip->dst);
            break;
        case IP_PROTO_UDP:
            if (udp_handler) udp_handler(data, data_len, ip->src, ip->dst);
            break;
        case IP_PROTO_ICMP:
            if (icmp_handler) icmp_handler(data, data_len, ip->src, ip->dst);
            break;
    }
}
