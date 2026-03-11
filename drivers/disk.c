#include "disk.h"
#include "../include/io.h"
#include "../include/string.h"
#include "vga.h"

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_DCR     0x3F6
#define ATA_SECONDARY_IO    0x170

#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA_LO      3
#define ATA_REG_LBA_MID     4
#define ATA_REG_LBA_HI      5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_COMMAND     7

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_FLUSH       0xE7

#define ATA_STATUS_BSY      0x80
#define ATA_STATUS_DRQ      0x08
#define ATA_STATUS_ERR      0x01

typedef struct {
    u16 base;
    u8 drive;
    u64 sectors;
    char model[41];
    int present;
} disk_t;

static disk_t disks[4];
static int current = 0;

static int ata_wait(u16 base, u8 bit, u8 val) {
    for (int i = 0; i < 10000000; i++) {
        u8 status = inb(base + ATA_REG_STATUS);
        if ((status & bit) == val) return 0;
    }
    return -1;
}

static int ata_identify(u16 base, u8 drive, disk_t* d) {
    outb(base + ATA_REG_DRIVE, drive ? 0xB0 : 0xA0);
    for (int i = 0; i < 4; i++) outb(base + i + 3, 0);
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    if (ata_wait(base, ATA_STATUS_BSY, 0) != 0) return -1;
    u8 status = inb(base + ATA_REG_STATUS);
    if (!status) return -1;
    
    u16 data[256];
    for (int i = 0; i < 256; i++) data[i] = inw(base + ATA_REG_DATA);
    
    d->present = 1;
    d->base = base;
    d->drive = drive;
    d->sectors = *(u32*)(data + 60);
    if (!d->sectors) d->sectors = *(u64*)(data + 100);
    
    for (int i = 0; i < 40; i+=2) {
        d->model[i] = data[27 + i/2] >> 8;
        d->model[i+1] = data[27 + i/2] & 0xFF;
    }
    d->model[40] = 0;
    return 0;
}

int disk_init(void) {
    memset(disks, 0, sizeof(disks));
    ata_identify(ATA_PRIMARY_IO, 0, &disks[0]);
    ata_identify(ATA_PRIMARY_IO, 1, &disks[1]);
    ata_identify(ATA_SECONDARY_IO, 0, &disks[2]);
    ata_identify(ATA_SECONDARY_IO, 1, &disks[3]);
    return 0;
}

int disk_read(u32 lba, u8* buffer) {
    disk_t* d = &disks[current];
    if (!d->present) return -1;
    
    if (ata_wait(d->base, ATA_STATUS_BSY, 0) != 0) return -1;
    
    outb(d->base + ATA_REG_DRIVE, 0xE0 | (d->drive << 4) | ((lba >> 24) & 0x0F));
    outb(d->base + ATA_REG_SECCOUNT, 1);
    outb(d->base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(d->base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(d->base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(d->base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    if (ata_wait(d->base, ATA_STATUS_BSY, 0) != 0) return -1;
    if (ata_wait(d->base, ATA_STATUS_DRQ, ATA_STATUS_DRQ) != 0) return -1;
    
    for (int i = 0; i < 256; i++) {
        u16 data = inw(d->base + ATA_REG_DATA);
        buffer[i*2] = data & 0xFF;
        buffer[i*2+1] = (data >> 8) & 0xFF;
    }
    return 0;
}

int disk_write(u32 lba, u8* buffer) {
    disk_t* d = &disks[current];
    if (!d->present) return -1;
    
    if (ata_wait(d->base, ATA_STATUS_BSY, 0) != 0) return -1;
    
    outb(d->base + ATA_REG_DRIVE, 0xE0 | (d->drive << 4) | ((lba >> 24) & 0x0F));
    outb(d->base + ATA_REG_SECCOUNT, 1);
    outb(d->base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(d->base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(d->base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(d->base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    if (ata_wait(d->base, ATA_STATUS_BSY, 0) != 0) return -1;
    if (ata_wait(d->base, ATA_STATUS_DRQ, ATA_STATUS_DRQ) != 0) return -1;
    
    for (int i = 0; i < 256; i++) {
        u16 data = buffer[i*2] | (buffer[i*2+1] << 8);
        outw(d->base + ATA_REG_DATA, data);
    }
    
    outb(d->base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
    ata_wait(d->base, ATA_STATUS_BSY, 0);
    return 0;
}

int disk_set_disk(int n) {
    if (n < 0 || n >= 4 || !disks[n].present) return -1;
    current = n;
    return 0;
}

int disk_get_disk_count(void) {
    int c = 0;
    for (int i = 0; i < 4; i++) if (disks[i].present) c++;
    return c;
}

void disk_list_disks(void) {
    for (int i = 0; i < 4; i++) {
        if (disks[i].present) {
            vga_write("  sd");
            vga_putchar('a' + i);
            vga_write(": ");
            vga_write(disks[i].model);
            vga_putchar('\n');
        }
    }
}

u64 disk_get_sectors(u8 drive) {
    int index = drive - 0x80;
    if (index < 0 || index >= 4) return 0;
    return disks[index].sectors;
}

static const char __disk_name[] __attribute__((section(".module_name"))) = "disk";
static int (*__disk_entry)(void) __attribute__((section(".module_entry"))) = disk_init;
