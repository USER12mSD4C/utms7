#include "net.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "rtl8139.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"

static u8 our_mac[6];
static u32 our_ip = 0;
static u32 our_netmask = 0xFFFFFF00;
static u32 our_gateway = 0;
static u32 our_dns = 0;

void net_init(void) {
    rtl8139_get_mac(our_mac);
    
    arp_cache_init();
    tcp_init();
    
    dhcp_start();
}

void net_handle_packet(u8 *packet, int len) {
    if (len < sizeof(eth_hdr_t)) return;
    
    eth_hdr_t *eth = (eth_hdr_t*)packet;
    u16 type = (eth->type << 8) | (eth->type >> 8);
    
    switch (type) {
        case 0x0608: // ARP
            arp_handle_packet(packet + sizeof(eth_hdr_t), len - sizeof(eth_hdr_t));
            break;
        case 0x0008: // IP
            ip_handle_packet(packet + sizeof(eth_hdr_t), len - sizeof(eth_hdr_t), tcp_handle_packet);
            break;
    }
}

void net_eth_send(u8 *dst_mac, u16 type, u8 *data, int len) {
    u8 *packet = kmalloc(len + sizeof(eth_hdr_t));
    eth_hdr_t *eth = (eth_hdr_t*)packet;
    
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, our_mac, 6);
    eth->type = (type << 8) | (type >> 8);
    
    memcpy(packet + sizeof(eth_hdr_t), data, len);
    
    rtl8139_send(packet, len + sizeof(eth_hdr_t));
    kfree(packet);
}

u8* net_get_mac(void) { return our_mac; }
u32 net_get_ip(void) { return our_ip; }
void net_set_ip(u32 ip) { our_ip = ip; }
void net_set_netmask(u32 mask) { our_netmask = mask; }
void net_set_gateway(u32 gw) { our_gateway = gw; }
void net_set_dns(u32 dns) { our_dns = dns; }
u32 net_get_dns(void) { return our_dns; }
