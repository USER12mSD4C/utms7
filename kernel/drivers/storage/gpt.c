#include "../../../include/gpt.h"
#include "../../core/vfs.h"
#include "../../core/memory.h"
#include "../../../include/string.h"

static const u8 ufs_guid[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
static const u8 efi_guid[16] = {0xC1, 0x2A, 0x73, 0x28, 0xF8, 0x1F, 0x11, 0xD2, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
static const u8 linux_guid[16] = {0x0F, 0xC6, 0x3D, 0xAF, 0x84, 0x83, 0x47, 0x72, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
static const u8 empty_guid[16] = {0};

const u8* gpt_get_ufs_guid(void) { return ufs_guid; }
const u8* gpt_get_efi_guid(void) { return efi_guid; }
const u8* gpt_get_linux_guid(void) { return linux_guid; }
const u8* gpt_get_empty_guid(void) { return empty_guid; }

static u32 gpt_crc32(const void* data, u32 len) {
    u32 crc = 0xFFFFFFFF;
    const u8* p = (const u8*)data;

    for (u32 i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

extern int udisk_add_partition(int disk, int part_num, u64 start_lba, u64 end_lba, int type);

int gpt_parse(vfs_node_t* dev_node, int disk_num) {
    if (!dev_node || dev_node->type != VFS_BLOCK_DEVICE) return -1;

    u8 sector[512] __attribute__((aligned(16)));

    if (vfs_read(dev_node, sector, 512, 0) != 512) return -1;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return -1;

    int gpt_protective = 0;
    for (int i = 0; i < 4; i++) {
        if (sector[446 + i * 16 + 4] == 0xEE) {
            gpt_protective = 1;
            break;
        }
    }
    if (!gpt_protective) return -1;

    if (vfs_read(dev_node, sector, 512, 512) != 512) return -1;

    gpt_header_t hdr;
    memcpy(&hdr, sector, sizeof(hdr));

    if (hdr.signature != GPT_HEADER_SIGNATURE) return -1;

    u32 entries_per_sector = 512 / hdr.partition_entry_size;
    if (entries_per_sector == 0) entries_per_sector = 4;

    u32 sectors_needed = (hdr.num_partition_entries + entries_per_sector - 1) / entries_per_sector;
    u8* entries_buf = kmalloc(sectors_needed * 512);
    if (!entries_buf) return -1;

    if (vfs_read(dev_node, entries_buf, sectors_needed * 512, hdr.partition_entry_lba * 512) != (int)(sectors_needed * 512)) {
        kfree(entries_buf);
        return -1;
    }

    int part_num = 1;
    for (u32 i = 0; i < hdr.num_partition_entries && part_num <= 16; i++) {
        gpt_entry_t* ent = (gpt_entry_t*)(entries_buf + i * hdr.partition_entry_size);

        int empty = 1;
        for (int j = 0; j < 16; j++) {
            if (ent->partition_type_guid[j] != 0) {
                empty = 0;
                break;
            }
        }
        if (empty) continue;

        int type = 0;
        if (memcmp(ent->partition_type_guid, ufs_guid, 16) == 0) type = 1;
        else if (memcmp(ent->partition_type_guid, efi_guid, 16) == 0) type = 2;
        else if (memcmp(ent->partition_type_guid, linux_guid, 16) == 0) type = 3;

        udisk_add_partition(disk_num, part_num++, ent->starting_lba, ent->ending_lba, type);
    }

    kfree(entries_buf);
    return 0;
}

int gpt_create_table(vfs_node_t* dev_node) {
    if (!dev_node || dev_node->type != VFS_BLOCK_DEVICE) return -1;

    u8 sector[512] __attribute__((aligned(16)));

    memset(sector, 0, 512);
    sector[510] = 0x55;
    sector[511] = 0xAA;
    sector[446 + 4] = 0xEE;
    sector[446 + 8] = 0x01;
    sector[446 + 12] = 0xFF;
    sector[446 + 13] = 0xFF;
    sector[446 + 14] = 0xFF;
    sector[446 + 15] = 0xFF;

    if (vfs_write(dev_node, sector, 512, 0) != 512) return -1;

    memset(sector, 0, 512);
    gpt_header_t* hdr = (gpt_header_t*)sector;
    hdr->signature = GPT_HEADER_SIGNATURE;
    hdr->revision = 0x00010000;
    hdr->header_size = 92;
    hdr->my_lba = 1;
    hdr->alternate_lba = 0;
    hdr->first_usable_lba = 34;
    hdr->last_usable_lba = 0xFFFFFFFFFFFFFFFFULL;
    hdr->partition_entry_lba = 2;
    hdr->num_partition_entries = 128;
    hdr->partition_entry_size = 128;

    u32 crc = gpt_crc32(hdr, 92);
    hdr->header_crc32 = crc;

    if (vfs_write(dev_node, sector, 512, 512) != 512) return -1;

    memset(sector, 0, 512);
    for (int i = 2; i < 34; i++) {
        if (vfs_write(dev_node, sector, 512, i * 512) != 512) return -1;
    }

    return 0;
}

int gpt_add_partition(vfs_node_t* dev_node, u64 start_lba, u64 size_sectors, const u8* type_guid) {
    if (!dev_node || dev_node->type != VFS_BLOCK_DEVICE) return -1;
    if (size_sectors == 0) return -1;

    u8 sector[512] __attribute__((aligned(16)));

    if (vfs_read(dev_node, sector, 512, 512) != 512) return -1;

    gpt_header_t hdr;
    memcpy(&hdr, sector, sizeof(hdr));

    if (hdr.signature != GPT_HEADER_SIGNATURE) return -1;

    u32 entries_per_sector = 512 / hdr.partition_entry_size;
    if (entries_per_sector == 0) entries_per_sector = 4;

    u32 sectors_needed = (hdr.num_partition_entries + entries_per_sector - 1) / entries_per_sector;

    int modified_sector = -1;
    int modified_entry = -1;

    for (u32 s = 0; s < sectors_needed; s++) {
        u8 sec[512];
        if (vfs_read(dev_node, sec, 512, (hdr.partition_entry_lba + s) * 512) != 512) return -1;

        for (u32 j = 0; j < entries_per_sector && (s * entries_per_sector + j) < hdr.num_partition_entries; j++) {
            gpt_entry_t* ent = (gpt_entry_t*)(sec + j * hdr.partition_entry_size);

            int empty = 1;
            for (int k = 0; k < 16; k++) {
                if (ent->partition_type_guid[k] != 0) {
                    empty = 0;
                    break;
                }
            }

            if (empty) {
                memcpy(ent->partition_type_guid, type_guid, 16);
                for (int k = 0; k < 16; k++) ent->unique_partition_guid[k] = 0;
                ent->starting_lba = start_lba;
                ent->ending_lba = start_lba + size_sectors - 1;
                ent->attributes = 0;
                memset(ent->partition_name, 0, sizeof(ent->partition_name));

                modified_sector = s;
                modified_entry = j;

                if (vfs_write(dev_node, sec, 512, (hdr.partition_entry_lba + s) * 512) != 512) {
                    return -1;
                }

                goto recalc_crc;
            }
        }
    }

    return -1;

recalc_crc:
    u8* all_entries = kmalloc(sectors_needed * 512);
    if (!all_entries) return -1;

    if (vfs_read(dev_node, all_entries, sectors_needed * 512, hdr.partition_entry_lba * 512) != (int)(sectors_needed * 512)) {
        kfree(all_entries);
        return -1;
    }

    hdr.partition_entries_crc32 = gpt_crc32(all_entries, hdr.num_partition_entries * hdr.partition_entry_size);
    kfree(all_entries);

    hdr.header_crc32 = 0;
    hdr.header_crc32 = gpt_crc32(&hdr, 92);

    memcpy(sector, &hdr, sizeof(hdr));
    if (vfs_write(dev_node, sector, 512, 512) != 512) {
        return -1;
    }

    return 0;
}
