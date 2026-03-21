// net/dns.c
#include "dns.h"
#include "udp.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

#define DNS_PORT 53
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

typedef struct {
    u16 id;
    u16 flags;
    u16 qdcount;
    u16 ancount;
    u16 nscount;
    u16 arcount;
} __attribute__((packed)) dns_hdr_t;

typedef struct {
    u16 type;
    u16 class;
} __attribute__((packed)) dns_question_t;

static u16 dns_id = 0;

static int dns_build_query(const char *hostname, u8 *buf, int buflen) {
    dns_hdr_t *hdr = (dns_hdr_t*)buf;
    u8 *p = buf + sizeof(dns_hdr_t);
    const char *label;
    const char *next;
    int len;
    
    if (buflen < sizeof(dns_hdr_t) + strlen(hostname) + 2 + 4) return -1;
    
    memset(hdr, 0, sizeof(dns_hdr_t));
    hdr->id = htons(dns_id++);
    hdr->flags = htons(0x0100);
    hdr->qdcount = htons(1);
    
    label = hostname;
    while (*label) {
        next = label;
        while (*next && *next != '.') next++;
        len = next - label;
        
        *p++ = len;
        memcpy(p, label, len);
        p += len;
        
        label = next;
        if (*label == '.') label++;
    }
    *p++ = 0;
    
    dns_question_t *q = (dns_question_t*)p;
    q->type = htons(DNS_TYPE_A);
    q->class = htons(DNS_CLASS_IN);
    
    return (p - buf) + sizeof(dns_question_t);
}

static int dns_parse_response(u8 *buf, int len, u32 *ip) {
    dns_hdr_t *hdr = (dns_hdr_t*)buf;
    u8 *p = buf + sizeof(dns_hdr_t);
    u8 *end = buf + len;
    int ancount = ntohs(hdr->ancount);
    
    if (ancount == 0) return -1;
    
    while (p < end) {
        if (*p & 0xC0) {
            p += 2;
            break;
        }
        if (*p == 0) {
            p++;
            break;
        }
        p += *p + 1;
    }
    p += sizeof(dns_question_t);
    
    for (int i = 0; i < ancount && p < end; i++) {
        if (*p & 0xC0) {
            p += 2;
        } else {
            while (p < end && *p) {
                p += *p + 1;
            }
            p++;
        }
        
        if (p + 10 > end) break;
        
        u16 type = ntohs(*(u16*)(p));
        u16 class = ntohs(*(u16*)(p + 2));
        u16 rdlength = ntohs(*(u16*)(p + 8));
        
        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlength == 4) {
            *ip = *(u32*)(p + 10);
            return 0;
        }
        
        p += 10 + rdlength;
    }
    
    return -1;
}

u32 dns_lookup(const char *hostname, u32 dns_server) {
    u8 query[512];
    u8 response[512];
    u32 ip = 0;
    int a, b, c, d;
    
    if (sscanf(hostname, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        return (a << 24) | (b << 16) | (c << 8) | d;
    }
    
    u32 dns_servers[4];
    int dns_count = 0;
    
    if (dns_server != 0) dns_servers[dns_count++] = dns_server;
    dns_servers[dns_count++] = 0x08080808;
    dns_servers[dns_count++] = 0x01010101;
    dns_servers[dns_count++] = 0x09090909;
    
    int qlen = dns_build_query(hostname, query, sizeof(query));
    if (qlen <= 0) return 0;
    
    for (int s = 0; s < dns_count; s++) {
        for (int attempt = 0; attempt < 2; attempt++) {
            if (udp_send(dns_servers[s], 12345, DNS_PORT, query, qlen) < 0) {
                continue;
            }
            
            int waited = 0;
            while (waited < 2000) {
                int rlen = udp_recv(response, sizeof(response));
                if (rlen > 0) {
                    if (dns_parse_response(response, rlen, &ip) == 0) {
                        return ip;
                    }
                }
                for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
                waited++;
            }
        }
    }
    
    return 0;
}
