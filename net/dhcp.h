#ifndef DHCP_H
#define DHCP_H

#include "../include/types.h"

int dhcp_request(void);
void dhcp_handle_packet(u8 *packet, int len);

#endif
