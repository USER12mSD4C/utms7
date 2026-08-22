#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void put_u64(unsigned long long v) {
    char buf[24];
    int i = 0;

    if (v == 0) {
        out("0");
        return;
    }

    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (i-- > 0) {
        char c = buf[i];
        write(1, &c, 1);
    }
}

int main(void) {
    disk_info_user_t disks[4];
    int n = disk_list(disks, 4);

    if (n <= 0) {
        out("no disks\n");
        return 0;
    }

    for (int i = 0; i < n; i++) {
        if (!disks[i].present) continue;

        unsigned long long ss = disks[i].sector_size ? disks[i].sector_size : 512;

        out("/dev/sd");
        char c = 'a' + i;
        write(1, &c, 1);
        out("  ");
        put_u64((disks[i].total_sectors * ss + (1024 * 1024 - 1)) / (1024 * 1024));
        out(" MB  ");
        out(disks[i].model);
        out(disks[i].is_gpt ? "  GPT\n" : "  MBR\n");

        for (int j = 0; j < disks[i].partition_count; j++) {
            if (!disks[i].partitions[j].present) continue;

            out("  /dev/sd");
            char c2 = 'a' + i;
            write(1, &c2, 1);
            put_u64(disks[i].partitions[j].partition_num);
            out("  ");
            put_u64((disks[i].partitions[j].size + (1024 * 1024 - 1)) / (1024 * 1024));
            out(" MB  ");

            switch (disks[i].partitions[j].type) {
                case 1:
                    out("UFS\n");
                    break;
                case 2:
                    out("FAT32\n");
                    break;
                case 3:
                    out("EXT4\n");
                    break;
                default:
                    out("unknown\n");
                    break;
            }
        }
    }

    return 0;
}
