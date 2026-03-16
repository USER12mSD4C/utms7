#include "http.h"
#include "tcp.h"
#include "dns.h"
#include "net.h"
#include "../kernel/memory.h"
#include "../include/string.h"

typedef struct {
    char host[256];
    char path[256];
    u16 port;
} url_t;

static int parse_url(const char *url_str, url_t *url) {
    const char *p = url_str;
    
    if (strncmp(p, "http://", 7) == 0) {
        url->port = 80;
        p += 7;
    } else {
        return -1;
    }
    
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) {
        url->host[i++] = *p++;
    }
    url->host[i] = '\0';
    
    if (*p == ':') {
        p++;
        url->port = 0;
        while (*p >= '0' && *p <= '9') {
            url->port = url->port * 10 + (*p - '0');
            p++;
        }
    }
    
    i = 0;
    url->path[i++] = '/';
    while (*p && i < 255) {
        url->path[i++] = *p++;
    }
    url->path[i] = '\0';
    
    return 0;
}

int http_get(const char *url_str, u8 **response, u32 *resp_len) {
    url_t url;
    if (parse_url(url_str, &url) != 0) return -1;
    
    u32 ip = dns_lookup(url.host, net_get_dns());
    if (ip == 0) return -1;
    
    int sock = tcp_socket_create();
    if (sock < 0) return -1;
    
    if (tcp_connect(sock, ip, url.port) != 0) return -1;
    
    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", url.path, url.host);
    
    if (tcp_send(sock, (u8*)request, req_len) < 0) {
        return -1;
    }
    
    u8 *buf = kmalloc(65536);
    int total = 0;
    
    while (1) {
        int r = tcp_recv(sock, buf + total, 4096);
        if (r <= 0) break;
        total += r;
        if (total >= 65536) break;
    }
    
    *response = buf;
    *resp_len = total;
    
    return 0;
}
