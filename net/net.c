#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/pci.h"
#include "../drivers/rtl8139.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"

#define MAX_SOCKETS 64
#define MAX_PACKET_QUEUE 128

typedef struct {
    int used;
    int domain, type, protocol;
    int state;
    
    // Для TCP
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    u32 seq;
    u32 ack;
    
    // Очередь пакетов
    u8 *rx_queue[MAX_PACKET_QUEUE];
    int rx_queue_len[MAX_PACKET_QUEUE];
    int rx_head, rx_tail, rx_count;
} socket_t;

static socket_t sockets[MAX_SOCKETS];
static u8 our_mac[6];
static u32 our_ip;
static u32 gateway_ip;
static u32 netmask;

// ARP кэш
typedef struct {
    u32 ip;
    u8 mac[6];
    int valid;
} arp_cache_t;

static arp_cache_t arp_cache[16];
static int arp_cache_next = 0;

// Инициализация сети
void net_init(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) sockets[i].used = 0;
    
    // Ищем сетевуху
    pci_dev_t *pci = pci_find_device(0x10EC, 0x8139); // RTL8139
    if (pci) {
        rtl8139_init(pci);
        rtl8139_get_mac(our_mac);
    }
    
    // DHCP получение IP
    net_dhcp_request();
}

// Отправка ethernet кадра
void net_eth_send(u8 *dst_mac, u16 type, u8 *data, int len) {
    u8 *packet = kmalloc(len + sizeof(eth_hdr_t));
    eth_hdr_t *eth = (eth_hdr_t*)packet;
    
    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, our_mac, ETH_ALEN);
    eth->type = htons(type);
    
    memcpy(packet + sizeof(eth_hdr_t), data, len);
    
    rtl8139_send(packet, len + sizeof(eth_hdr_t));
    kfree(packet);
}

// ARP запрос
void net_arp_request(u32 target_ip) {
    arp_pkt_t arp;
    
    arp.htype = htons(ARP_HTYPE_ETHER);
    arp.ptype = htons(ARP_PTYPE_IP);
    arp.hlen = ARP_HLEN_ETHER;
    arp.plen = ARP_PLEN_IP;
    arp.op = htons(ARP_OP_REQUEST);
    
    memcpy(arp.sha, our_mac, ETH_ALEN);
    arp.spa = our_ip;
    memset(arp.tha, 0, ETH_ALEN);
    arp.tpa = target_ip;
    
    u8 broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_eth_send(broadcast, ETHERTYPE_ARP, (u8*)&arp, sizeof(arp_pkt_t));
}

// Добавление в ARP кэш
void net_arp_cache_add(u32 ip, u8 *mac) {
    for (int i = 0; i < 16; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }
    
    arp_cache[arp_cache_next].ip = ip;
    memcpy(arp_cache[arp_cache_next].mac, mac, ETH_ALEN);
    arp_cache[arp_cache_next].valid = 1;
    arp_cache_next = (arp_cache_next + 1) % 16;
}

// Поиск MAC по IP
int net_arp_lookup(u32 ip, u8 *mac) {
    for (int i = 0; i < 16; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, ETH_ALEN);
            return 0;
        }
    }
    return -1;
}

// Обработка ARP пакета
void net_arp_handle(u8 *packet, int len) {
    arp_pkt_t *arp = (arp_pkt_t*)packet;
    
    if (ntohs(arp->op) == ARP_OP_REQUEST) {
        if (arp->tpa == our_ip) {
            arp_pkt_t reply;
            
            reply.htype = htons(ARP_HTYPE_ETHER);
            reply.ptype = htons(ARP_PTYPE_IP);
            reply.hlen = ARP_HLEN_ETHER;
            reply.plen = ARP_PLEN_IP;
            reply.op = htons(ARP_OP_REPLY);
            
            memcpy(reply.sha, our_mac, ETH_ALEN);
            reply.spa = our_ip;
            memcpy(reply.tha, arp->sha, ETH_ALEN);
            reply.tpa = arp->spa;
            
            net_eth_send(arp->sha, ETHERTYPE_ARP, (u8*)&reply, sizeof(arp_pkt_t));
        }
    } else if (ntohs(arp->op) == ARP_OP_REPLY) {
        net_arp_cache_add(arp->spa, arp->sha);
    }
}

// Подсчет контрольной суммы IP
u16 ip_checksum(u16 *data, int len) {
    u32 sum = 0;
    for (int i = 0; i < len / 2; i++) sum += data[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

// Отправка IP пакета
int net_ip_send(u32 dst_ip, u8 protocol, u8 *data, int len) {
    u8 mac[6];
    if (net_arp_lookup(dst_ip, mac) != 0) {
        net_arp_request(dst_ip);
        return -1; // Повторить позже
    }
    
    int total_len = sizeof(ip_hdr_t) + len;
    u8 *packet = kmalloc(total_len);
    ip_hdr_t *ip = (ip_hdr_t*)packet;
    
    ip->ver_ihl = 0x45; // IPv4, IHL=5
    ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(1);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src = our_ip;
    ip->dst = dst_ip;
    
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    
    memcpy(packet + sizeof(ip_hdr_t), data, len);
    
    net_eth_send(mac, ETHERTYPE_IP, packet, total_len);
    kfree(packet);
    return len;
}

// Обработка IP пакета
void net_ip_handle(u8 *packet, int len) {
    ip_hdr_t *ip = (ip_hdr_t*)packet;
    
    if (ip->dst != our_ip && ip->dst != 0xFFFFFFFF) return;
    
    switch (ip->protocol) {
        case IP_PROTO_TCP:
            net_tcp_handle(packet + sizeof(ip_hdr_t), 
                          len - sizeof(ip_hdr_t), ip->src, ip->dst);
            break;
        case IP_PROTO_UDP:
            // UDP обработка
            break;
        case IP_PROTO_ICMP:
            // ICMP обработка (ping)
            break;
    }
}

// TCP псевдо-заголовок для контрольной суммы
typedef struct {
    u32 src;
    u32 dst;
    u8 zero;
    u8 protocol;
    u16 tcp_len;
} tcp_pseudo_t;

// Подсчет TCP контрольной суммы
u16 tcp_checksum(ip_hdr_t *ip, tcp_hdr_t *tcp, int tcp_len) {
    tcp_pseudo_t pseudo;
    pseudo.src = ip->src;
    pseudo.dst = ip->dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_len = htons(tcp_len);
    
    u32 sum = 0;
    u16 *p = (u16*)&pseudo;
    for (int i = 0; i < sizeof(pseudo)/2; i++) sum += p[i];
    
    p = (u16*)tcp;
    for (int i = 0; i < tcp_len/2; i++) sum += p[i];
    
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

// Отправка TCP пакета
int net_tcp_send(socket_t *s, u8 flags, u8 *data, int len) {
    int tcp_len = sizeof(tcp_hdr_t) + len;
    int ip_len = sizeof(ip_hdr_t) + tcp_len;
    
    u8 *packet = kmalloc(ip_len);
    ip_hdr_t *ip = (ip_hdr_t*)packet;
    tcp_hdr_t *tcp = (tcp_hdr_t*)(packet + sizeof(ip_hdr_t));
    
    // IP заголовок
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(ip_len);
    ip->id = htons(s->seq);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->checksum = 0;
    ip->src = s->local_ip;
    ip->dst = s->remote_ip;
    
    // TCP заголовок
    tcp->src_port = htons(s->local_port);
    tcp->dst_port = htons(s->remote_port);
    tcp->seq = htonl(s->seq);
    tcp->ack = htonl(s->ack);
    tcp->offset = (sizeof(tcp_hdr_t)/4) << 4;
    tcp->flags = flags;
    tcp->window = htons(65535);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;
    
    // Данные
    if (len > 0) memcpy(packet + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t), data, len);
    
    // Контрольная сумма
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    tcp->checksum = tcp_checksum(ip, tcp, tcp_len);
    
    // Отправка
    u8 mac[6];
    if (net_arp_lookup(s->remote_ip, mac) == 0) {
        net_eth_send(mac, ETHERTYPE_IP, packet, ip_len);
    }
    
    s->seq += len;
    if (flags & TCP_FLAG_SYN) s->seq++;
    if (flags & TCP_FLAG_FIN) s->seq++;
    
    kfree(packet);
    return len;
}

// Создание сокета
int net_socket_create(int domain, int type, int protocol) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            sockets[i].used = 1;
            sockets[i].domain = domain;
            sockets[i].type = type;
            sockets[i].protocol = protocol;
            sockets[i].state = TCP_STATE_CLOSED;
            sockets[i].local_ip = our_ip;
            sockets[i].local_port = 0;
            sockets[i].remote_ip = 0;
            sockets[i].remote_port = 0;
            sockets[i].seq = 1000 + i * 100;
            sockets[i].ack = 0;
            sockets[i].rx_head = 0;
            sockets[i].rx_tail = 0;
            sockets[i].rx_count = 0;
            return i;
        }
    }
    return -1;
}

// Подключение к удаленному хосту
int net_socket_connect(int sock, u32 ip, u16 port) {
    if (sock < 0 || sock >= MAX_SOCKETS || !sockets[sock].used) return -1;
    
    socket_t *s = &sockets[sock];
    s->remote_ip = ip;
    s->remote_port = port;
    s->local_port = 1024 + sock; // Временный порт
    
    // Отправляем SYN
    s->state = TCP_STATE_SYN_SENT;
    return net_tcp_send(s, TCP_FLAG_SYN, NULL, 0);
}

// Отправка данных
int net_socket_send(int sock, u8 *data, int len) {
    if (sock < 0 || sock >= MAX_SOCKETS || !sockets[sock].used) return -1;
    socket_t *s = &sockets[sock];
    if (s->state != TCP_STATE_ESTABLISHED) return -1;
    
    return net_tcp_send(s, TCP_FLAG_PSH | TCP_FLAG_ACK, data, len);
}

// Получение данных
int net_socket_recv(int sock, u8 *buf, int len) {
    if (sock < 0 || sock >= MAX_SOCKETS || !sockets[sock].used) return -1;
    socket_t *s = &sockets[sock];
    
    if (s->rx_count == 0) return 0;
    
    int total = 0;
    while (s->rx_count > 0 && total < len) {
        int idx = s->rx_head;
        int pkt_len = s->rx_queue_len[idx];
        int copy_len = (pkt_len < len - total) ? pkt_len : len - total;
        
        memcpy(buf + total, s->rx_queue[idx], copy_len);
        total += copy_len;
        
        if (copy_len == pkt_len) {
            kfree(s->rx_queue[idx]);
            s->rx_head = (s->rx_head + 1) % MAX_PACKET_QUEUE;
            s->rx_count--;
        } else {
            // Частичное чтение - оставляем остаток
            u8 *remaining = kmalloc(pkt_len - copy_len);
            memcpy(remaining, s->rx_queue[idx] + copy_len, pkt_len - copy_len);
            kfree(s->rx_queue[idx]);
            s->rx_queue[idx] = remaining;
            s->rx_queue_len[idx] = pkt_len - copy_len;
        }
    }
    
    return total;
}

// Обработка TCP пакета
void net_tcp_handle(u8 *packet, int len, u32 src_ip, u32 dst_ip) {
    tcp_hdr_t *tcp = (tcp_hdr_t*)packet;
    u16 src_port = ntohs(tcp->src_port);
    u16 dst_port = ntohs(tcp->dst_port);
    u32 seq = ntohl(tcp->seq);
    u32 ack = ntohl(tcp->ack);
    int data_len = len - (tcp->offset >> 4) * 4;
    u8 *data = packet + (tcp->offset >> 4) * 4;
    
    // Ищем сокет
    socket_t *s = NULL;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].used && 
            sockets[i].local_port == dst_port &&
            sockets[i].remote_ip == src_ip &&
            sockets[i].remote_port == src_port) {
            s = &sockets[i];
            break;
        }
    }
    if (!s) return;
    
    // Обновляем ACK
    if (tcp->flags & TCP_FLAG_ACK) {
        s->ack = ack;
    }
    
    // Обработка по состоянию
    switch (s->state) {
        case TCP_STATE_SYN_SENT:
            if (tcp->flags & TCP_FLAG_SYN && tcp->flags & TCP_FLAG_ACK) {
                s->state = TCP_STATE_ESTABLISHED;
                s->ack = seq + 1;
                net_tcp_send(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            if (data_len > 0) {
                // Сохраняем данные в очередь
                int idx = s->rx_tail;
                s->rx_queue[idx] = kmalloc(data_len);
                memcpy(s->rx_queue[idx], data, data_len);
                s->rx_queue_len[idx] = data_len;
                s->rx_tail = (s->rx_tail + 1) % MAX_PACKET_QUEUE;
                s->rx_count++;
                
                // Отправляем ACK
                s->ack = seq + data_len;
                net_tcp_send(s, TCP_FLAG_ACK, NULL, 0);
            }
            
            if (tcp->flags & TCP_FLAG_FIN) {
                s->state = TCP_STATE_CLOSE_WAIT;
                s->ack = seq + 1;
                net_tcp_send(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
    }
}

// DHCP клиент
void net_dhcp_request(void) {
    // Формируем DHCP Discover
    u8 dhcp_pkt[300];
    memset(dhcp_pkt, 0, sizeof(dhcp_pkt));
    
    // UDP заголовок
    // DHCP содержимое
    // Отправляем broadcast
}
