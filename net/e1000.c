// net/e1000.c
#include "e1000.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../kernel/paging.h"
#include "../drivers/pci.h"
#include "../drivers/vga.h"

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

#define E1000_CTRL     0x0000
#define E1000_STATUS   0x0008
#define E1000_EECD     0x0010
#define E1000_EERD     0x0014
#define E1000_CTRL_EXT 0x0018
#define E1000_ICR      0x00C0
#define E1000_IMS      0x00D0
#define E1000_IMC      0x00D8
#define E1000_RCTL     0x0100
#define E1000_TCTL     0x0400
#define E1000_TIPG     0x0410
#define E1000_RDBAL    0x2800
#define E1000_RDBAH    0x2804
#define E1000_RDLEN    0x2808
#define E1000_RDH      0x2810
#define E1000_RDT      0x2818
#define E1000_TDBAL    0x3800
#define E1000_TDBAH    0x3804
#define E1000_TDLEN    0x3808
#define E1000_TDH      0x3810
#define E1000_TDT      0x3818
#define E1000_RA       0x5400

#define E1000_CTRL_RST     0x04000000
#define E1000_CTRL_SLU     0x40000000
#define E1000_RCTL_EN      0x00000002
#define E1000_RCTL_BAM     0x00008000
#define E1000_RCTL_SECRC   0x04000000
#define E1000_TCTL_EN      0x00000002
#define E1000_TCTL_PSP     0x00000008

#define RX_DESC_COUNT 256
#define TX_DESC_COUNT 256
#define RX_BUFFER_SIZE 2048
#define TX_BUFFER_SIZE 1518
#define MMIO_SIZE 0x20000

typedef struct {
    u64 addr;
    u16 length;
    u16 csum;
    u8 status;
    u8 errors;
    u16 special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    u64 addr;
    u16 length;
    u8 cso;
    u8 status;
    u8 css;
    u16 special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct {
    u32 mmio_base;
    u8 mac[6];
    u8 *rx_buf[RX_DESC_COUNT];
    u8 *tx_buf[TX_DESC_COUNT];
    e1000_rx_desc_t *rx_desc;
    e1000_tx_desc_t *tx_desc;
    int rx_cur;
    int tx_cur;
    int attached;
} e1000_softc_t;

static e1000_softc_t *sc = NULL;

static inline u32 e1000_read32(u32 reg) {
    return *(volatile u32*)(sc->mmio_base + reg);
}

static inline void e1000_write32(u32 reg, u32 val) {
    *(volatile u32*)(sc->mmio_base + reg) = val;
}

static void e1000_read_mac(void) {
    u32 rar_low = e1000_read32(E1000_RA);
    u32 rar_high = e1000_read32(E1000_RA + 4);
    
    sc->mac[0] = rar_low & 0xFF;
    sc->mac[1] = (rar_low >> 8) & 0xFF;
    sc->mac[2] = (rar_low >> 16) & 0xFF;
    sc->mac[3] = (rar_low >> 24) & 0xFF;
    sc->mac[4] = rar_high & 0xFF;
    sc->mac[5] = (rar_high >> 8) & 0xFF;
    
    if (sc->mac[0] == 0 && sc->mac[1] == 0 && sc->mac[2] == 0 &&
        sc->mac[3] == 0 && sc->mac[4] == 0 && sc->mac[5] == 0) {
        for (int i = 0; i < 3; i++) {
            e1000_write32(E1000_EERD, (i << 8) | 1);
            u32 val = e1000_read32(E1000_EERD);
            sc->mac[i*2] = (val >> 8) & 0xFF;
            sc->mac[i*2+1] = (val >> 0) & 0xFF;
        }
    }
    
    if (sc->mac[0] == 0 && sc->mac[1] == 0 && sc->mac[2] == 0 &&
        sc->mac[3] == 0 && sc->mac[4] == 0 && sc->mac[5] == 0) {
        sc->mac[0] = 0x52;
        sc->mac[1] = 0x54;
        sc->mac[2] = 0x00;
        sc->mac[3] = 0x12;
        sc->mac[4] = 0x34;
        sc->mac[5] = 0x56;
    }
}

static int e1000_map_mmio(void) {
    u64 phys = sc->mmio_base;
    u64 virt = phys;
    
    for (u64 offset = 0; offset < MMIO_SIZE; offset += 4096) {
        if (paging_map(phys + offset, virt + offset, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            return -1;
        }
    }
    
    return 0;
}

int e1000_init(pci_dev_t *pci) {
    if (!pci) return -1;
    if (pci->vendor_id != E1000_VENDOR || pci->device_id != E1000_DEVICE) return -1;
    
    sc = kmalloc(sizeof(e1000_softc_t));
    if (!sc) return -1;
    memset(sc, 0, sizeof(e1000_softc_t));
    
    sc->mmio_base = pci->bar[0] & 0xFFFFFFF0;
    sc->rx_cur = 0;
    sc->tx_cur = 0;
    sc->attached = 0;
    
    vga_write("  E1000: MMIO base=0x");
    vga_write_hex(sc->mmio_base);
    vga_write("\n");
    
    if (e1000_map_mmio() != 0) {
        vga_write("  E1000: MMIO mapping failed\n");
        kfree(sc);
        sc = NULL;
        return -1;
    }
    
    u32 cmd = pci_read_config(pci->bus, pci->slot, pci->func, 0x04);
    cmd |= 0x07;
    pci_write_config(pci->bus, pci->slot, pci->func, 0x04, cmd);
    
    e1000_write32(E1000_CTRL, E1000_CTRL_RST);
    int timeout = 100000;
    while ((e1000_read32(E1000_CTRL) & E1000_CTRL_RST) && timeout-- > 0) {
        __asm__ volatile ("pause");
    }
    if (timeout <= 0) {
        vga_write("  E1000: reset timeout\n");
        kfree(sc);
        sc = NULL;
        return -1;
    }
    
    e1000_read_mac();
    
    vga_write("  E1000: MAC ");
    for (int i = 0; i < 6; i++) {
        vga_write_hex(sc->mac[i]);
        if (i < 5) vga_write(":");
    }
    vga_write("\n");
    
    sc->rx_desc = kmalloc(sizeof(e1000_rx_desc_t) * RX_DESC_COUNT);
    sc->tx_desc = kmalloc(sizeof(e1000_tx_desc_t) * TX_DESC_COUNT);
    if (!sc->rx_desc || !sc->tx_desc) goto fail;
    
    memset(sc->rx_desc, 0, sizeof(e1000_rx_desc_t) * RX_DESC_COUNT);
    memset(sc->tx_desc, 0, sizeof(e1000_tx_desc_t) * TX_DESC_COUNT);
    
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        sc->rx_buf[i] = kmalloc(RX_BUFFER_SIZE);
        if (!sc->rx_buf[i]) goto fail;
        sc->rx_desc[i].addr = (u64)sc->rx_buf[i];
        sc->rx_desc[i].status = 0;
    }
    
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        sc->tx_buf[i] = kmalloc(TX_BUFFER_SIZE);
        if (!sc->tx_buf[i]) goto fail;
        sc->tx_desc[i].addr = (u64)sc->tx_buf[i];
        sc->tx_desc[i].status = 0;
    }
    
    e1000_write32(E1000_RDBAL, (u32)(u64)sc->rx_desc);
    e1000_write32(E1000_RDBAH, (u32)((u64)sc->rx_desc >> 32));
    e1000_write32(E1000_RDLEN, sizeof(e1000_rx_desc_t) * RX_DESC_COUNT);
    e1000_write32(E1000_RDH, 0);
    e1000_write32(E1000_RDT, RX_DESC_COUNT - 1);
    
    e1000_write32(E1000_TDBAL, (u32)(u64)sc->tx_desc);
    e1000_write32(E1000_TDBAH, (u32)((u64)sc->tx_desc >> 32));
    e1000_write32(E1000_TDLEN, sizeof(e1000_tx_desc_t) * TX_DESC_COUNT);
    e1000_write32(E1000_TDH, 0);
    e1000_write32(E1000_TDT, 0);
    
    e1000_write32(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    e1000_write32(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);
    e1000_write32(E1000_TIPG, 0x0060200A);
    
    e1000_write32(E1000_IMS, 0x1F6DC);
    e1000_write32(E1000_ICR, 0xFFFFFFFF);
    e1000_write32(E1000_CTRL, e1000_read32(E1000_CTRL) | E1000_CTRL_SLU);
    
    sc->attached = 1;
    vga_write("  E1000: OK\n");
    return 0;
    
fail:
    if (sc->rx_desc) kfree(sc->rx_desc);
    if (sc->tx_desc) kfree(sc->tx_desc);
    for (int i = 0; i < RX_DESC_COUNT; i++) if (sc->rx_buf[i]) kfree(sc->rx_buf[i]);
    for (int i = 0; i < TX_DESC_COUNT; i++) if (sc->tx_buf[i]) kfree(sc->tx_buf[i]);
    kfree(sc);
    sc = NULL;
    return -1;
}

void e1000_send(u8 *data, u16 len) {
    if (!sc || !sc->attached || len == 0 || len > TX_BUFFER_SIZE) return;
    
    int idx = sc->tx_cur;
    int timeout = 100000;
    while ((sc->tx_desc[idx].status & 0x01) && timeout-- > 0) {
        __asm__ volatile ("pause");
    }
    if (timeout <= 0) return;
    
    memcpy(sc->tx_buf[idx], data, len);
    sc->tx_desc[idx].length = len;
    sc->tx_desc[idx].cso = 0;
    sc->tx_desc[idx].status = 0;
    sc->tx_desc[idx].css = 0;
    sc->tx_desc[idx].special = 0;
    
    sc->tx_cur = (sc->tx_cur + 1) % TX_DESC_COUNT;
    e1000_write32(E1000_TDT, sc->tx_cur);
}

int e1000_recv(u8 *buffer, u16 max_len) {
    if (!sc || !sc->attached) return 0;
    
    int idx = sc->rx_cur;
    if (!(sc->rx_desc[idx].status & 0x01)) return 0;
    
    u16 len = sc->rx_desc[idx].length;
    if (len > max_len) len = max_len;
    memcpy(buffer, sc->rx_buf[idx], len);
    
    sc->rx_desc[idx].status = 0;
    sc->rx_cur = (sc->rx_cur + 1) % RX_DESC_COUNT;
    e1000_write32(E1000_RDT, sc->rx_cur);
    
    return len;
}

void e1000_get_mac(u8 *mac) {
    if (sc && sc->attached) {
        memcpy(mac, sc->mac, 6);
    } else {
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;
    }
}

void e1000_handle_irq(void) {
    if (!sc || !sc->attached) return;
    u32 icr = e1000_read32(E1000_ICR);
    if (icr & 0x01) {
        u8 packet[1514];
        int len = e1000_recv(packet, 1514);
        if (len > 0) {
            extern void net_handle_packet(u8*, int);
            net_handle_packet(packet, len);
        }
    }
}

int e1000_present(void) {
    return (sc && sc->attached);
}
