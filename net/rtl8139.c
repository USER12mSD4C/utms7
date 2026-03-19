#include "rtl8139.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/pci.h"
#include "../drivers/vga.h"

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
#define RTL8139_REG_CFG 0x52
#define RTL8139_REG_CR 0x37
#define RTL8139_REG_CONFIG1 0x52

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
    int present;
} rtl8139_t;

static rtl8139_t *rtl = NULL;

static void rtl8139_write_reg(u16 reg, u8 val) {
    if (!rtl) return;
    outb(rtl->io_base + reg, val);
}

static u8 rtl8139_read_reg(u16 reg) {
    if (!rtl) return 0;
    return inb(rtl->io_base + reg);
}

static void rtl8139_write_long(u16 reg, u32 val) {
    if (!rtl) return;
    outl(rtl->io_base + reg, val);
}

static u32 rtl8139_read_long(u16 reg) {
    if (!rtl) return 0;
    return inl(rtl->io_base + reg);
}

static void rtl8139_write_word(u16 reg, u16 val) {
    if (!rtl) return;
    outw(rtl->io_base + reg, val);
}
static u16 rtl8139_read_word(u16 reg) {
    if (!rtl) return 0;
    return inw(rtl->io_base + reg);
}
int rtl8139_init(pci_dev_t *pci) {
    if (!pci) {
        vga_write("  RTL8139: No PCI device\n");
        return -1;
    }
    
    vga_write("  RTL8139: Initializing...\n");
    
    rtl = kmalloc(sizeof(rtl8139_t));
    if (!rtl) return -1;
    memset(rtl, 0, sizeof(rtl8139_t));
    
    rtl->io_base = pci->bar[0] & ~0x3;
    rtl->tx_cur = 0;
    rtl->present = 1;
    
    vga_write("  RTL8139: IO base=0x");
    vga_write_hex(rtl->io_base);
    vga_write("\n");
    
    // Включаем шину PCI
    u32 pci_cmd = pci_read_config(pci->bus, pci->slot, pci->func, 0x04);
    pci_cmd |= 0x07; // IO Space, Memory Space, Bus Master
    pci_write_config(pci->bus, pci->slot, pci->func, 0x04, pci_cmd);
    
    // Софт-резет
    rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RESET);
    
    int timeout = 1000;
    while ((rtl8139_read_reg(RTL8139_REG_CMD) & RTL8139_CMD_RESET) && timeout-- > 0) {
        for (int i = 0; i < 1000; i++) __asm__ volatile ("pause");
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
    
    // Выделяем RX буфер
    rtl->rx_buffer = kmalloc(RX_BUF_SIZE + 16);
    if (!rtl->rx_buffer) {
        kfree(rtl);
        rtl = NULL;
        return -1;
    }
    
    // Выравнивание
    u64 addr = (u64)rtl->rx_buffer;
    addr = (addr + 15) & ~0xF;
    rtl->rx_buffer = (u8*)addr;
    
    // Выделяем TX буферы
    for (int i = 0; i < 4; i++) {
        rtl->tx_buffer[i] = kmalloc(TX_BUF_SIZE);
        if (!rtl->tx_buffer[i]) {
            for (int j = 0; j < i; j++) kfree(rtl->tx_buffer[j]);
            kfree(rtl->rx_buffer);
            kfree(rtl);
            rtl = NULL;
            return -1;
        }
    }
    
    // Конфигурируем
    rtl8139_write_reg(RTL8139_REG_CFG, 0x00); // 8K RX, FIFO 8K
    
    // Устанавливаем RX буфер
    rtl8139_write_long(RTL8139_REG_RBSTART, (u32)(u64)rtl->rx_buffer);
    
    // Включаем прерывания
    rtl8139_write_word(RTL8139_REG_IMR, 0xFFFF);
    
    // Включаем прием и передачу
    rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);
    
    // Дополнительная конфигурация
    rtl8139_write_reg(RTL8139_REG_CONFIG1, 0x00);
    
    vga_write("  RTL8139: OK\n");
    
    return 0;
}

void rtl8139_send(u8 *data, u16 len) {
    if (!rtl || !rtl->present) return;
    
    int idx = rtl->tx_cur;
    
    // Копируем данные в TX буфер
    memcpy(rtl->tx_buffer[idx], data, len);
    
    // Устанавливаем адрес
    rtl8139_write_long(RTL8139_REG_TXADDR0 + idx * 4, (u32)(u64)rtl->tx_buffer[idx]);
    
    // Отправляем
    rtl8139_write_long(RTL8139_REG_TXSTATUS0 + idx * 4, len | 0x8000);
    
    // Следующий буфер
    rtl->tx_cur = (rtl->tx_cur + 1) % 4;
    
    // Ждем завершения отправки
    int timeout = 10000;
    while (timeout-- > 0) {
        if (!(rtl8139_read_long(RTL8139_REG_TXSTATUS0 + idx * 4) & 0x8000)) break;
        __asm__ volatile ("pause");
    }
}

int rtl8139_recv(u8 *buffer, u16 max_len) {
    if (!rtl || !rtl->present) return 0;
    
    u16 status = rtl8139_read_word(RTL8139_REG_ISR);
    if (!(status & 0x01)) return 0;
    
    // Сбрасываем прерывание
    rtl8139_write_word(RTL8139_REG_ISR, status);
    
    // Читаем заголовок пакета из RX буфера
    u32 *rx_header = (u32*)rtl->rx_buffer;
    u16 pkt_len = (*rx_header >> 16) & 0x3FFF;
    u16 pkt_status = *rx_header & 0xFFFF;
    
    // Проверка на ошибки
    if (pkt_status & 0x0001) return -1;
    
    // Копируем пакет
    if (pkt_len > max_len) pkt_len = max_len;
    memcpy(buffer, rtl->rx_buffer + 4, pkt_len);
    
    // Обновляем указатель RX буфера
    u32 cap = rtl8139_read_long(0x38);
    cap += pkt_len + 4;
    cap &= ~0x3;
    cap |= 0x01;
    rtl8139_write_long(0x38, cap);
    
    return pkt_len;
}

void rtl8139_get_mac(u8 *mac) {
    if (rtl && rtl->present) {
        memcpy(mac, rtl->mac, 6);
    } else {
        // Дефолтный MAC если драйвер не загружен
        mac[0] = 0x52;
        mac[1] = 0x54;
        mac[2] = 0x00;
        mac[3] = 0x12;
        mac[4] = 0x34;
        mac[5] = 0x56;
    }
}

void rtl8139_handle_irq(void) {
    if (!rtl || !rtl->present) return;
    
    u16 status = rtl8139_read_word(RTL8139_REG_ISR);
    
    // Сбрасываем все прерывания
    rtl8139_write_word(RTL8139_REG_ISR, status);
    
    if (status & 0x01) { // RX OK
        u8 packet[1514];
        int len = rtl8139_recv(packet, 1514);
        if (len > 0) {
            extern void net_handle_packet(u8*, int);
            net_handle_packet(packet, len);
        }
    }
    
    if (status & 0x04) { // TX OK
        // Трансмиссия завершена
    }
    
    if (status & 0x10) { // RX Error
        // Ошибка приема - сброс
        rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RESET);
    }
}

int rtl8139_present(void) {
    return (rtl && rtl->present);
}
