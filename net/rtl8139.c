#include "../include/io.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/pci.h"
#include "ethernet.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL8139_REG_MAC0 0x00
#define RTL8139_REG_MAC4 0x04
#define RTL8139_REG_RBSTART 0x30
#define RTL8139_REG_CMD 0x37
#define RTL8139_REG_IMR 0x3C
#define RTL8139_REG_ISR 0x3E
#define RTL8139_REG_TXSTATUS0 0x10
#define RTL8139_REG_TXADDR0 0x20
#define RTL8139_REG_RX 0x3E
#define RTL8139_REG_CFG 0x52

#define RTL8139_CMD_RESET 0x10
#define RTL8139_CMD_RX_ENABLE 0x08
#define RTL8139_CMD_TX_ENABLE 0x04

#define RX_BUF_SIZE 8192
#define TX_BUF_SIZE 1536

typedef struct {
    u16 io_base;
    u8 mac[6];
    u8 *rx_buffer;
    u8 *tx_buffer[4];
    int tx_cur;
} rtl8139_t;

static rtl8139_t *rtl = NULL;

int rtl8139_init(pci_dev_t *pci) {
    rtl = kmalloc(sizeof(rtl8139_t));
    if (!rtl) return -1;
    
    rtl->io_base = pci->bar[0] & ~0x3;
    rtl->tx_cur = 0;
    
    outb(rtl->io_base + RTL8139_REG_CFG, 0x00);
    outb(rtl->io_base + RTL8139_REG_CMD, RTL8139_CMD_RESET);
    while (inb(rtl->io_base + RTL8139_REG_CMD) & RTL8139_CMD_RESET);
    
    for (int i = 0; i < 6; i++) {
        rtl->mac[i] = inb(rtl->io_base + RTL8139_REG_MAC0 + i);
    }
    
    rtl->rx_buffer = kmalloc(RX_BUF_SIZE + 16);
    if (!rtl->rx_buffer) {
        kfree(rtl);
        return -1;
    }
    rtl->rx_buffer = (u8*)(((u32)rtl->rx_buffer + 15) & ~0xF);
    
    for (int i = 0; i < 4; i++) {
        rtl->tx_buffer[i] = kmalloc(TX_BUF_SIZE);
        if (!rtl->tx_buffer[i]) return -1;
    }
    
    outl(rtl->io_base + RTL8139_REG_RBSTART, (u32)rtl->rx_buffer);
    
    outl(rtl->io_base + RTL8139_REG_IMR, 0xFFFF);
    
    outb(rtl->io_base + RTL8139_REG_CMD, 
         RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);
    
    outb(rtl->io_base + RTL8139_REG_CFG, 0x00);
    
    return 0;
}

void rtl8139_send(u8 *data, u16 len) {
    if (!rtl) return;
    
    int idx = rtl->tx_cur;
    memcpy(rtl->tx_buffer[idx], data, len);
    
    outl(rtl->io_base + RTL8139_REG_TXADDR0 + idx * 4, (u32)rtl->tx_buffer[idx]);
    outl(rtl->io_base + RTL8139_REG_TXSTATUS0 + idx * 4, len | 0x8000);
    
    rtl->tx_cur = (rtl->tx_cur + 1) % 4;
    
    while (inl(rtl->io_base + RTL8139_REG_TXSTATUS0 + idx * 4) & 0x8000);
}

int rtl8139_recv(u8 *buffer, u16 max_len) {
    if (!rtl) return 0;
    
    u16 status = inw(rtl->io_base + RTL8139_REG_ISR);
    if (!(status & 0x01)) return 0;
    
    u32 *rx_header = (u32*)rtl->rx_buffer;
    u16 pkt_len = (*rx_header >> 16) & 0x3FFF;
    u16 pkt_status = *rx_header & 0xFFFF;
    
    if (pkt_status & 0x0001) return -1;
    
    if (pkt_len > max_len) pkt_len = max_len;
    memcpy(buffer, rtl->rx_buffer + 4, pkt_len);
    
    u32 cap = inl(rtl->io_base + 0x38);
    cap += pkt_len + 4;
    cap &= ~0x3;
    cap |= 0x01;
    outl(rtl->io_base + 0x38, cap);
    
    return pkt_len;
}

void rtl8139_get_mac(u8 *mac) {
    if (rtl) memcpy(mac, rtl->mac, 6);
}

void rtl8139_handle_irq(void) {
    if (!rtl) return;
    
    u16 status = inw(rtl->io_base + RTL8139_REG_ISR);
    outw(rtl->io_base + RTL8139_REG_ISR, status);
    
    if (status & 0x01) {
        u8 packet[1514];
        int len = rtl8139_recv(packet, 1514);
        if (len > 0) {
            net_handle_packet(packet, len);
        }
    }
}
