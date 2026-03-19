#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

static u16 icmp_id = 0x1234;
static u16 icmp_seq = 0;
static u32 ping_target = 0;
static int ping_received = 0;
static int ping_tries = 0;

u16 icmp_checksum(u16 *data, int len) {
    u32 sum = 0;
    
    for (int i = 0; i < len / 2; i++) {
        sum += data[i];
    }
    
    if (len & 1) {
        sum += *((u8*)data + len - 1);
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

int icmp_send_request(u32 dst_ip, u16 id, u16 seq) {
    u8 packet[64];
    icmp_hdr_t *icmp = (icmp_hdr_t*)packet;
    
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = (id << 8) | (id >> 8);
    icmp->seq = (seq << 8) | (seq >> 8);
    icmp->checksum = 0;
    
    // Добавим немного данных для проверки
    char *data = (char*)(packet + sizeof(icmp_hdr_t));
    strcpy(data, "UTMS ping");
    
    int len = sizeof(icmp_hdr_t) + 9; // 9 байт данных
    
    icmp->checksum = icmp_checksum((u16*)packet, len);
    
    return ip_send_packet(dst_ip, IP_PROTO_ICMP, packet, len, net_get_mac(), net_get_ip());
}

void icmp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    if (len < sizeof(icmp_hdr_t)) return;
    
    icmp_hdr_t *icmp = (icmp_hdr_t*)packet;
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        // Отвечаем на ping
        icmp_hdr_t *reply = (icmp_hdr_t*)packet;
        reply->type = ICMP_ECHO_REPLY;
        reply->checksum = 0;
        reply->checksum = icmp_checksum((u16*)packet, len);
        
        ip_send_packet(src_ip, IP_PROTO_ICMP, packet, len, net_get_mac(), dst_ip);
    }
    else if (icmp->type == ICMP_ECHO_REPLY) {
        // Получили ответ на наш ping
        u16 id = (icmp->id << 8) | (icmp->id >> 8);
        u16 seq = (icmp->seq << 8) | (icmp->seq >> 8);
        
        if (src_ip == ping_target) {
            ping_received = 1;
            // УБИРАЕМ upac_print ОТСЮДА - это для ядра, не для upac
        }
    }
}

int icmp_ping(u32 dst_ip, int timeout_ms) {
    ping_target = dst_ip;
    ping_received = 0;
    
    for (int tries = 0; tries < 3; tries++) {
        icmp_send_request(dst_ip, icmp_id, icmp_seq++);
        
        // Ждем ответ - timeout_ms в миллисекундах
        int waited = 0;
        while (waited < timeout_ms) {
            for (int i = 0; i < 1000; i++) __asm__ volatile ("pause"); // ~1 мс
            if (ping_received) {
                return 0;
            }
            waited++;
        }
    }
    
    return -1;
}
