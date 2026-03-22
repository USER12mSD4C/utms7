// net/icmp.c
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"

static u16 icmp_id = 0x1234;
static u16 icmp_seq = 0;
static u32 ping_target = 0;
int icmp_ping_received = 0;

static u16 icmp_checksum(u16 *data, int len) {
    u32 sum = 0;
    for (int i = 0; i < len / 2; i++) sum += data[i];
    if (len & 1) sum += *((u8*)data + len - 1);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

int icmp_send_request(u32 dst, u16 id, u16 seq) {
    u8 pkt[64];
    icmp_hdr_t *icmp = (icmp_hdr_t*)pkt;
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);
    icmp->checksum = 0;
    
    char *data = (char*)(pkt + sizeof(icmp_hdr_t));
    memcpy(data, "UTMS ping", 9);
    int len = sizeof(icmp_hdr_t) + 9;
    icmp->checksum = icmp_checksum((u16*)pkt, len);
    return ip_send_packet(dst, IP_PROTO_ICMP, pkt, len);
}

void icmp_handle_packet(u8 *pkt, int len, u32 src, u32 dst) {
    (void)dst;
    if (len < sizeof(icmp_hdr_t)) return;
    icmp_hdr_t *icmp = (icmp_hdr_t*)pkt;
    u16 id = ntohs(icmp->id);
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = icmp_checksum((u16*)pkt, len);
        ip_send_packet(src, IP_PROTO_ICMP, pkt, len);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        if (src == ping_target && id == icmp_id) icmp_ping_received = 1;
    }
}

int icmp_ping(u32 dst, int timeout) {
    ping_target = dst;
    icmp_ping_received = 0;
    icmp_send_request(dst, icmp_id, icmp_seq++);
    int waited = 0;
    while (waited < timeout) {
        if (icmp_ping_received) return 0;
        for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
        waited++;
    }
    return -1;
}
