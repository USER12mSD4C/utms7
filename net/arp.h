#ifndef ARP_H
#define ARP_H

#include "../include/types.h"

#define ARP_HTYPE_ETHER 1
#define ARP_PTYPE_IP 0x0800
#define ARP_HLEN_ETHER 6
#define ARP_PLEN_IP 4
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

typedef struct {
    u16 htype;
    u16 ptype;
    u8 hlen;
    u8 plen;
    u16 op;
    u8 sha[6];
    u32 spa;
    u8 tha[6];
    u32 tpa;
} arp_pkt_t;

void arp_cache_init(void);
int arp_cache_lookup(u32 ip, u8 *mac);
void arp_cache_add(u32 ip, u8 *mac);
void arp_send_request(u32 target_ip, u8 *src_mac, u32 src_ip);
void arp_handle_packet(u8 *packet, int len);

#endif
