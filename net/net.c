#include "net.h"
#include "../include/string.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"
#include "rtl8139.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "../drivers/pci.h"

static u8 our_mac[6];
static u32 our_ip = 0;
static u32 our_netmask = 0;
static u32 our_gateway = 0;
static u32 our_dns = 0;
static int network_ready = 0;
static int dhcp_attempts = 0;

void net_init(void) {
    vga_write("  Network: Scanning for devices...\n");
    
    pci_dev_t *dev = NULL;
    
    dev = pci_find_device(0x10EC, 0x8139);
    if (!dev) dev = pci_find_device(0x10EC, 0x8169);
    if (!dev) dev = pci_find_device(0x8086, 0x100E);
    
    if (dev) {
        vga_write("  Found network controller, initializing...\n");
        
        if (dev->vendor_id == 0x10EC && (dev->device_id == 0x8139 || dev->device_id == 0x8169)) {
            if (rtl8139_init(dev) == 0) {
                rtl8139_get_mac(our_mac);
                network_ready = 1;
                vga_write("  RTL8139/RTL8169 driver loaded\n");
            }
        }
    } else {
        vga_write("  No supported network device found\n");
    }
    
    if (!network_ready) {
        vga_write("  No network device, using dummy MAC\n");
        our_mac[0] = 0x52;
        our_mac[1] = 0x54;
        our_mac[2] = 0x00;
        our_mac[3] = 0x12;
        our_mac[4] = 0x34;
        our_mac[5] = 0x56;
    }
    
    vga_write("  MAC: ");
    for (int i = 0; i < 6; i++) {
        vga_write_hex(our_mac[i]);
        if (i < 5) vga_write(":");
    }
    vga_write("\n");
    
    arp_cache_init();
    udp_init();
    tcp_init();
    
    if (network_ready) {
        vga_write("  Starting DHCP...\n");
        
        for (dhcp_attempts = 0; dhcp_attempts < 3; dhcp_attempts++) {
            dhcp_start();
            
            for (int i = 0; i < 200; i++) {
                for (int j = 0; j < 10000; j++) __asm__ volatile ("pause");
                if (our_ip != 0) break;
            }
            
            if (our_ip != 0) break;
            vga_write("  DHCP attempt ");
            vga_write_num(dhcp_attempts + 1);
            vga_write(" failed\n");
        }
    }
    
    if (our_ip == 0) {
        // Для QEMU всегда используем 10.0.2.15
        our_ip = 0x0A00020F;     // 10.0.2.15
        our_gateway = 0x0A000202; // 10.0.2.2
        our_netmask = 0x00FFFFFF; // 255.0.0.0
        our_dns = 0x0A000202;     // 10.0.2.2 как DNS (работает в QEMU)
        vga_write("  Using static IP: 10.0.2.15 (QEMU mode)\n");
    }
    
    vga_write("  Network ready, IP: ");
    vga_write_num((our_ip >> 24) & 0xFF);
    vga_write(".");
    vga_write_num((our_ip >> 16) & 0xFF);
    vga_write(".");
    vga_write_num((our_ip >> 8) & 0xFF);
    vga_write(".");
    vga_write_num(our_ip & 0xFF);
    vga_write("\n");
}

void net_handle_packet(u8 *packet, int len) {
    if (len < sizeof(eth_hdr_t)) return;
    
    eth_hdr_t *eth = (eth_hdr_t*)packet;
    u16 type = (eth->type << 8) | (eth->type >> 8);
    
    switch (type) {
        case 0x0608:
            arp_handle_packet(packet + sizeof(eth_hdr_t), len - sizeof(eth_hdr_t));
            break;
        case 0x0008:
            ip_handle_packet(packet + sizeof(eth_hdr_t), len - sizeof(eth_hdr_t), 
                           tcp_handle_packet, 
                           udp_handle_packet,
                           icmp_handle_packet);
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
    eth->type = (type << 8) | (type >> 8);
    
    memcpy(packet + sizeof(eth_hdr_t), data, len);
    
    rtl8139_send(packet, len + sizeof(eth_hdr_t));
    kfree(packet);
}

u8* net_get_mac(void) { return our_mac; }
u32 net_get_ip(void) { return our_ip; }
void net_set_ip(u32 ip) { our_ip = ip; }
void net_set_netmask(u32 mask) { our_netmask = mask; }
void net_set_gateway(u32 gw) { our_gateway = gw; }
void net_set_dns(u32 dns) { our_dns = dns; }
u32 net_get_dns(void) { return our_dns; }
int net_is_ready(void) { return network_ready; }
