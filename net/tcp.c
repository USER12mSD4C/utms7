// net/tcp.c
#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../kernel/sched.h"
#include "../drivers/vga.h"

#define MAX_SOCKETS 32
#define TCP_RETRANSMIT_MS 500

static tcp_socket_t sockets[MAX_SOCKETS];
static u16 next_port = 1024;
static u32 tcp_ticks = 0;

// Псевдозаголовок для checksum
typedef struct {
    u32 src_ip;
    u32 dst_ip;
    u8 zero;
    u8 proto;
    u16 len;
} __attribute__((packed)) tcp_pseudo_t;

// Вычисление checksum
static u16 tcp_checksum(u32 src_ip, u32 dst_ip, tcp_hdr_t *tcp, int len) {
    tcp_pseudo_t pseudo;
    u32 sum = 0;
    u16 *p;
    int i;
    
    pseudo.src_ip = src_ip;
    pseudo.dst_ip = dst_ip;
    pseudo.zero = 0;
    pseudo.proto = IP_PROTO_TCP;
    pseudo.len = htons(len);
    
    p = (u16*)&pseudo;
    for (i = 0; i < sizeof(pseudo)/2; i++) sum += p[i];
    
    p = (u16*)tcp;
    for (i = 0; i < len/2; i++) sum += p[i];
    
    if (len & 1) sum += *((u8*)tcp + len - 1);
    
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

// Отправка TCP пакета
static int tcp_send_packet(tcp_socket_t *s, u8 flags, u8 *data, int len) {
    int tcp_len = sizeof(tcp_hdr_t) + len;
    int total_len = sizeof(ip_hdr_t) + tcp_len;
    u8 *pkt = kmalloc(total_len);
    if (!pkt) return -1;
    
    ip_hdr_t *ip = (ip_hdr_t*)pkt;
    tcp_hdr_t *tcp = (tcp_hdr_t*)(pkt + sizeof(ip_hdr_t));
    
    // IP заголовок
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(s->snd_nxt & 0xFFFF);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->checksum = 0;
    ip->src = s->local_ip;
    ip->dst = s->remote_ip;
    
    // TCP заголовок
    tcp->src_port = htons(s->local_port);
    tcp->dst_port = htons(s->remote_port);
    tcp->seq = htonl(s->snd_nxt);
    tcp->ack = htonl(s->rcv_nxt);
    tcp->offset = (sizeof(tcp_hdr_t)/4) << 4;
    tcp->flags = flags;
    tcp->window = htons(s->rcv_wnd);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;
    
    if (len > 0 && data) {
        memcpy(pkt + sizeof(ip_hdr_t) + sizeof(tcp_hdr_t), data, len);
    }
    
    // Вычисляем checksums
    ip->checksum = ip_checksum((u16*)ip, sizeof(ip_hdr_t));
    tcp->checksum = tcp_checksum(s->local_ip, s->remote_ip, tcp, tcp_len);
    
    // Отправляем
    int res = ip_send_packet(s->remote_ip, IP_PROTO_TCP, pkt + sizeof(ip_hdr_t), tcp_len);
    
    // Обновляем состояние отправки
    if (res >= 0) {
        if (flags & TCP_FLAG_SYN) s->snd_nxt++;
        if (flags & TCP_FLAG_FIN) s->snd_nxt++;
        if (len > 0) s->snd_nxt += len;
        s->timestamp = tcp_ticks;
        s->retries = 0;
    }
    
    kfree(pkt);
    return res;
}

// Поиск сокета по fd
static tcp_socket_t* tcp_find_socket(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS) return NULL;
    if (!sockets[fd].used) return NULL;
    return &sockets[fd];
}

// Поиск сокета по соединению
static tcp_socket_t* tcp_find_connection(u32 local_ip, u16 local_port, 
                                          u32 remote_ip, u16 remote_port) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].used &&
            sockets[i].local_ip == local_ip &&
            sockets[i].local_port == local_port &&
            sockets[i].remote_ip == remote_ip &&
            sockets[i].remote_port == remote_port) {
            return &sockets[i];
        }
    }
    return NULL;
}

// Поиск слушающего сокета
static tcp_socket_t* tcp_find_listener(u16 port) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].used &&
            sockets[i].state == TCP_LISTEN &&
            sockets[i].local_port == port) {
            return &sockets[i];
        }
    }
    return NULL;
}

// Инициализация
void tcp_init(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        sockets[i].used = 0;
        sockets[i].rcv_buf = NULL;
        sockets[i].snd_buf = NULL;
    }
    vga_write("TCP initialized\n");
}

// Создание сокета
int tcp_socket(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            memset(&sockets[i], 0, sizeof(tcp_socket_t));
            sockets[i].used = 1;
            sockets[i].fd = i;
            sockets[i].state = TCP_CLOSED;
            sockets[i].local_ip = net_get_ip();
            sockets[i].local_port = next_port++;
            sockets[i].rcv_wnd = TCP_WINDOW;
            sockets[i].rcv_buf_size = 65536;
            sockets[i].rcv_buf = kmalloc(sockets[i].rcv_buf_size);
            sockets[i].snd_buf_size = 65536;
            sockets[i].snd_buf = kmalloc(sockets[i].snd_buf_size);
            
            if (!sockets[i].rcv_buf || !sockets[i].snd_buf) {
                if (sockets[i].rcv_buf) kfree(sockets[i].rcv_buf);
                if (sockets[i].snd_buf) kfree(sockets[i].snd_buf);
                sockets[i].used = 0;
                return -1;
            }
            
            return i;
        }
    }
    return -1;
}

// Привязка к порту
int tcp_bind(int fd, u16 port) {
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s || s->state != TCP_CLOSED) return -1;
    
    // Проверяем, не занят ли порт
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (i != fd && sockets[i].used && sockets[i].local_port == port) {
            return -1;
        }
    }
    
    s->local_port = port;
    return 0;
}

// Соединение
int tcp_connect(int fd, u32 ip, u16 port) {
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s || s->state != TCP_CLOSED) return -1;
    
    s->remote_ip = ip;
    s->remote_port = port;
    s->state = TCP_SYN_SENT;
    s->snd_nxt = (u32)((u64)s);
    s->rcv_nxt = 0;
    s->retries = 0;
    s->timestamp = tcp_ticks;
    
    return tcp_send_packet(s, TCP_FLAG_SYN, NULL, 0);
}

// Ожидание соединений
int tcp_listen(int fd, int backlog) {
    (void)backlog;
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s || s->state != TCP_CLOSED) return -1;
    
    s->state = TCP_LISTEN;
    return 0;
}

// Принятие соединения
int tcp_accept(int fd, u32 *client_ip, u16 *client_port) {
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s || s->state != TCP_LISTEN) return -1;
    
    // Ждём SYN
    int timeout = 10000;
    while (timeout-- > 0) {
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if (sockets[i].used && 
                sockets[i].state == TCP_SYN_RCVD &&
                sockets[i].local_port == s->local_port) {
                
                // Создаём новый сокет для клиента
                int new_fd = tcp_socket();
                if (new_fd < 0) return -1;
                
                tcp_socket_t *new_s = &sockets[new_fd];
                new_s->local_ip = sockets[i].local_ip;
                new_s->local_port = sockets[i].local_port;
                new_s->remote_ip = sockets[i].remote_ip;
                new_s->remote_port = sockets[i].remote_port;
                new_s->snd_nxt = sockets[i].snd_nxt;
                new_s->rcv_nxt = sockets[i].rcv_nxt;
                new_s->state = TCP_ESTABLISHED;
                
                if (client_ip) *client_ip = new_s->remote_ip;
                if (client_port) *client_port = new_s->remote_port;
                
                // Удаляем старый
                sockets[i].used = 0;
                kfree(sockets[i].rcv_buf);
                kfree(sockets[i].snd_buf);
                
                return new_fd;
            }
        }
        sched_sleep(10);
    }
    
    return -1;
}

// Отправка данных
int tcp_send(int fd, const u8 *data, int len) {
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s || s->state != TCP_ESTABLISHED) return -1;
    
    int sent = 0;
    while (sent < len) {
        int chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        
        int res = tcp_send_packet(s, TCP_FLAG_PSH | TCP_FLAG_ACK, 
                                   (u8*)data + sent, chunk);
        if (res < 0) return sent;
        
        sent += chunk;
        
        // Ждём ACK
        int timeout = 1000;
        while (timeout-- > 0) {
            if (s->snd_una >= s->snd_nxt) break;
            sched_sleep(10);
        }
    }
    
    return sent;
}

// Приём данных
int tcp_recv(int fd, u8 *buf, int len) {
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s) return -1;
    
    if (s->state == TCP_CLOSE_WAIT && s->rcv_buf_count == 0) {
        return 0;  // EOF
    }
    
    // Ждём данные
    int timeout = 10000;
    while (s->rcv_buf_count == 0 && s->state == TCP_ESTABLISHED && timeout-- > 0) {
        sched_sleep(10);
    }
    
    if (s->rcv_buf_count == 0) return -1;
    
    int total = 0;
    while (s->rcv_buf_count > 0 && total < len) {
        int avail = s->rcv_buf_count;
        int copy = avail > (len - total) ? (len - total) : avail;
        
        if (s->rcv_buf_head + copy <= s->rcv_buf_size) {
            memcpy(buf + total, s->rcv_buf + s->rcv_buf_head, copy);
        } else {
            int first = s->rcv_buf_size - s->rcv_buf_head;
            memcpy(buf + total, s->rcv_buf + s->rcv_buf_head, first);
            memcpy(buf + total + first, s->rcv_buf, copy - first);
        }
        
        total += copy;
        s->rcv_buf_head = (s->rcv_buf_head + copy) % s->rcv_buf_size;
        s->rcv_buf_count -= copy;
        s->rcv_nxt += copy;
    }
    
    // Отправляем ACK
    if (total > 0) {
        tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
    }
    
    return total;
}

// Закрытие сокета
int tcp_close(int fd) {
    tcp_socket_t *s = tcp_find_socket(fd);
    if (!s) return -1;
    
    if (s->state == TCP_ESTABLISHED) {
        s->state = TCP_FIN_WAIT_1;
        tcp_send_packet(s, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        
        int timeout = 1000;
        while (s->state != TCP_CLOSED && timeout-- > 0) {
            sched_sleep(10);
        }
    }
    
    if (s->rcv_buf) kfree(s->rcv_buf);
    if (s->snd_buf) kfree(s->snd_buf);
    s->used = 0;
    
    return 0;
}

// Таймер для ретрансмиссии
void tcp_timer(void) {
    tcp_ticks++;
    
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) continue;
        tcp_socket_t *s = &sockets[i];
        
        // Ретрансмиссия для SYN_SENT и ESTABLISHED
        if ((s->state == TCP_SYN_SENT || s->state == TCP_ESTABLISHED) &&
            s->snd_una < s->snd_nxt &&
            tcp_ticks - s->timestamp > TCP_RETRANSMIT_MS) {
            
            if (s->retries < TCP_RETRIES) {
                s->retries++;
                s->timestamp = tcp_ticks;
                
                if (s->state == TCP_SYN_SENT) {
                    tcp_send_packet(s, TCP_FLAG_SYN, NULL, 0);
                } else {
                    tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
                }
            } else {
                // Превышено число попыток
                s->state = TCP_CLOSED;
            }
        }
    }
}

// Обработка входящих пакетов
void tcp_handle_packet(u8 *pkt, int len, u32 src_ip, u32 dst_ip) {
    if (len < sizeof(tcp_hdr_t)) return;
    
    tcp_hdr_t *tcp = (tcp_hdr_t*)pkt;
    u16 src_port = ntohs(tcp->src_port);
    u16 dst_port = ntohs(tcp->dst_port);
    u32 seq = ntohl(tcp->seq);
    u32 ack = ntohl(tcp->ack);
    int hlen = (tcp->offset >> 4) * 4;
    int dlen = len - hlen;
    u8 *data = pkt + hlen;
    u8 flags = tcp->flags;
    
    // Поиск существующего соединения
    tcp_socket_t *s = tcp_find_connection(dst_ip, dst_port, src_ip, src_port);
    
    // Если нет — возможно, это SYN для слушающего сокета
    if (!s && (flags & TCP_FLAG_SYN)) {
        tcp_socket_t *listener = tcp_find_listener(dst_port);
        if (listener) {
            // Создаём временный сокет для SYN_RCVD
            for (int i = 0; i < MAX_SOCKETS; i++) {
                if (!sockets[i].used) {
                    s = &sockets[i];
                    memset(s, 0, sizeof(tcp_socket_t));
                    s->used = 1;
                    s->fd = i;
                    s->local_ip = dst_ip;
                    s->local_port = dst_port;
                    s->remote_ip = src_ip;
                    s->remote_port = src_port;
                    s->rcv_nxt = seq + 1;
                    s->snd_nxt = (u32)((u64)s);
                    s->state = TCP_SYN_RCVD;
                    s->rcv_wnd = TCP_WINDOW;
                    s->rcv_buf_size = 65536;
                    s->rcv_buf = kmalloc(s->rcv_buf_size);
                    s->snd_buf_size = 65536;
                    s->snd_buf = kmalloc(s->snd_buf_size);
                    
                    // Отправляем SYN+ACK
                    tcp_send_packet(s, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                    break;
                }
            }
            return;
        }
        return;
    }
    
    if (!s) return;
    
    // Обновляем ACK
    if (flags & TCP_FLAG_ACK) {
        if (ack > s->snd_una && ack <= s->snd_nxt) {
            s->snd_una = ack;
        }
    }
    
    // Обработка данных
    if (dlen > 0 && seq == s->rcv_nxt) {
        // Добавляем в буфер
        int space = s->rcv_buf_size - s->rcv_buf_count;
        if (dlen > space) dlen = space;
        
        if (dlen > 0) {
            int tail = s->rcv_buf_tail;
            if (tail + dlen <= s->rcv_buf_size) {
                memcpy(s->rcv_buf + tail, data, dlen);
                s->rcv_buf_tail += dlen;
            } else {
                int first = s->rcv_buf_size - tail;
                memcpy(s->rcv_buf + tail, data, first);
                memcpy(s->rcv_buf, data + first, dlen - first);
                s->rcv_buf_tail = dlen - first;
            }
            s->rcv_buf_count += dlen;
            s->rcv_nxt += dlen;
        }
        
        // Отправляем ACK
        tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
    }
    
    // Обработка состояний
    switch (s->state) {
        case TCP_SYN_SENT:
            if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
                s->state = TCP_ESTABLISHED;
                s->rcv_nxt = seq + 1;
                s->snd_una = ack;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_SYN_RCVD:
            if (flags & TCP_FLAG_ACK) {
                s->state = TCP_ESTABLISHED;
            }
            break;
            
        case TCP_ESTABLISHED:
            if (flags & TCP_FLAG_FIN) {
                s->state = TCP_CLOSE_WAIT;
                s->rcv_nxt = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_FIN_WAIT_1:
            if (flags & TCP_FLAG_ACK && ack == s->snd_nxt) {
                s->state = TCP_FIN_WAIT_2;
            }
            if (flags & TCP_FLAG_FIN) {
                s->state = TCP_CLOSING;
                s->rcv_nxt = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_FIN_WAIT_2:
            if (flags & TCP_FLAG_FIN) {
                s->state = TCP_TIME_WAIT;
                s->rcv_nxt = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_CLOSE_WAIT:
            if (flags & TCP_FLAG_FIN) {
                s->state = TCP_LAST_ACK;
                s->rcv_nxt = seq + 1;
                tcp_send_packet(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_LAST_ACK:
            if (flags & TCP_FLAG_ACK && ack == s->snd_nxt) {
                s->state = TCP_CLOSED;
            }
            break;
            
        case TCP_TIME_WAIT:
            if (tcp_ticks - s->timestamp > 2000) {
                s->state = TCP_CLOSED;
            }
            break;
    }
}
