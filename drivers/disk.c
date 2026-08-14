// File: drivers/disk.c
#include "disk.h"
#include "../include/ahci.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../drivers/drm.h"

typedef struct {
    int port;
    u64 sectors;
    char model[41];
    int present;
} disk_t;

static disk_t disks[4];
static int current_disk = 0;

// Статический выровненный буфер для DMA
__attribute__((aligned(16))) static u8 disk_dma_buf[512];

void ahci_register_disk(int port, u64 sectors, const char* model) {
    if (port < 0 || port >= 4) return;
    disks[port].port = port;
    disks[port].sectors = sectors;
    strncpy(disks[port].model, model, 40);
    disks[port].model[40] = '\0';
    disks[port].present = 1;
}

int disk_init(void) {
    memset(disks, 0, sizeof(disks));
    ahci_init();
    return 0;
}

int disk_set_disk(int n) {
    if (n < 0 || n >= 4) return -1;
    if (!disks[n].present) return -1;
    current_disk = n;
    return 0;
}

int disk_set_drive(u8 drive) { return disk_set_disk(drive - 0x80); }

int disk_read(u32 lba, u8* buffer) {
    disk_t* d = &disks[current_disk];
    if (!d->present) return -1;
    int res = ahci_read(d->port, lba, 1, disk_dma_buf);
    if (res == 0) memcpy(buffer, disk_dma_buf, 512);
    return res;
}

int disk_write(u32 lba, u8* buffer) {
    disk_t* d = &disks[current_disk];
    if (!d->present) return -1;
    memcpy(disk_dma_buf, buffer, 512);
    return ahci_write(d->port, lba, 1, disk_dma_buf);
}

int disk_get_disk_count(void) {
    int c = 0; for (int i = 0; i < 4; i++) if (disks[i].present) c++; return c;
}

int disk_get_boot_device(void) {
    for (int i = 0; i < 4; i++) if (disks[i].present) return i; return -1;
}

u64 disk_get_sectors(u8 drive) {
    int i = drive - 0x80;
    return (i < 0 || i >= 4) ? 0 : disks[i].sectors;
}

void disk_get_model(u8 drive, char* model) {
    int i = drive - 0x80;
    if (i < 0 || i >= 4) { strcpy(model, "Unknown"); return; }
    strcpy(model, disks[i].model);
}

void disk_list_disks(void) {
    for (int i = 0; i < 4; i++) {
        if (disks[i].present) {
            print("  sd"); print_char('a' + i); print(": ");
            print(disks[i].model); print(" (");
            printnum((u32)(disks[i].sectors / 2048)); print(" MB)\n");
        }
    }
}

static const char __disk_name[] __attribute__((section(".module_name"))) = "disk";
static int (*__disk_entry)(void) __attribute__((section(".module_entry"))) = disk_init;
