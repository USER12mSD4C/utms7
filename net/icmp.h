// net/icmp.h
#ifndef ICMP_H
#define ICMP_H

#include "../include/types.h"

void icmp_handle_packet(u8 *packet, int len, u32 src_ip, u32 dst_ip);
int icmp_ping(u32 dst_ip, int timeout_ms);
int icmp_send_request(u32 dst_ip, u16 id, u16 seq);

#endif
