#ifndef NET_ETHERNET_H
#define NET_ETHERNET_H

#define ETH_ALEN 6
#define ETHERTYPE_IP 0x0800
#define ETHERTYPE_ARP 0x0806

typedef struct {
    u8 dst[ETH_ALEN];
    u8 src[ETH_ALEN];
    u16 type;
} eth_hdr_t;

#endif
