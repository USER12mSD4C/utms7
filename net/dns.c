#include "dns.h"
#include "udp.h"
#include "../include/string.h"
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

static u16 dns_htons(u16 h) {
    return (h >> 8) | (h << 8);
}

static u32 dns_htonl(u32 h) {
    return (h >> 24) | ((h >> 8) & 0xFF00) | ((h << 8) & 0xFF0000) | (h << 24);
}

static int dns_build_query(const char *hostname, u8 *buf, int buflen) {
    dns_hdr_t *hdr = (dns_hdr_t*)buf;
    u8 *p = buf + sizeof(dns_hdr_t);
    const char *label;
    const char *next;
    int len;
    
    if (buflen < sizeof(dns_hdr_t) + strlen(hostname) + 2 + 4) return -1;
    
    memset(hdr, 0, sizeof(dns_hdr_t));
    hdr->id = dns_htons(dns_id++);
    hdr->flags = dns_htons(0x0100);
    hdr->qdcount = dns_htons(1);
    
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
    q->type = dns_htons(DNS_TYPE_A);
    q->class = dns_htons(DNS_CLASS_IN);
    
    return (p - buf) + sizeof(dns_question_t);
}

static int dns_parse_response(u8 *buf, int len, u32 *ip) {
    dns_hdr_t *hdr = (dns_hdr_t*)buf;
    u8 *p = buf + sizeof(dns_hdr_t);
    u8 *end = buf + len;
    int ancount = dns_htons(hdr->ancount);
    
    if (ancount == 0) return -1;
    
    vga_write("    DNS response has ");
    vga_write_num(ancount);
    vga_write(" answers\n");
    
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
        
        dns_answer_t *ans = (dns_answer_t*)p;
        u16 type = dns_htons(ans->type);
        u16 class = dns_htons(ans->class);
        u16 rdlength = dns_htons(ans->rdlength);
        
        vga_write("    Answer ");
        vga_write_num(i);
        vga_write(": type=");
        vga_write_num(type);
        vga_write(" class=");
        vga_write_num(class);
        vga_write(" rdlen=");
        vga_write_num(rdlength);
        vga_write("\n");
        
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
    
    vga_write("  DNS lookup for ");
    vga_write(hostname);
    vga_write("\n");
    
    if (sscanf(hostname, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        ip = (a << 24) | (b << 16) | (c << 8) | d;
        vga_write("  It's an IP: ");
        vga_write_num(a);
        vga_write(".");
        vga_write_num(b);
        vga_write(".");
        vga_write_num(c);
        vga_write(".");
        vga_write_num(d);
        vga_write("\n");
        return ip;
    }
    
    u32 dns_servers[4];
    int dns_count = 0;
    
    if (dns_server != 0) {
        dns_servers[dns_count++] = dns_server;
    }
    dns_servers[dns_count++] = 0x08080808;
    dns_servers[dns_count++] = 0x01010101;
    dns_servers[dns_count++] = 0x09090909;
    
    int qlen = dns_build_query(hostname, query, sizeof(query));
    if (qlen <= 0) return 0;
    
    for (int s = 0; s < dns_count; s++) {
        vga_write("  Trying DNS ");
        vga_write_num((dns_servers[s] >> 24) & 0xFF);
        vga_write(".");
        vga_write_num((dns_servers[s] >> 16) & 0xFF);
        vga_write(".");
        vga_write_num((dns_servers[s] >> 8) & 0xFF);
        vga_write(".");
        vga_write_num(dns_servers[s] & 0xFF);
        vga_write("\n");
        
        for (int attempt = 0; attempt < 2; attempt++) {
            vga_write("    Attempt ");
            vga_write_num(attempt);
            vga_write("\n");
            
            if (udp_send(dns_servers[s], 12345, dns_servers[s], DNS_PORT, query, qlen) < 0) {
                vga_write("    UDP send failed\n");
                continue;
            }
            
            int waited = 0;
            while (waited < 2000) {
                int rlen = udp_recv(response, sizeof(response));
                if (rlen > 0) {
                    vga_write("    Got response ");
                    vga_write_num(rlen);
                    vga_write(" bytes\n");
                    
                    if (dns_parse_response(response, rlen, &ip) == 0) {
                        vga_write("    Found IP: ");
                        vga_write_num((ip >> 24) & 0xFF);
                        vga_write(".");
                        vga_write_num((ip >> 16) & 0xFF);
                        vga_write(".");
                        vga_write_num((ip >> 8) & 0xFF);
                        vga_write(".");
                        vga_write_num(ip & 0xFF);
                        vga_write("\n");
                        return ip;
                    }
                }
                
                for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
                waited++;
            }
            vga_write("    Timeout\n");
        }
    }
    
    vga_write("  DNS lookup FAILED\n");
    return 0;
}
