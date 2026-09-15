#ifndef E1000_H
#define E1000_H

#include "../include/types.h"
#include "../drivers/pci.h"

#define E1000_DEVICE_82540EM 0x100E
#define E1000_DEVICE_82579LM 0x1502

int e1000_init(pci_dev_t *pci);
void e1000_send(u8 *data, u16 len);
int e1000_recv(u8 *buffer, u16 max_len);
void e1000_get_mac(u8 *mac);
void e1000_handle_irq(void);
int e1000_present(void);

#endif
