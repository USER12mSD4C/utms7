// net/icmp.c
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

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
    
    vga_write("ICMP: sending request to ");
    vga_write_num((dst_ip >> 24) & 0xFF);
    vga_write("."); vga_write_num((dst_ip >> 16) & 0xFF);
    vga_write("."); vga_write_num((dst_ip >> 8) & 0xFF);
    vga_write("."); vga_write_num(dst_ip & 0xFF);
    vga_write("\n");
    
    return ip_send_packet(dst_ip, IP_PROTO_ICMP, packet, len);
}

void icmp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    (void)dst_ip;
    if (len < sizeof(icmp_hdr_t)) return;
    
    icmp_hdr_t *icmp = (icmp_hdr_t*)packet;
    u16 id = ntohs(icmp->id);
    u16 seq = ntohs(icmp->seq);
    
    vga_write("ICMP: received type=");
    vga_write_num(icmp->type);
    vga_write(" from ");
    vga_write_num((src_ip >> 24) & 0xFF);
    vga_write("."); vga_write_num((src_ip >> 16) & 0xFF);
    vga_write("."); vga_write_num((src_ip >> 8) & 0xFF);
    vga_write("."); vga_write_num(src_ip & 0xFF);
    vga_write("\n");
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        vga_write("ICMP: echo request, sending reply\n");
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = icmp_checksum((u16*)packet, len);
        ip_send_packet(src_ip, IP_PROTO_ICMP, packet, len);
    }
    else if (icmp->type == ICMP_ECHO_REPLY) {
        vga_write("ICMP: echo reply received\n");
        if (src_ip == ping_target && id == icmp_id) {
            icmp_ping_received = 1;
        }
    }
}

int icmp_ping(u32 dst_ip, int timeout_ms) {
    ping_target = dst_ip;
    icmp_ping_received = 0;
    
    vga_write("ICMP: ping ");
    vga_write_num((dst_ip >> 24) & 0xFF);
    vga_write("."); vga_write_num((dst_ip >> 16) & 0xFF);
    vga_write("."); vga_write_num((dst_ip >> 8) & 0xFF);
    vga_write("."); vga_write_num(dst_ip & 0xFF);
    vga_write("\n");
    
    icmp_send_request(dst_ip, icmp_id, icmp_seq++);
    
    int waited = 0;
    while (waited < timeout_ms) {
        if (icmp_ping_received) return 0;
        for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
        waited++;
    }
    return -1;
}
