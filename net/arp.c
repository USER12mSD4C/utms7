#include "arp.h"
#include "net.h"
#include "../include/string.h"
#include "../kernel/memory.h"

#define ARP_CACHE_SIZE 16

typedef struct {
    u32 ip;
    u8 mac[6];
    int valid;
} arp_cache_entry_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];

void arp_cache_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = 0;
    }
}

int arp_cache_lookup(u32 ip, u8 *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

void arp_cache_add(u32 ip, u8 *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    }
    
    for (int i = 0; i < ARP_CACHE_SIZE - 1; i++) {
        arp_cache[i] = arp_cache[i + 1];
    }
    
    arp_cache[ARP_CACHE_SIZE - 1].ip = ip;
    memcpy(arp_cache[ARP_CACHE_SIZE - 1].mac, mac, 6);
    arp_cache[ARP_CACHE_SIZE - 1].valid = 1;
}

void arp_send_request(u32 target_ip, u8 *src_mac, u32 src_ip) {
    arp_pkt_t arp;
    u8 broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    arp.htype = 0x0100;
    arp.ptype = 0x0008;
    arp.hlen = 6;
    arp.plen = 4;
    arp.op = 0x0100;
    
    memcpy(arp.sha, src_mac, 6);
    arp.spa = src_ip;
    memset(arp.tha, 0, 6);
    arp.tpa = target_ip;
    
    net_eth_send(broadcast, 0x0608, (u8*)&arp, sizeof(arp_pkt_t));
}

void arp_handle_packet(u8 *packet, int len) {
    if (len < sizeof(arp_pkt_t)) return;
    
    arp_pkt_t *arp = (arp_pkt_t*)packet;
    
    if (arp->op == 0x0100) {
        arp_pkt_t reply;
        
        reply.htype = 0x0100;
        reply.ptype = 0x0008;
        reply.hlen = 6;
        reply.plen = 4;
        reply.op = 0x0200;
        
        memcpy(reply.sha, arp->tha, 6);
        reply.spa = arp->tpa;
        memcpy(reply.tha, arp->sha, 6);
        reply.tpa = arp->spa;
        
        net_eth_send(arp->sha, 0x0608, (u8*)&reply, sizeof(arp_pkt_t));
    }
    
    arp_cache_add(arp->spa, arp->sha);
}
