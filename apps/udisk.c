#include "../lib/libc.h"

static void out(const char *s) { write(1, s, strlen(s)); }

static void put_u64(unsigned long long v) {
    char buf[24]; int i = 0;
    if (v == 0) { out("0"); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i-- > 0) { char c = buf[i]; write(1, &c, 1); }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        out("Usage: udisk <cmd> [args]\n");
        out("Commands:\n");
        out("  list / lsblk            - show disks\n");
        out("  mbr /dev/sdX            - create MBR table\n");
        out("  gpt /dev/sdX            - create GPT table\n");
        out("  create /dev/sdX <size> [type] - create partition\n");
        out("  delete /dev/sdX[1-16]   - delete partition\n");
        out("  mount /dev/sdX[1-16] [point] - mount ufs partition\n");
        out("  umount                  - unmount ufs\n");
        out("  mkfs /dev/sdX[1-16]     - format partition as ufs\n");
        return 1;
    }

    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "lsblk") == 0) {
        disk_info_user_t disks[4];
        int n = disk_list(disks, 4);
        if (n <= 0) { out("no disks\n"); return 0; }
        for (int i = 0; i < n; i++) {
            if (!disks[i].present) continue;
            out("/dev/sd"); char c = 'a' + i; write(1, &c, 1); out("  ");
            put_u64(disks[i].total_sectors * 512 / (1024*1024)); out(" MB  ");
            out(disks[i].model); out(disks[i].is_gpt ? "  GPT\n" : "  MBR\n");
            for (int j = 0; j < disks[i].partition_count; j++) {
                if (!disks[i].partitions[j].present) continue;
                out("  /dev/sd"); char c2 = 'a' + i; write(1, &c2, 1);
                put_u64(disks[i].partitions[j].partition_num); out("  ");
                put_u64(disks[i].partitions[j].size / (1024*1024)); out(" MB  ");
                switch(disks[i].partitions[j].type) {
                    case PARTITION_UFS: out("UFS\n"); break;
                    case PARTITION_FAT32: out("FAT32\n"); break;
                    case PARTITION_EXT4: out("EXT4\n"); break;
                    default: out("unknown\n"); break;
                }
            }
        }
        return 0;
    }

    if (strcmp(argv[1], "mbr") == 0) {
        if (argc < 3) { out("Usage: udisk mbr /dev/sdX\n"); return 1; }
        if (disk_create_table(argv[2], 0) == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    if (strcmp(argv[1], "gpt") == 0) {
        if (argc < 3) { out("Usage: udisk gpt /dev/sdX\n"); return 1; }
        if (disk_create_table(argv[2], 1) == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    if (strcmp(argv[1], "create") == 0) {
        if (argc < 4) { out("Usage: udisk create /dev/sdX <sizeMB> [type]\n"); return 1; }
        unsigned long mb = 0;
        for (char *p = argv[3]; *p; p++) {
            if (*p < '0' || *p > '9') { out("invalid size\n"); return 1; }
            mb = mb * 10 + (unsigned long)(*p - '0');
        }
        int type = 1;
        if (argc >= 5) {
            if (strcmp(argv[4], "fat32") == 0) type = 2;
            else if (strcmp(argv[4], "ext4") == 0) type = 3;
        }
        if (partition_create(argv[2], mb, type) == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) { out("Usage: udisk delete /dev/sdX[1-16]\n"); return 1; }
        if (partition_delete(argv[2]) == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    if (strcmp(argv[1], "mount") == 0) {
        if (argc < 3) { out("Usage: udisk mount /dev/sdX[1-16] [point]\n"); return 1; }
        const char* point = (argc >= 4) ? argv[3] : "/";
        if (partition_mount(argv[2], point) == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    if (strcmp(argv[1], "umount") == 0) {
        if (partition_umount() == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    if (strcmp(argv[1], "mkfs") == 0 || strcmp(argv[1], "mkfs.ufs") == 0) {
        if (argc < 3) { out("Usage: udisk mkfs /dev/sdX[1-16]\n"); return 1; }
        if (partition_format(argv[2], "ufs") == 0) out("OK\n"); else out("FAILED\n");
        return 0;
    }

    out("unknown udisk command\n");
    return 1;
}
