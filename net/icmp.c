// net/icmp.c
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"

static u16 icmp_id = 0x1234;
static u16 icmp_seq = 0;
static u32 ping_target = 0;
static int ping_received = 0;

static u16 icmp_checksum(u16 *data, int len) {
    u32 sum = 0;
    for (int i = 0; i < len / 2; i++) sum += data[i];
    if (len & 1) sum += *((u8*)data + len - 1);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

int icmp_send_request(u32 dst_ip, u16 id, u16 seq) {
    u8 packet[64];
    icmp_hdr_t *icmp = (icmp_hdr_t*)packet;
    
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);
    icmp->checksum = 0;
    
    char *data = (char*)(packet + sizeof(icmp_hdr_t));
    const char *msg = "UTMS ping";
    int msg_len = 9;
    memcpy(data, msg, msg_len);
    
    int len = sizeof(icmp_hdr_t) + msg_len;
    icmp->checksum = icmp_checksum((u16*)packet, len);
    
    return ip_send_packet(dst_ip, IP_PROTO_ICMP, packet, len);
}

void icmp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    (void)dst_ip;
    if (len < sizeof(icmp_hdr_t)) return;
    
    icmp_hdr_t *icmp = (icmp_hdr_t*)packet;
    u16 id = ntohs(icmp->id);
    u16 seq = ntohs(icmp->seq);
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = icmp_checksum((u16*)packet, len);
        ip_send_packet(src_ip, IP_PROTO_ICMP, packet, len);
    }
    else if (icmp->type == ICMP_ECHO_REPLY) {
        if (src_ip == ping_target && id == icmp_id) {
            ping_received = 1;
        }
    }
}

int icmp_ping(u32 dst_ip, int timeout_ms) {
    ping_target = dst_ip;
    ping_received = 0;
    
    for (int tries = 0; tries < 3; tries++) {
        icmp_send_request(dst_ip, icmp_id, icmp_seq++);
        
        int waited = 0;
        while (waited < timeout_ms) {
            for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
            if (ping_received) return 0;
            waited++;
        }
    }
    return -1;
}
