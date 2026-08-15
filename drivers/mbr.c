// File: drivers/mbr.c
#include "../include/mbr.h"
#include "../include/string.h"
#include "disk.h"
#include "../include/print.h"

static mbr_entry_t mbr_entries[32];
static int mbr_entry_count = 0;
static u32 mbr_extended_lba = 0;
static u8 mbr_current_drive = 0;

static int mbr_read(u8 drive, u32 lba, u8* buf) {
    int disk_num = drive - 0x80;
    if (disk_num < 0 || disk_num >= 4) return -1;
    disk_set_disk(disk_num);
    return disk_read(lba, buf);
}

static int mbr_write(u8 drive, u32 lba, u8* buf) {
    int disk_num = drive - 0x80;
    if (disk_num < 0 || disk_num >= 4) return -1;
    disk_set_disk(disk_num);
    return disk_write(lba, buf);
}

static void lba_to_chs(u32 lba, u8* chs) {
    u32 c = lba / (16 * 63);
    u32 h = (lba / 63) % 16;
    u32 s = (lba % 63) + 1;
    if (c > 1023) c = 1023;
    chs[0] = (u8)h;
    chs[1] = (u8)((c >> 8) | (s & 0x3F));
    chs[2] = (u8)(c & 0xFF);
}

int mbr_detect(u8 drive) {
    u8 sector[512];
    if (mbr_read(drive, 0, sector) != 0) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    for (int i = 0; i < 4; i++) {
        if (sector[446 + i*16 + 4] == 0xEE) return 0;
    }
    return 1;
}

int mbr_create(u8 drive) {
    u8 sector[512];
    memset(sector, 0, 512);
    sector[510] = 0x55;
    sector[511] = 0xAA;
    return mbr_write(drive, 0, sector);
}

int mbr_read_partitions(u8 drive) {
    u8 sector[512];
    mbr_current_drive = drive;
    mbr_entry_count = 0;
    mbr_extended_lba = 0;

    if (mbr_read(drive, 0, sector) != 0) return -1;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return -1;

    for (int i = 0; i < 4; i++) {
        u8* entry = sector + 446 + i * 16;
        u8 type = entry[4];
        if (type == 0) continue;
        if (type == 0xEE) continue;

        mbr_entry_t* e = &mbr_entries[mbr_entry_count];
        e->bootable = entry[0];
        e->type = type;
        e->lba_start = *(u32*)(entry + 8);
        e->lba_count = *(u32*)(entry + 12);

        if (type == 0x05 || type == 0x0F || type == 0x85) {
            mbr_extended_lba = e->lba_start;
            continue;
        }

        mbr_entry_count++;
    }

    u32 current_ebr = mbr_extended_lba;
    while (current_ebr != 0 && mbr_entry_count < 32) {
        if (mbr_read(drive, current_ebr, sector) != 0) break;
        if (sector[510] != 0x55 || sector[511] != 0xAA) break;

        u8* entry0 = sector + 446;
        u8 type0 = entry0[4];
        u32 start0 = *(u32*)(entry0 + 8);
        u32 size0 = *(u32*)(entry0 + 12);

        if (type0 != 0 && size0 > 0) {
            mbr_entry_t* e = &mbr_entries[mbr_entry_count];
            e->bootable = 0;
            e->type = type0;
            e->lba_start = current_ebr + start0;
            e->lba_count = size0;
            mbr_entry_count++;
        }

        u8* entry1 = sector + 446 + 16;
        u8 type1 = entry1[4];
        u32 start1 = *(u32*)(entry1 + 8);

        if (type1 == 0x05 || type1 == 0x0F || type1 == 0x85) {
            current_ebr = mbr_extended_lba + start1;
        } else {
            break;
        }
    }

    return mbr_entry_count;
}

int mbr_get_entry(int index, mbr_entry_t* entry) {
    if (index < 0 || index >= mbr_entry_count) return -1;
    memcpy(entry, &mbr_entries[index], sizeof(mbr_entry_t));
    return 0;
}

int mbr_add_partition(u8 drive, u64 start_lba, u64 size_lba, u8 type) {
    u8 sector[512];
    if (mbr_read(drive, 0, sector) != 0) return -1;

    int free_entry = -1;
    for (int i = 0; i < 4; i++) {
        u8* entry = sector + 446 + i * 16;
        if (entry[4] == 0) {
            free_entry = i;
            break;
        }
    }

    if (free_entry == -1) return -1;

    u8* entry = sector + 446 + free_entry * 16;
    memset(entry, 0, 16);
    entry[0] = 0x00;
    lba_to_chs((u32)start_lba, &entry[1]);
    entry[4] = type;
    lba_to_chs((u32)(start_lba + size_lba - 1), &entry[5]);
    *(u32*)(entry + 8) = (u32)start_lba;
    *(u32*)(entry + 12) = (u32)size_lba;

    if (mbr_write(drive, 0, sector) != 0) return -1;

    u8 verify[512];
    if (mbr_read(drive, 0, verify) != 0) return -1;
    if (memcmp(sector, verify, 512) != 0) return -1;

    return 0;
}

int mbr_delete_partition(u8 drive, int index) {
    u8 sector[512];
    if (mbr_read(drive, 0, sector) != 0) return -1;

    int current = 0;
    for (int i = 0; i < 4; i++) {
        u8* entry = sector + 446 + i * 16;
        if (entry[4] != 0 && entry[4] != 0xEE) {
            if (current == index) {
                memset(entry, 0, 16);
                return mbr_write(drive, 0, sector);
            }
            current++;
        }
    }
    return -1;
}

int mbr_set_type(u8 drive, int index, u8 type) {
    u8 sector[512];
    if (mbr_read(drive, 0, sector) != 0) return -1;

    int current = 0;
    for (int i = 0; i < 4; i++) {
        u8* entry = sector + 446 + i * 16;
        if (entry[4] != 0 && entry[4] != 0xEE) {
            if (current == index) {
                entry[4] = type;
                return mbr_write(drive, 0, sector);
            }
            current++;
        }
    }
    return -1;
}

int mbr_init(void) { return 0; }

static const char __mbr_name[] __attribute__((section(".module_name"))) = "mbr";
static int (*__mbr_entry)(void) __attribute__((section(".module_entry"))) = mbr_init;
