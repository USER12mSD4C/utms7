// net/arp.c
#include "arp.h"
#include "net.h"
#include "ethernet.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

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

int arp_cache_dump(char *buf, int max_len) {
    int pos = 0;
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            pos += snprintf(buf + pos, max_len - pos, "%d.%d.%d.%d -> ",
                (arp_cache[i].ip >> 24) & 0xFF,
                (arp_cache[i].ip >> 16) & 0xFF,
                (arp_cache[i].ip >> 8) & 0xFF,
                arp_cache[i].ip & 0xFF);
            
            for (int j = 0; j < 6; j++) {
                pos += snprintf(buf + pos, max_len - pos, "%02x", arp_cache[i].mac[j]);
                if (j < 5) pos += snprintf(buf + pos, max_len - pos, ":");
            }
            pos += snprintf(buf + pos, max_len - pos, "\n");
        }
    }
    
    return pos;
}

void arp_send_request(u32 target_ip, u8 *src_mac, u32 src_ip) {
    arp_pkt_t arp;
    u8 broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    arp.htype = htons(ARP_HTYPE_ETHER);
    arp.ptype = htons(ARP_PTYPE_IP);
    arp.hlen = ARP_HLEN_ETHER;
    arp.plen = ARP_PLEN_IP;
    arp.op = htons(ARP_OP_REQUEST);
    memcpy(arp.sha, src_mac, 6);
    arp.spa = src_ip;
    memset(arp.tha, 0, 6);
    arp.tpa = target_ip;
    
    net_eth_send(broadcast, ETHERTYPE_ARP, (u8*)&arp, sizeof(arp_pkt_t));
}

void arp_handle_packet(u8 *packet, int len) {
    if (len < sizeof(arp_pkt_t)) return;
    
    arp_pkt_t *arp = (arp_pkt_t*)packet;
    
    vga_write("ARP: ");
    if (ntohs(arp->op) == ARP_OP_REQUEST) vga_write("REQUEST ");
    else vga_write("REPLY ");
    vga_write("from ");
    vga_write_num((arp->spa >> 24) & 0xFF);
    vga_write("."); vga_write_num((arp->spa >> 16) & 0xFF);
    vga_write("."); vga_write_num((arp->spa >> 8) & 0xFF);
    vga_write("."); vga_write_num(arp->spa & 0xFF);
    vga_write("\n");
    
    if (ntohs(arp->op) == ARP_OP_REQUEST && arp->tpa == net_get_ip()) {
        arp_pkt_t reply;
        reply.htype = htons(ARP_HTYPE_ETHER);
        reply.ptype = htons(ARP_PTYPE_IP);
        reply.hlen = ARP_HLEN_ETHER;
        reply.plen = ARP_PLEN_IP;
        reply.op = htons(ARP_OP_REPLY);
        memcpy(reply.sha, net_get_mac(), 6);
        reply.spa = net_get_ip();
        memcpy(reply.tha, arp->sha, 6);
        reply.tpa = arp->spa;
        
        net_eth_send(arp->sha, ETHERTYPE_ARP, (u8*)&reply, sizeof(arp_pkt_t));
        vga_write("ARP: sent reply\n");
    }
    
    arp_cache_add(arp->spa, arp->sha);
}
