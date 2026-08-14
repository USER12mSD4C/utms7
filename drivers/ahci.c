// File: drivers/ahci.c
#include "../include/ahci.h"
#include "../kernel/paging.h"
#include "pci.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/print.h"

#define AHCI_CAP        0x00
#define AHCI_GHC        0x04
#define AHCI_IS         0x08
#define AHCI_PI         0x0C

#define AHCI_PxCLB      0x00
#define AHCI_PxCLBU     0x04
#define AHCI_PxFB       0x08
#define AHCI_PxFBU      0x0C
#define AHCI_PxIS       0x10
#define AHCI_PxIE       0x14
#define AHCI_PxCMD      0x18
#define AHCI_PxTFD      0x20
#define AHCI_PxSIG      0x24
#define AHCI_PxSSTS     0x28
#define AHCI_PxCI       0x38

#define AHCI_GHC_AE     (1 << 31)
#define AHCI_PxCMD_ST   (1 << 0)
#define AHCI_PxCMD_FRE  (1 << 4)
#define AHCI_PxCMD_FR   (1 << 14)
#define AHCI_PxCMD_CR   (1 << 15)

#define SATA_SIG_ATA    0x00000101
#define FIS_TYPE_REG_H2D 0x27

typedef struct {
    u8  cfl:5, a:1, w:1, p:1;
    u8  r:1, b:1, c:1, rsv0:1, pmp:4;
    u16 prdtl;
    volatile u32 prdbc;
    u64 ctba;
    u32 rsv1[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
    u64 dba;
    u32 rsv0;
    u32 dbc:22, rsv1:9, i:1;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct {
    u8 cfis[64];
    u8 acmd[16];
    u8 rsv[48];
    ahci_prdt_entry_t prdt_entry[8];
} __attribute__((packed)) ahci_cmd_table_t;

typedef struct {
    u8  fis_type;
    u8  pmport:4, rsv0:3, c:1;
    u8  command;
    u8  featurel;
    u8  lba0, lba1, lba2;
    u8  device;
    u8  lba3, lba4, lba5;
    u8  featureh;
    u8  countl, counth;
    u8  icc, control;
    u8  rsv1[4];
} __attribute__((packed)) fis_reg_h2d_t;

static volatile u8* ahci_base = NULL;
static u32 ahci_ports = 0;

__attribute__((aligned(1024))) static ahci_cmd_header_t cmd_list[32];
__attribute__((aligned(256))) static u8 fis_buf[256];
__attribute__((aligned(128))) static ahci_cmd_table_t cmd_table[32];
__attribute__((aligned(16))) static u8 identify_buf[512];

static u64 virt_to_phys(void* ptr) {
    u64 addr = (u64)ptr;
    if (addr >= 0xFFFFFFFF80000000ULL) {
        return addr - 0xFFFFFFFF80000000ULL + 0x100000;
    }
    return addr;
}

static void ahci_port_stop(int port) {
    volatile u8* port_base = ahci_base + 0x100 + port * 0x80;
    u32 cmd = *(volatile u32*)(port_base + AHCI_PxCMD);
    cmd &= ~AHCI_PxCMD_ST;
    *(volatile u32*)(port_base + AHCI_PxCMD) = cmd;
    while (*(volatile u32*)(port_base + AHCI_PxCMD) & AHCI_PxCMD_CR);

    cmd &= ~AHCI_PxCMD_FRE;
    *(volatile u32*)(port_base + AHCI_PxCMD) = cmd;
    while (*(volatile u32*)(port_base + AHCI_PxCMD) & AHCI_PxCMD_FR);
}

static void ahci_port_start(int port) {
    volatile u8* port_base = ahci_base + 0x100 + port * 0x80;
    while (*(volatile u32*)(port_base + AHCI_PxCMD) & AHCI_PxCMD_CR);
    u32 cmd = *(volatile u32*)(port_base + AHCI_PxCMD);
    cmd |= AHCI_PxCMD_FRE;
    cmd |= AHCI_PxCMD_ST;
    *(volatile u32*)(port_base + AHCI_PxCMD) = cmd;
}

static void ahci_port_init(int port) {
    volatile u8* port_base = ahci_base + 0x100 + port * 0x80;
    ahci_port_stop(port);

    u64 clb_phys = virt_to_phys(&cmd_list[0]);
    u64 fb_phys = virt_to_phys(&fis_buf[0]);
    u64 ctba_phys = virt_to_phys(&cmd_table[0]);

    *(volatile u32*)(port_base + AHCI_PxCLB) = (u32)clb_phys;
    *(volatile u32*)(port_base + AHCI_PxCLBU) = (u32)(clb_phys >> 32);
    *(volatile u32*)(port_base + AHCI_PxFB) = (u32)fb_phys;
    *(volatile u32*)(port_base + AHCI_PxFBU) = (u32)(fb_phys >> 32);

    *(volatile u32*)(port_base + AHCI_PxIS) = 0xFFFFFFFF;
    *(volatile u32*)(port_base + AHCI_PxIE) = 0;

    memset(&cmd_list[0], 0, sizeof(ahci_cmd_header_t));
    cmd_list[0].ctba = ctba_phys;
    memset(&fis_buf[0], 0, 256);
    memset(&cmd_table[0], 0, sizeof(ahci_cmd_table_t));

    ahci_port_start(port);
}

static int ahci_send_cmd(int port, int is_write, u8 cmd_code, u64 lba, u32 count, void* buffer) {
    volatile u8* port_base = ahci_base + 0x100 + port * 0x80;

    memset(&cmd_list[0], 0, sizeof(ahci_cmd_header_t));
    cmd_list[0].cfl = sizeof(fis_reg_h2d_t) / 4;
    cmd_list[0].w = is_write;
    cmd_list[0].prdtl = 1;
    cmd_list[0].ctba = virt_to_phys(&cmd_table[0]);

    memset(&cmd_table[0], 0, sizeof(ahci_cmd_table_t));

    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)(&cmd_table[0].cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = cmd_code;

    cmdfis->lba0 = (u8)lba;
    cmdfis->lba1 = (u8)(lba >> 8);
    cmdfis->lba2 = (u8)(lba >> 16);
    cmdfis->device = 1 << 6;
    cmdfis->lba3 = (u8)(lba >> 24);
    cmdfis->lba4 = (u8)(lba >> 32);
    cmdfis->lba5 = (u8)(lba >> 40);

    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    cmd_table[0].prdt_entry[0].dba = virt_to_phys(buffer);
    cmd_table[0].prdt_entry[0].dbc = count * 512 - 1;
    cmd_table[0].prdt_entry[0].i = 1;

    *(volatile u32*)(port_base + AHCI_PxIS) = 0xFFFFFFFF;
    *(volatile u32*)(port_base + AHCI_PxCI) = 1;

    while (1) {
        if ((*(volatile u32*)(port_base + AHCI_PxCI) & 1) == 0) break;
        if (*(volatile u32*)(port_base + AHCI_PxIS) & (1 << 30)) return -1;
    }
    return 0;
}

static void ahci_port_identify(int port) {
    memset(identify_buf, 0, 512);
    print("AHCI: sending IDENTIFY to port "); printnum(port); print("...\n");

    int res = ahci_send_cmd(port, 0, 0xEC, 0, 1, identify_buf);

    print("AHCI: IDENTIFY cmd result = "); printnum(res); print("\n");
    if (res != 0) return;

    u16* data = (u16*)identify_buf;

    print("AHCI: IDENTIFY word0 = "); printhex(data[0]); print("\n");
    print("AHCI: IDENTIFY word60 = "); printhex(data[60]); print("\n");
    print("AHCI: IDENTIFY word61 = "); printhex(data[61]); print("\n");

    char model[41];
    for (int i = 0; i < 40; i+=2) {
        model[i] = data[27 + i/2] >> 8;
        model[i+1] = data[27 + i/2] & 0xFF;
    }
    model[40] = '\0';
    for (int i = 0; i < 40; i++) {
        if (model[i] < 32 || model[i] > 126) model[i] = ' ';
    }

    u64 sectors = (u32)data[60] | ((u32)data[61] << 16);
    if (sectors == 0) {
        sectors = ((u64)data[100] | ((u64)data[101] << 16) |
                   ((u64)data[102] << 32) | ((u64)data[103] << 48));
    }

    print("AHCI: sectors = "); printnum((u32)sectors); print("\n");
    print("AHCI: model = "); print(model); print("\n");

    extern void ahci_register_disk(int, u64, const char*);
    ahci_register_disk(port, sectors, model);
}

int ahci_read(int port, u64 lba, u32 count, void* buffer) {
    return ahci_send_cmd(port, 0, 0x25, lba, count, buffer);
}

int ahci_write(int port, u64 lba, u32 count, void* buffer) {
    return ahci_send_cmd(port, 1, 0x35, lba, count, buffer);
}

int ahci_init(void) {
    print("AHCI: scanning...\n");

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                u32 id = pci_read_config(bus, slot, func, 0);
                if (id == 0xFFFFFFFF) {
                    if (func == 0) break;
                    continue;
                }

                u32 class_reg = pci_read_config(bus, slot, func, 8);
                u8 class = (class_reg >> 24) & 0xFF;
                u8 subclass = (class_reg >> 16) & 0xFF;
                u8 progif = (class_reg >> 8) & 0xFF;

                if (class == 0x01 && subclass == 0x06 && progif == 0x01) {
                    print("AHCI: found controller at ");
                    printnum(bus); print(":"); printnum(slot); print("."); printnum(func); print("\n");

                    u32 bar5 = pci_read_config(bus, slot, func, 0x24);
                    u64 bar5_phys = (u64)(bar5 & ~0xF);

                    if (bar5_phys == 0) {
                        print("AHCI: BAR5 is invalid\n");
                        return -1;
                    }

                    u64 virt_addr = 0xFFFF900000000000ULL + bar5_phys;
                    for (u64 i = 0; i < 4096; i += 4096) {
                        paging_map(bar5_phys + i, virt_addr + i, PAGE_PRESENT | PAGE_WRITABLE);
                    }
                    ahci_base = (volatile u8*)virt_addr;

                    volatile u32* ghc = (volatile u32*)(ahci_base + AHCI_GHC);
                    if (!(*ghc & AHCI_GHC_AE)) {
                        *ghc = *ghc | AHCI_GHC_AE;
                    }

                    ahci_ports = *(volatile u32*)(ahci_base + AHCI_PI);
                    print("AHCI: implemented ports mask = ");
                    printhex(ahci_ports);
                    print("\n");

                    for (int i = 0; i < 32; i++) {
                        if (ahci_ports & (1 << i)) {
                            volatile u8* port_base = ahci_base + 0x100 + i * 0x80;
                            u32 ssts = *(volatile u32*)(port_base + AHCI_PxSSTS);
                            u8 det = ssts & 0x0F;
                            u8 ipm = (ssts >> 8) & 0x0F;

                            if (det == 3 && ipm == 1) {
                                u32 sig = *(volatile u32*)(port_base + AHCI_PxSIG);
                                if (sig == SATA_SIG_ATA) {
                                    print("AHCI: SATA drive found on port ");
                                    printnum(i);
                                    print("\n");
                                    ahci_port_init(i);
                                    ahci_port_identify(i);
                                }
                            }
                        }
                    }
                    return 0;
                }

                u32 header = pci_read_config(bus, slot, func, 12);
                if (func == 0 && !((header >> 16) & 0x80)) break;
            }
        }
    }
    print("AHCI: no controller found\n");
    return -1;
}

static const char __ahci_name[] __attribute__((section(".module_name"))) = "ahci";
static int (*__ahci_entry)(void) __attribute__((section(".module_entry"))) = ahci_init;
