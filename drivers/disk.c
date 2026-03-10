#include "disk.h"
#include "../include/io.h"
#include "../drivers/vga.h"
#include "../include/string.h"

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_DCR     0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_DCR   0x376

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

#define ATA_STATUS_ERR      0x01
#define ATA_STATUS_DRQ      0x08
#define ATA_STATUS_SRV      0x10
#define ATA_STATUS_DF       0x20
#define ATA_STATUS_RDY      0x40
#define ATA_STATUS_BSY      0x80

typedef struct {
    u8 present;
    u8 is_ata;
    u8 is_atapi;
    u64 sectors;
    char model[41];
    u16 io_base;
    u8 drive_num;
} disk_info_t;

static disk_info_t disks[4];
static int current_disk = 0;

static void debug_putc(char c) { outb(0xE9, c); }
static void debug_puts(const char* s) { while (*s) debug_putc(*s++); }

static int ata_wait_bsy(u16 io_base) {
    int timeout = 10000000;
    while (timeout--) {
        u8 status = inb(io_base + ATA_REG_STATUS);
        if (!(status & ATA_STATUS_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(u16 io_base) {
    int timeout = 10000000;
    while (timeout--) {
        u8 status = inb(io_base + ATA_REG_STATUS);
        if (status & ATA_STATUS_DRQ) return 0;
        if (status & ATA_STATUS_ERR) return -1;
    }
    return -1;
}

static int ata_identify(u16 io_base, u8 drive, disk_info_t* info) {
    outb(io_base + ATA_REG_DRIVE, drive == 0 ? 0xA0 : 0xB0);
    io_wait();
    
    outb(io_base + ATA_REG_SECCOUNT, 0);
    outb(io_base + ATA_REG_LBA_LO, 0);
    outb(io_base + ATA_REG_LBA_MID, 0);
    outb(io_base + ATA_REG_LBA_HI, 0);
    
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    if (ata_wait_bsy(io_base) != 0) return -1;
    
    u8 status = inb(io_base + ATA_REG_STATUS);
    if (status == 0) return -1;
    
    u16 data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = inw(io_base + ATA_REG_DATA);
    }
    
    info->present = 1;
    info->io_base = io_base;
    info->drive_num = drive;
    
    if (data[0] & 0x8000) {
        info->is_ata = 0;
        info->is_atapi = 1;
        return 0;
    }
    
    info->is_ata = 1;
    info->is_atapi = 0;
    
    info->sectors = *(u32*)(data + 60);
    if (info->sectors == 0) {
        info->sectors = *(u64*)(data + 100);
    }
    
    for (int i = 0; i < 40; i+=2) {
        info->model[i] = data[27 + i/2] >> 8;
        info->model[i+1] = data[27 + i/2] & 0xFF;
    }
    info->model[40] = 0;
    
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
    if (current_disk < 0 || current_disk >= 4) return -1;
    if (!disks[current_disk].present) return -1;
    
    u16 io_base = disks[current_disk].io_base;
    u8 drive = disks[current_disk].drive_num;
    
    if (ata_wait_bsy(io_base) != 0) return -1;
    
    outb(io_base + ATA_REG_DRIVE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(io_base + ATA_REG_SECCOUNT, 1);
    outb(io_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    if (ata_wait_bsy(io_base) != 0) return -1;
    if (ata_wait_drq(io_base) != 0) return -1;
    
    for (int i = 0; i < 256; i++) {
        u16 data = inw(io_base + ATA_REG_DATA);
        buffer[i*2] = data & 0xFF;
        buffer[i*2+1] = (data >> 8) & 0xFF;
    }
    
    return 0;
}

int disk_write(u32 lba, u8* buffer) {
    if (current_disk < 0 || current_disk >= 4) return -1;
    if (!disks[current_disk].present) return -1;
    
    u16 io_base = disks[current_disk].io_base;
    u8 drive = disks[current_disk].drive_num;
    
    if (ata_wait_bsy(io_base) != 0) return -1;
    
    outb(io_base + ATA_REG_DRIVE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(io_base + ATA_REG_SECCOUNT, 1);
    outb(io_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    if (ata_wait_bsy(io_base) != 0) return -1;
    if (ata_wait_drq(io_base) != 0) return -1;
    
    for (int i = 0; i < 256; i++) {
        u16 data = buffer[i*2] | (buffer[i*2+1] << 8);
        outw(io_base + ATA_REG_DATA, data);
    }
    
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
    ata_wait_bsy(io_base);
    
    return 0;
}

int disk_set_disk(int disk_num) {
    if (disk_num < 0 || disk_num >= 4) return -1;
    if (!disks[disk_num].present) return -1;
    current_disk = disk_num;
    return 0;
}

int disk_set_drive(u8 drive) {
    int index = drive - 0x80;
    return disk_set_disk(index);
}

int disk_get_disk_count(void) {
    int count = 0;
    for (int i = 0; i < 4; i++) {
        if (disks[i].present) count++;
    }
    return count;
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
// Для автоматической регистрации в kinit
static const char __disk_name[] __attribute__((section(".kinit.modules"))) = "disk_init";
static void* __disk_func __attribute__((section(".kinit.modules"))) = disk_init;
