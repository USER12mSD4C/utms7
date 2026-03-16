#ifndef DHCP_H
#define DHCP_H

#include "../include/types.h"

void dhcp_start(void);
void dhcp_handle_packet(u8 *packet, int len);

#endif
