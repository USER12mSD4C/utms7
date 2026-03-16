#include "../include/string.h"
#include "../kernel/memory.h"
#include "ip.h"
#include "udp.h"

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
} dns_hdr_t;

typedef struct {
    u16 type;
    u16 class;
} dns_question_t;

typedef struct {
    u16 name_ptr;
    u16 type;
    u16 class;
    u32 ttl;
    u16 rdlength;
    u32 rdata;
} dns_answer_t;

static u16 dns_id = 0;

u16 dns_htons(u16 h) { return (h >> 8) | (h << 8); }
u32 dns_htonl(u32 h) { return (h >> 24) | ((h >> 8) & 0xFF00) | ((h << 8) & 0xFF0000) | (h << 24); }

int dns_build_query(const char *hostname, u8 *buf, int buflen) {
    dns_hdr_t *hdr = (dns_hdr_t*)buf;
    u8 *p = buf + sizeof(dns_hdr_t);
    int len = 0;
    
    memset(hdr, 0, sizeof(dns_hdr_t));
    hdr->id = dns_htons(dns_id++);
    hdr->flags = dns_htons(0x0100);
    hdr->qdcount = dns_htons(1);
    
    const char *dot = hostname;
    while (*dot) {
        const char *next = dot;
        while (*next && *next != '.') next++;
        
        *p++ = next - dot;
        memcpy(p, dot, next - dot);
        p += next - dot;
        
        dot = next;
        if (*dot == '.') dot++;
    }
    *p++ = 0;
    
    dns_question_t *q = (dns_question_t*)p;
    q->type = dns_htons(DNS_TYPE_A);
    q->class = dns_htons(DNS_CLASS_IN);
    
    len = (p - buf) + sizeof(dns_question_t);
    return len;
}

int dns_parse_response(u8 *buf, int len, u32 *ip) {
    dns_hdr_t *hdr = (dns_hdr_t*)buf;
    
    if (dns_htons(hdr->ancount) == 0) return -1;
    
    u8 *p = buf + sizeof(dns_hdr_t);
    
    while (*p) {
        if (*p & 0xC0) {
            p += 2;
            break;
        }
        p += *p + 1;
    }
    p += 1;
    
    p += sizeof(dns_question_t);
    
    for (int i = 0; i < dns_htons(hdr->ancount); i++) {
        if (*p & 0xC0) {
            p += 2;
        } else {
            while (*p) p += *p + 1;
            p += 1;
        }
        
        dns_answer_t *ans = (dns_answer_t*)p;
        
        if (dns_htons(ans->type) == DNS_TYPE_A && dns_htons(ans->class) == DNS_CLASS_IN) {
            *ip = ans->rdata;
            return 0;
        }
        
        p += sizeof(dns_answer_t) + dns_htons(ans->rdlength) - 4;
    }
    
    return -1;
}

u32 dns_lookup(const char *hostname, u32 dns_server) {
    u8 query[512];
    u8 response[512];
    
    int qlen = dns_build_query(hostname, query, sizeof(query));
    if (qlen <= 0) return 0;
    
    if (udp_send(dns_server, 12345, dns_server, DNS_PORT, query, qlen) < 0) return 0;
    
    u32 timeout = system_ticks + 50;
    while (system_ticks < timeout) {
        int rlen = udp_recv(response, sizeof(response));
        if (rlen > 0) {
            u32 ip;
            if (dns_parse_response(response, rlen, &ip) == 0) {
                return ip;
            }
        }
    }
    
    return 0;
}
