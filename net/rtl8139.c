// net/rtl8139.c
#include "rtl8139.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/pci.h"
#include "../drivers/vga.h"

extern void net_handle_packet(u8 *packet, int len);

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

#define RTL_IDR0   0x00
#define RTL_RBSTART 0x30
#define RTL_CMD     0x37
#define RTL_IMR     0x3C
#define RTL_ISR     0x3E
#define RTL_TCR     0x40
#define RTL_RCR     0x44
#define RTL_TSAD    0x20
#define RTL_TSD     0x10

#define RTL_CMD_RESET   0x10
#define RTL_CMD_RX_ENB  0x08
#define RTL_CMD_TX_ENB  0x04

#define RTL_RCR_ACCEPT_ALL 0x0F

#define RX_BUF_SIZE 8192
#define TX_BUF_SIZE 1536

typedef struct {
    u16 iobase;
    u8 mac[6];
    u8 *rxbuf;
    u8 *txbuf[4];
    int tx_idx;
    int attached;
} rtl8139_softc_t;

static rtl8139_softc_t *sc = NULL;

static inline void rtl_write8(u16 reg, u8 v) { outb(sc->iobase + reg, v); }
static inline u8 rtl_read8(u16 reg) { return inb(sc->iobase + reg); }
static inline void rtl_write16(u16 reg, u16 v) { outw(sc->iobase + reg, v); }
static inline u16 rtl_read16(u16 reg) { return inw(sc->iobase + reg); }
static inline void rtl_write32(u16 reg, u32 v) { outl(sc->iobase + reg, v); }
static inline u32 rtl_read32(u16 reg) { return inl(sc->iobase + reg); }

int rtl8139_init(pci_dev_t *pci) {
    if (!pci) return -1;
    if (pci->vendor_id != RTL8139_VENDOR || pci->device_id != RTL8139_DEVICE) return -1;
    
    sc = kmalloc(sizeof(rtl8139_softc_t));
    if (!sc) return -1;
    memset(sc, 0, sizeof(rtl8139_softc_t));
    
    sc->iobase = pci->bar[0] & 0xFFFC;
    sc->tx_idx = 0;
    sc->attached = 1;
    
    vga_write("  RTL8139: IO base=0x"); vga_write_hex(sc->iobase); vga_write("\n");
    
    u32 cmd = pci_read_config(pci->bus, pci->slot, pci->func, 0x04);
    cmd |= 0x07;
    pci_write_config(pci->bus, pci->slot, pci->func, 0x04, cmd);
    
    rtl_write8(RTL_CMD, RTL_CMD_RESET);
    int timeout = 10000;
    while ((rtl_read8(RTL_CMD) & RTL_CMD_RESET) && timeout-- > 0) __asm__ volatile ("pause");
    if (timeout <= 0) goto fail;
    
    for (int i = 0; i < 6; i++) sc->mac[i] = rtl_read8(RTL_IDR0 + i);
    
    vga_write("  RTL8139: MAC ");
    for (int i = 0; i < 6; i++) { vga_write_hex(sc->mac[i]); if (i < 5) vga_write(":"); }
    vga_write("\n");
    
    sc->rxbuf = kmalloc(RX_BUF_SIZE + 256);
    if (!sc->rxbuf) goto fail;
    u64 rxaddr = (u64)sc->rxbuf;
    rxaddr = (rxaddr + 255) & ~255;
    sc->rxbuf = (u8*)rxaddr;
    
    for (int i = 0; i < 4; i++) {
        sc->txbuf[i] = kmalloc(TX_BUF_SIZE);
        if (!sc->txbuf[i]) goto fail;
    }
    
    rtl_write32(RTL_RBSTART, (u32)rxaddr);
    rtl_write32(RTL_RCR, RTL_RCR_ACCEPT_ALL);
    rtl_write32(RTL_TCR, 0x600000);
    rtl_write16(RTL_IMR, 0x0005);
    rtl_write16(RTL_ISR, 0xFFFF);
    rtl_write8(RTL_CMD, RTL_CMD_RX_ENB | RTL_CMD_TX_ENB);
    
    vga_write("  RTL8139: OK\n");
    return 0;
    
fail:
    if (sc->rxbuf) kfree(sc->rxbuf);
    for (int i = 0; i < 4; i++) if (sc->txbuf[i]) kfree(sc->txbuf[i]);
    kfree(sc);
    sc = NULL;
    return -1;
}

void rtl8139_dump_regs(void) {
    if (!sc) {
        vga_write("RTL8139: not attached\n");
        return;
    }
    
    vga_write("RTL8139 registers:\n");
    vga_write("  CMD: 0x"); vga_write_hex(rtl_read8(RTL_CMD)); vga_write("\n");
    vga_write("  ISR: 0x"); vga_write_hex(rtl_read16(RTL_ISR)); vga_write("\n");
    vga_write("  IMR: 0x"); vga_write_hex(rtl_read16(RTL_IMR)); vga_write("\n");
    
    for (int i = 0; i < 4; i++) {
        vga_write("  TSD"); vga_write_num(i); vga_write(": 0x");
        vga_write_hex(rtl_read32(RTL_TSD + i * 4));
        vga_write("\n");
    }
}

void rtl8139_send(u8 *data, u16 len) {
    if (!sc || !sc->attached || len == 0 || len > TX_BUF_SIZE) return;
    
    int idx = sc->tx_idx;
    int timeout = 100000;
    while ((rtl_read32(RTL_TSD + idx * 4) & 0x8000) && timeout-- > 0) __asm__ volatile ("pause");
    if (timeout <= 0) rtl_write32(RTL_TSD + idx * 4, 0);
    
    memcpy(sc->txbuf[idx], data, len);
    rtl_write32(RTL_TSAD + idx * 4, (u32)(u64)sc->txbuf[idx]);
    rtl_write32(RTL_TSD + idx * 4, len);
    
    sc->tx_idx = (sc->tx_idx + 1) % 4;
}

int rtl8139_recv(u8 *buffer, u16 max_len) {
    if (!sc || !sc->attached) return 0;
    u16 isr = rtl_read16(RTL_ISR);
    if (!(isr & 0x01)) return 0;
    rtl_write16(RTL_ISR, 0x01);
    
    u32 rx_status = *(u32*)sc->rxbuf;
    u16 pkt_len = (rx_status >> 16) & 0x3FFF;
    if (pkt_len == 0 || pkt_len > 1514 || (rx_status & 0x0001)) return 0;
    if (pkt_len > max_len) pkt_len = max_len;
    memcpy(buffer, sc->rxbuf + 4, pkt_len);
    
    u32 capr = rtl_read32(0x38);
    capr += pkt_len + 4;
    capr = (capr + 3) & ~3;
    rtl_write32(0x38, capr);
    return pkt_len;
}

void rtl8139_get_mac(u8 *mac) {
    if (sc && sc->attached) memcpy(mac, sc->mac, 6);
    else { mac[0]=0x52; mac[1]=0x54; mac[2]=0x00; mac[3]=0x12; mac[4]=0x34; mac[5]=0x56; }
}

void rtl8139_handle_irq(void) {
    if (!sc || !sc->attached) return;
    u16 isr = rtl_read16(RTL_ISR);
    rtl_write16(RTL_ISR, isr);
    if (isr & 0x01) {
        u8 packet[1514];
        int len = rtl8139_recv(packet, 1514);
        if (len > 0) net_handle_packet(packet, len);
    }
}

int rtl8139_present(void) { return (sc && sc->attached); }
