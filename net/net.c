// net/net.c
#include "net.h"
#include "../include/string.h"
#include "../include/endian.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"
#include "../drivers/pci.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "e1000.h"
#include "rtl8139.h"

static u8 our_mac[6];
static u32 our_ip = 0;
static u32 our_netmask = 0;
static u32 our_gateway = 0;
static u32 our_dns = 0;
static int network_ready = 0;
static int nic_type = 0;  // 0 = none, 1 = e1000, 2 = rtl8139

int net_init(void) {
    vga_write("\nNetwork: Scanning for devices...\n");

    // Ищем Intel e1000
    pci_dev_t *dev = pci_find_device(0x8086, 0x100E);
    if (dev) {
        vga_write("Found Intel e1000 (PCI ");
        vga_write_hex(dev->bus);
        vga_write(":");
        vga_write_hex(dev->slot);
        vga_write(":");
        vga_write_hex(dev->func);
        vga_write(")\n");

if (e1000_init(dev) == 0) {
    e1000_get_mac(our_mac);
    // Проверяем, что MAC не нулевой
    int zero_mac = 1;
    for (int i = 0; i < 6; i++) {
        if (our_mac[i] != 0) {
            zero_mac = 0;
            break;
        }
    }
    if (zero_mac) {
        vga_write("Warning: zero MAC, using default\n");
        our_mac[0] = 0x52;
        our_mac[1] = 0x54;
        our_mac[2] = 0x00;
        our_mac[3] = 0x12;
        our_mac[4] = 0x34;
        our_mac[5] = 0x56;
    }
    network_ready = 1;
    nic_type = 1;
    vga_write("Intel e1000 driver loaded, MAC: ");
    for (int i = 0; i < 6; i++) {
        vga_write_hex(our_mac[i]);
        if (i < 5) vga_write(":");
    }
    vga_write("\n");
} else {
            vga_write("Intel e1000 init failed\n");
        }
    }

    // Если e1000 не найден или не загрузился, ищем RTL8139
    if (!network_ready) {
        dev = pci_find_device(0x10EC, 0x8139);
        if (dev) {
            vga_write("Found Realtek RTL8139 (PCI ");
            vga_write_hex(dev->bus);
            vga_write(":");
            vga_write_hex(dev->slot);
            vga_write(":");
            vga_write_hex(dev->func);
            vga_write(")\n");

            if (rtl8139_init(dev) == 0) {
                rtl8139_get_mac(our_mac);
                network_ready = 1;
                nic_type = 2;
                vga_write("RTL8139 driver loaded, MAC: ");
                for (int i = 0; i < 6; i++) {
                    vga_write_hex(our_mac[i]);
                    if (i < 5) vga_write(":");
                }
                vga_write("\n");
            } else {
                vga_write("RTL8139 init failed\n");
            }
        }
    }

    if (!network_ready) {
        vga_write("No network device found, using dummy MAC\n");
        our_mac[0] = 0x52; our_mac[1] = 0x54; our_mac[2] = 0x00;
        our_mac[3] = 0x12; our_mac[4] = 0x34; our_mac[5] = 0x56;
        return 0;
    }

    arp_cache_init();
    udp_init();
    tcp_init();

    if (network_ready) {
        vga_write("Starting DHCP...\n");
        for (int attempt = 0; attempt < 3; attempt++) {
            dhcp_start();
            for (int i = 0; i < 500; i++) {
                for (int j = 0; j < 10000; j++) __asm__ volatile ("pause");
                if (our_ip != 0) break;
            }
            if (our_ip != 0) break;
            vga_write("DHCP attempt ");
            vga_write_num(attempt + 1);
            vga_write(" failed\n");
        }
    }

    if (our_ip == 0) {
        our_ip = 0x0A00020F;      // 10.0.2.15
        our_gateway = 0x0A000202; // 10.0.2.2
        our_netmask = 0xFFFFFF00; // 255.255.255.0
        our_dns = 0x0A000202;     // 10.0.2.2
        vga_write("Using static IP: 10.0.2.15 (QEMU mode)\n");
    }

    vga_write("\n=== Network Configuration ===\n");
    vga_write("IP:     ");
    vga_write_num((our_ip >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((our_ip >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((our_ip >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(our_ip & 0xFF);
    vga_write("\n");

    vga_write("Gateway: ");
    vga_write_num((our_gateway >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((our_gateway >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((our_gateway >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(our_gateway & 0xFF);
    vga_write("\n");

    vga_write("Netmask: ");
    vga_write_num((our_netmask >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((our_netmask >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((our_netmask >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(our_netmask & 0xFF);
    vga_write("\n");

    vga_write("DNS:     ");
    vga_write_num((our_dns >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((our_dns >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((our_dns >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(our_dns & 0xFF);
    vga_write("\n");
    vga_write("===========================\n\n");

    vga_write("Network ready!\n");
    return 0;
}

void net_handle_packet(u8 *packet, int len) {
    if (len < sizeof(eth_hdr_t)) return;
    eth_hdr_t *eth = (eth_hdr_t*)packet;
    u16 type = ntohs(eth->type);

    switch (type) {
        case ETHERTYPE_ARP:
            arp_handle_packet(packet + sizeof(eth_hdr_t), len - sizeof(eth_hdr_t));
            break;
        case ETHERTYPE_IP:
            ip_handle_packet(packet + sizeof(eth_hdr_t), len - sizeof(eth_hdr_t),
                           tcp_handle_packet, udp_handle_packet, icmp_handle_packet);
            break;
    }
}

void net_eth_send(u8 *dst_mac, u16 type, u8 *data, int len) {
    if (!network_ready) return;
    u8 *packet = kmalloc(len + sizeof(eth_hdr_t));
    if (!packet) return;
    eth_hdr_t *eth = (eth_hdr_t*)packet;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, our_mac, 6);
    eth->type = htons(type);
    memcpy(packet + sizeof(eth_hdr_t), data, len);

    if (nic_type == 1 && e1000_present()) {
        e1000_send(packet, len + sizeof(eth_hdr_t));
    } else if (nic_type == 2 && rtl8139_present()) {
        rtl8139_send(packet, len + sizeof(eth_hdr_t));
    }
    kfree(packet);
}

u8* net_get_mac(void) { return our_mac; }
u32 net_get_ip(void) { return our_ip; }
u32 net_get_gateway(void) { return our_gateway; }
u32 net_get_netmask(void) { return our_netmask; }
void net_set_ip(u32 ip) { our_ip = ip; }
void net_set_netmask(u32 mask) { our_netmask = mask; }
void net_set_gateway(u32 gw) { our_gateway = gw; }
void net_set_dns(u32 dns) { our_dns = dns; }
u32 net_get_dns(void) { return our_dns; }
int net_is_ready(void) { return network_ready; }
