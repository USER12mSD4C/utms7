// net/rtl8139.c
#include "rtl8139.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/pci.h"
#include "../drivers/vga.h"

#define RTL8139_REG_MAC0       0x00
#define RTL8139_REG_MAC4       0x04
#define RTL8139_REG_RBSTART    0x30
#define RTL8139_REG_CMD        0x37
#define RTL8139_REG_IMR        0x3C
#define RTL8139_REG_ISR        0x3E
#define RTL8139_REG_TX_STATUS0 0x10
#define RTL8139_REG_TX_ADDR0   0x20
#define RTL8139_REG_CFG        0x52
#define RTL8139_REG_CAPR       0x38
#define RTL8139_REG_TX_CONFIG  0x40
#define RTL8139_REG_RX_CONFIG  0x44

#define RTL8139_CMD_RESET      0x10
#define RTL8139_CMD_RX_ENABLE  0x08
#define RTL8139_CMD_TX_ENABLE  0x04

#define RX_BUF_SIZE 8192
#define TX_BUF_SIZE 1536

typedef struct {
    u16 io_base;
    u8 mac[6];
    u8 *rx_buffer;
    u8 *tx_buffer[4];
    int tx_cur;
    int present;
} rtl8139_t;

static rtl8139_t *rtl = NULL;

static inline void rtl8139_write_reg(u16 reg, u8 val) {
    outb(rtl->io_base + reg, val);
}

static inline u8 rtl8139_read_reg(u16 reg) {
    return inb(rtl->io_base + reg);
}

static inline void rtl8139_write_long(u16 reg, u32 val) {
    outl(rtl->io_base + reg, val);
}

static inline u32 rtl8139_read_long(u16 reg) {
    return inl(rtl->io_base + reg);
}

static inline void rtl8139_write_word(u16 reg, u16 val) {
    outw(rtl->io_base + reg, val);
}

static inline u16 rtl8139_read_word(u16 reg) {
    return inw(rtl->io_base + reg);
}

void rtl8139_dump_regs(void) {
    if (!rtl) {
        vga_write("RTL8139: not initialized\n");
        return;
    }
    
    vga_write("RTL8139 registers:\n");
    vga_write("  CMD: 0x"); vga_write_hex(rtl8139_read_reg(RTL8139_REG_CMD)); vga_write("\n");
    vga_write("  ISR: 0x"); vga_write_hex(rtl8139_read_word(RTL8139_REG_ISR)); vga_write("\n");
    vga_write("  IMR: 0x"); vga_write_hex(rtl8139_read_word(RTL8139_REG_IMR)); vga_write("\n");
    
    for (int i = 0; i < 4; i++) {
        vga_write("  TX_STATUS"); vga_write_num(i); vga_write(": 0x");
        vga_write_hex(rtl8139_read_long(RTL8139_REG_TX_STATUS0 + i * 4));
        vga_write("\n");
    }
}

int rtl8139_init(pci_dev_t *pci) {
    if (!pci) return -1;
    
    rtl = kmalloc(sizeof(rtl8139_t));
    if (!rtl) return -1;
    memset(rtl, 0, sizeof(rtl8139_t));
    
    rtl->io_base = pci->bar[0] & 0xFFFC;
    rtl->tx_cur = 0;
    rtl->present = 1;
    
    vga_write("  RTL8139: IO base=0x");
    vga_write_hex(rtl->io_base);
    vga_write("\n");
    
    // Включаем Bus Mastering
    u32 pci_cmd = pci_read_config(pci->bus, pci->slot, pci->func, 0x04);
    pci_cmd |= 0x07;
    pci_write_config(pci->bus, pci->slot, pci->func, 0x04, pci_cmd);
    
    // Софт-резет
    rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RESET);
    
    int timeout = 10000;
    while ((rtl8139_read_reg(RTL8139_REG_CMD) & RTL8139_CMD_RESET) && timeout-- > 0) {
        for (int i = 0; i < 100; i++) __asm__ volatile ("pause");
    }
    
    if (timeout <= 0) {
        vga_write("  RTL8139: Reset timeout\n");
        kfree(rtl);
        rtl = NULL;
        return -1;
    }
    
    // Читаем MAC
    for (int i = 0; i < 6; i++) {
        rtl->mac[i] = rtl8139_read_reg(RTL8139_REG_MAC0 + i);
    }
    
    vga_write("  RTL8139: MAC ");
    for (int i = 0; i < 6; i++) {
        vga_write_hex(rtl->mac[i]);
        if (i < 5) vga_write(":");
    }
    vga_write("\n");
    
    // Выделяем RX буфер (выровненный по 256 байт)
    rtl->rx_buffer = kmalloc(RX_BUF_SIZE + 256);
    if (!rtl->rx_buffer) {
        kfree(rtl);
        return -1;
    }
    u64 rx_addr = (u64)rtl->rx_buffer;
    rx_addr = (rx_addr + 255) & ~255;
    rtl->rx_buffer = (u8*)rx_addr;
    
    // Выделяем TX буферы
    for (int i = 0; i < 4; i++) {
        rtl->tx_buffer[i] = kmalloc(TX_BUF_SIZE);
        if (!rtl->tx_buffer[i]) {
            for (int j = 0; j < i; j++) kfree(rtl->tx_buffer[j]);
            kfree(rtl->rx_buffer);
            kfree(rtl);
            return -1;
        }
        memset(rtl->tx_buffer[i], 0, TX_BUF_SIZE);
    }
    
    // Настройка RX
    rtl8139_write_long(RTL8139_REG_RBSTART, (u32)rx_addr);
    
    // Настройка TX
    rtl8139_write_long(RTL8139_REG_TX_CONFIG, 0x600000);
    
    // Включаем прерывания
    rtl8139_write_word(RTL8139_REG_IMR, 0x0005);  // RX OK, TX OK
    rtl8139_write_word(RTL8139_REG_ISR, 0xFFFF);  // Очищаем все прерывания
    
    // Включаем прием и передачу
    rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);
    
    vga_write("  RTL8139: OK\n");
    return 0;
}

void rtl8139_send(u8 *data, u16 len) {
    if (!rtl || !rtl->present) return;
    if (len == 0 || len > TX_BUF_SIZE) return;
    
    // Ищем свободный дескриптор и сбрасываем зависшие
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        u32 status = rtl8139_read_long(RTL8139_REG_TX_STATUS0 + i * 4);
        if (!(status & 0x8000)) {
            idx = i;
            break;
        }
        // Если статус висит слишком долго (бит 0x8000 не сбрасывается), сбрасываем принудительно
        if ((status & 0x8000) && (status & 0x4000)) {
            rtl8139_write_long(RTL8139_REG_TX_STATUS0 + i * 4, 0);
            idx = i;
            break;
        }
    }
    
    if (idx == -1) {
        vga_write("RTL8139: no free TX desc\n");
        // Сбрасываем все дескрипторы
        for (int i = 0; i < 4; i++) {
            rtl8139_write_long(RTL8139_REG_TX_STATUS0 + i * 4, 0);
        }
        idx = 0;
    }
    
    // Копируем данные
    memcpy(rtl->tx_buffer[idx], data, len);
    
    // Устанавливаем адрес буфера
    rtl8139_write_long(RTL8139_REG_TX_ADDR0 + idx * 4, (u32)(u64)rtl->tx_buffer[idx]);
    
    // Запускаем передачу
    rtl8139_write_long(RTL8139_REG_TX_STATUS0 + idx * 4, len);
    
    rtl->tx_cur = (rtl->tx_cur + 1) % 4;
}

int rtl8139_recv(u8 *buffer, u16 max_len) {
    if (!rtl || !rtl->present) return 0;
    
    u16 status = rtl8139_read_word(RTL8139_REG_ISR);
    if (!(status & 0x01)) return 0;
    
    rtl8139_write_word(RTL8139_REG_ISR, 0x01);
    
    u32 rx_status = *(u32*)rtl->rx_buffer;
    u16 pkt_len = (rx_status >> 16) & 0x3FFF;
    
    if (pkt_len == 0 || pkt_len > 1514) return 0;
    if (rx_status & 0x0001) return 0;
    
    if (pkt_len > max_len) pkt_len = max_len;
    memcpy(buffer, rtl->rx_buffer + 4, pkt_len);
    
    u32 capr = rtl8139_read_long(RTL8139_REG_CAPR);
    capr += pkt_len + 4;
    capr = (capr + 3) & ~3;
    rtl8139_write_long(RTL8139_REG_CAPR, capr);
    
    return pkt_len;
}

void rtl8139_get_mac(u8 *mac) {
    if (rtl && rtl->present) {
        memcpy(mac, rtl->mac, 6);
    } else {
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;
    }
}

void rtl8139_handle_irq(void) {
    if (!rtl || !rtl->present) return;
    
    u16 status = rtl8139_read_word(RTL8139_REG_ISR);
    rtl8139_write_word(RTL8139_REG_ISR, status);
    
    if (status & 0x01) {
        u8 packet[1514];
        int len = rtl8139_recv(packet, 1514);
        if (len > 0) {
            extern void net_handle_packet(u8*, int);
            net_handle_packet(packet, len);
        }
    }
}

int rtl8139_present(void) {
    return (rtl && rtl->present);
}
