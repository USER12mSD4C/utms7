#ifndef RTL8139_H
#define RTL8139_H

#include "../include/types.h"
#include "../drivers/pci.h"

int rtl8139_init(pci_dev_t *pci);
void rtl8139_send(u8 *data, u16 len);
int rtl8139_recv(u8 *buffer, u16 max_len);
void rtl8139_get_mac(u8 *mac);
void rtl8139_handle_irq(void);

#endif
