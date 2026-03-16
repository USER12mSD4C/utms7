#include "../include/string.h"
#include "../kernel/memory.h"
#include "ip.h"
#include "udp.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_COOKIE 0x63825363

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

typedef struct {
    u8 op;
    u8 htype;
    u8 hlen;
    u8 hops;
    u32 xid;
    u16 secs;
    u16 flags;
    u32 ciaddr;
    u32 yiaddr;
    u32 siaddr;
    u32 giaddr;
    u8 chaddr[16];
    u8 sname[64];
    u8 file[128];
    u32 magic;
    u8 options[308];
} dhcp_pkt_t;

static u32 dhcp_xid = 0x12345678;
static u32 dhcp_server = 0;
static u32 dhcp_lease = 86400;

int dhcp_send_discover(u8 *mac, u32 xid) {
    dhcp_pkt_t pkt;
    u8 *opt;
    
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = ip_htonl(xid);
    memcpy(pkt.chaddr, mac, 6);
    pkt.magic = ip_htonl(DHCP_MAGIC_COOKIE);
    
    opt = pkt.options;
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = DHCP_DISCOVER;
    
    *opt++ = 55;
    *opt++ = 4;
    *opt++ = 1;
    *opt++ = 3;
    *opt++ = 6;
    *opt++ = 15;
    
    *opt++ = 255;
    
    return udp_send(0xFFFFFFFF, DHCP_CLIENT_PORT, 0xFFFFFFFF, DHCP_SERVER_PORT, (u8*)&pkt, opt - (u8*)&pkt);
}

int dhcp_send_request(u8 *mac, u32 xid, u32 requested_ip, u32 server_ip) {
    dhcp_pkt_t pkt;
    u8 *opt;
    
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = ip_htonl(xid);
    memcpy(pkt.chaddr, mac, 6);
    pkt.magic = ip_htonl(DHCP_MAGIC_COOKIE);
    
    opt = pkt.options;
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = DHCP_REQUEST;
    
    *opt++ = 50;
    *opt++ = 4;
    *opt++ = (requested_ip >> 24) & 0xFF;
    *opt++ = (requested_ip >> 16) & 0xFF;
    *opt++ = (requested_ip >> 8) & 0xFF;
    *opt++ = requested_ip & 0xFF;
    
    *opt++ = 54;
    *opt++ = 4;
    *opt++ = (server_ip >> 24) & 0xFF;
    *opt++ = (server_ip >> 16) & 0xFF;
    *opt++ = (server_ip >> 8) & 0xFF;
    *opt++ = server_ip & 0xFF;
    
    *opt++ = 255;
    
    return udp_send(0xFFFFFFFF, DHCP_CLIENT_PORT, 0xFFFFFFFF, DHCP_SERVER_PORT, (u8*)&pkt, opt - (u8*)&pkt);
}

void dhcp_handle_packet(u8 *packet, int len) {
    dhcp_pkt_t *pkt = (dhcp_pkt_t*)packet;
    
    if (len < sizeof(dhcp_pkt_t) - sizeof(pkt->options)) return;
    if (ip_htonl(pkt->magic) != DHCP_MAGIC_COOKIE) return;
    
    u8 *opt = pkt->options;
    u8 *end = (u8*)packet + len;
    u8 msg_type = 0;
    u32 server_ip = 0;
    
    while (opt < end && *opt != 255) {
        u8 code = *opt++;
        u8 length = *opt++;
        
        if (code == 53) msg_type = *opt;
        if (code == 54 && length == 4) {
            server_ip = (opt[0] << 24) | (opt[1] << 16) | (opt[2] << 8) | opt[3];
        }
        
        opt += length;
    }
    
    if (msg_type == DHCP_OFFER && pkt->yiaddr != 0) {
        dhcp_server = server_ip;
        dhcp_send_request(net_get_mac(), ip_htonl(pkt->xid), pkt->yiaddr, server_ip);
    }
    
    if (msg_type == DHCP_ACK && pkt->yiaddr != 0) {
        net_set_ip(pkt->yiaddr);
        net_set_server(dhcp_server);
        
        opt = pkt->options;
        while (opt < end && *opt != 255) {
            u8 code = *opt++;
            u8 length = *opt++;
            
            if (code == 1) {
                u32 mask = (opt[0] << 24) | (opt[1] << 16) | (opt[2] << 8) | opt[3];
                net_set_netmask(mask);
            }
            if (code == 3) {
                u32 gateway = (opt[0] << 24) | (opt[1] << 16) | (opt[2] << 8) | opt[3];
                net_set_gateway(gateway);
            }
            if (code == 6) {
                u32 dns = (opt[0] << 24) | (opt[1] << 16) | (opt[2] << 8) | opt[3];
                net_set_dns(dns);
            }
            if (code == 51 && length == 4) {
                dhcp_lease = (opt[0] << 24) | (opt[1] << 16) | (opt[2] << 8) | opt[3];
            }
            
            opt += length;
        }
    }
}

void dhcp_start(void) {
    dhcp_send_discover(net_get_mac(), dhcp_xid);
}
