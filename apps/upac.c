#include "../../include/string.h"
#include "../../kernel/memory.h"
#include "../../fs/ufs.h"
#include "../../net/http.h"
#include "../../lib/zlib.h"
#include "../../drivers/vga.h"

#define UPAC_ROOT "/var/lib/upac"
#define UPAC_DB UPAC_ROOT "/core.db"
#define UPAC_INSTALLED UPAC_ROOT "/installed"
#define UPAC_REPO "raw.githubusercontent.com"
#define UPAC_REPO_PATH "/user12ms/upac-repo/main"

typedef struct {
    char name[64];
    char version[32];
    char desc[256];
    u32 deps_count;
    char deps[8][64];
    u32 files_count;
    struct {
        char path[256];
        u32 size;
        u32 crc;
    } files[128];
} upac_pkg_t;

static void upac_print(const char *s) {
    vga_write(s);
}

static void upac_print_num(u32 n) {
    vga_write_num(n);
}

static int upac_mkdir_recursive(const char *path) {
    char tmp[256];
    char *p = NULL;
    
    strcpy(tmp, path);
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!ufs_exists(tmp)) {
                if (ufs_mkdir(tmp) != 0) return -1;
            }
            *p = '/';
        }
    }
    
    if (!ufs_exists(tmp)) {
        if (ufs_mkdir(tmp) != 0) return -1;
    }
    
    return 0;
}

static int upac_download(const char *host, const char *path, u8 **data, u32 *size) {
    char url[512];
    snprintf(url, sizeof(url), "http://%s%s", host, path);
    
    upac_print("  Downloading ");
    upac_print(url);
    upac_print("... ");
    
    if (http_get(url, data, size) != 0) {
        upac_print("FAILED\n");
        return -1;
    }
    
    upac_print("OK (");
    upac_print_num(*size);
    upac_print(" bytes)\n");
    
    return 0;
}

static int upac_sync(void) {
    upac_print(":: Synchronizing package database...\n");
    
    ufs_mkdir(UPAC_ROOT);
    ufs_mkdir(UPAC_INSTALLED);
    
    u8 *db_gz;
    u32 db_gz_len;
    
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "%s/core.db.tar.gz", UPAC_REPO_PATH);
    
    if (upac_download(UPAC_REPO, db_path, &db_gz, &db_gz_len) != 0) {
        return -1;
    }
    
    upac_print("  Extracting database... ");
    
    u8 *db_raw;
    u32 db_raw_len;
    if (zlib_inflate(db_gz, db_gz_len, &db_raw, &db_raw_len) != 0) {
        upac_print("FAILED\n");
        kfree(db_gz);
        return -1;
    }
    
    kfree(db_gz);
    
    if (ufs_rewrite(UPAC_DB, db_raw, db_raw_len) != 0) {
        upac_print("FAILED\n");
        kfree(db_raw);
        return -1;
    }
    
    kfree(db_raw);
    upac_print("OK\n");
    upac_print(":: Database updated\n");
    
    return 0;
}

static int upac_db_find(const char *name, upac_pkg_t *pkg) {
    u8 *db_data;
    u32 db_len;
    
    if (ufs_read(UPAC_DB, &db_data, &db_len) != 0) return -1;
    
    char *p = (char*)db_data;
    char *end = p + db_len;
    
    while (p < end) {
        char *pkgname = p;
        p += strlen(p) + 1;
        if (p >= end) break;
        
        char *version = p;
        p += strlen(p) + 1;
        if (p >= end) break;
        
        char *desc = p;
        p += strlen(p) + 1;
        if (p >= end) break;
        
        if (strcmp(pkgname, name) == 0) {
            strcpy(pkg->name, pkgname);
            strcpy(pkg->version, version);
            strcpy(pkg->desc, desc);
            
            pkg->deps_count = 0;
            while (p < end && *p) {
                strcpy(pkg->deps[pkg->deps_count++], p);
                p += strlen(p) + 1;
            }
            p++;
            
            pkg->files_count = 0;
            while (p < end && *p) {
                strcpy(pkg->files[pkg->files_count].path, p);
                p += strlen(p) + 1;
                if (p >= end) break;
                
                pkg->files[pkg->files_count].size = 0;
                while (*p >= '0' && *p <= '9') {
                    pkg->files[pkg->files_count].size = 
                        pkg->files[pkg->files_count].size * 10 + (*p - '0');
                    p++;
                }
                p++;
                
                if (p >= end) break;
                
                pkg->files[pkg->files_count].crc = 0;
                while (*p >= '0' && *p <= '9') {
                    pkg->files[pkg->files_count].crc = 
                        pkg->files[pkg->files_count].crc * 10 + (*p - '0');
                    p++;
                }
                p++;
                
                pkg->files_count++;
            }
            
            kfree(db_data);
            return 0;
        }
        
        while (p < end && *p) p += strlen(p) + 1;
        p++;
        
        while (p < end && *p) {
            p += strlen(p) + 1;
            if (p >= end) break;
            p += strlen(p) + 1;
            if (p >= end) break;
            p += strlen(p) + 1;
        }
        p++;
    }
    
    kfree(db_data);
    return -1;
}

static int upac_install_deps(upac_pkg_t *pkg) {
    for (u32 i = 0; i < pkg->deps_count; i++) {
        char dep_path[256];
        snprintf(dep_path, sizeof(dep_path), "%s/%s", UPAC_INSTALLED, pkg->deps[i]);
        
        if (!ufs_exists(dep_path)) {
            upac_print("  Installing dependency: ");
            upac_print(pkg->deps[i]);
            upac_print("\n");
            
            if (upac_install(pkg->deps[i]) != 0) return -1;
        }
    }
    
    return 0;
}

int upac_install(const char *name) {
    upac_pkg_t pkg;
    
    upac_print(":: Installing ");
    upac_print(name);
    upac_print("...\n");
    
    if (upac_db_find(name, &pkg) != 0) {
        upac_print("  Package not found\n");
        return -1;
    }
    
    upac_print("  Version: ");
    upac_print(pkg.version);
    upac_print("\n");
    
    upac_print("  Description: ");
    upac_print(pkg.desc);
    upac_print("\n");
    
    if (upac_install_deps(&pkg) != 0) {
        upac_print("  Failed to install dependencies\n");
        return -1;
    }
    
    char pkg_url[512];
    char pkg_path[256];
    snprintf(pkg_path, sizeof(pkg_path), "%s/packages/%s-%s.upac", 
             UPAC_REPO_PATH, pkg.name, pkg.version);
    
    u8 *pkg_data;
    u32 pkg_len;
    if (upac_download(UPAC_REPO, pkg_path, &pkg_data, &pkg_len) != 0) {
        return -1;
    }
    
    upac_print("  Extracting package... ");
    
    u8 *pkg_raw;
    u32 pkg_raw_len;
    if (zlib_inflate(pkg_data, pkg_len, &pkg_raw, &pkg_raw_len) != 0) {
        upac_print("FAILED\n");
        kfree(pkg_data);
        return -1;
    }
    
    kfree(pkg_data);
    
    char *fp = (char*)pkg_raw;
    char *fend = fp + pkg_raw_len;
    
    u32 installed_files = 0;
    
    while (fp < fend && *fp) {
        char *filepath = fp;
        fp += strlen(fp) + 1;
        if (fp >= fend) break;
        
        u32 filesize = 0;
        while (*fp >= '0' && *fp <= '9') {
            filesize = filesize * 10 + (*fp - '0');
            fp++;
        }
        fp++;
        
        if (fp >= fend) break;
        
        u8 *filedata = (u8*)fp;
        fp += filesize;
        
        upac_mkdir_recursive(filepath);
        
        if (ufs_write(filepath, filedata, filesize) == 0) {
            installed_files++;
        }
    }
    
    kfree(pkg_raw);
    
    upac_print("OK (");
    upac_print_num(installed_files);
    upac_print(" files)\n");
    
    char installed_path[256];
    snprintf(installed_path, sizeof(installed_path), "%s/%s", UPAC_INSTALLED, pkg.name);
    
    char info[1024];
    int pos = 0;
    pos += snprintf(info + pos, sizeof(info) - pos, "%s\n", pkg.name);
    pos += snprintf(info + pos, sizeof(info) - pos, "%s\n", pkg.version);
    pos += snprintf(info + pos, sizeof(info) - pos, "%s\n", pkg.desc);
    
    for (u32 i = 0; i < pkg.files_count; i++) {
        pos += snprintf(info + pos, sizeof(info) - pos, "%s\n", pkg.files[i].path);
    }
    
    ufs_write(installed_path, (u8*)info, pos);
    
    upac_print(":: Done.\n");
    
    return 0;
}

int upac_remove(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", UPAC_INSTALLED, name);
    
    upac_print(":: Removing ");
    upac_print(name);
    upac_print("...\n");
    
    if (!ufs_exists(path)) {
        upac_print("  Package not installed\n");
        return -1;
    }
    
    u8 *info;
    u32 info_len;
    if (ufs_read(path, &info, &info_len) != 0) {
        return -1;
    }
    
    char *p = (char*)info;
    p += strlen(p) + 1;
    p += strlen(p) + 1;
    p += strlen(p) + 1;
    
    u32 removed = 0;
    while (p < (char*)info + info_len && *p) {
        if (ufs_delete(p) == 0) removed++;
        p += strlen(p) + 1;
    }
    
    kfree(info);
    
    ufs_delete(path);
    
    upac_print("  Removed ");
    upac_print_num(removed);
    upac_print(" files\n");
    upac_print(":: Done.\n");
    
    return 0;
}

int upac_query(void) {
    upac_print(":: Installed packages:\n");
    
    FSNode *entries;
    u32 count;
    
    if (ufs_readdir(UPAC_INSTALLED, &entries, &count) != 0) {
        upac_print("  none\n");
        return 0;
    }
    
    for (u32 i = 0; i < count; i++) {
        if (!entries[i].is_dir) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", UPAC_INSTALLED, entries[i].name);
            
            u8 *info;
            u32 info_len;
            if (ufs_read(path, &info, &info_len) == 0) {
                char *p = (char*)info;
                char *name = p;
                p += strlen(p) + 1;
                char *ver = p;
                
                upac_print("  ");
                upac_print(name);
                upac_print(" ");
                upac_print(ver);
                upac_print("\n");
                
                kfree(info);
            }
        }
    }
    
    kfree(entries);
    return 0;
}

int upac_upgrade(void) {
    upac_print(":: Starting full system upgrade...\n");
    
    if (upac_sync() != 0) return -1;
    
    FSNode *entries;
    u32 count;
    
    if (ufs_readdir(UPAC_INSTALLED, &entries, &count) != 0) {
        return 0;
    }
    
    for (u32 i = 0; i < count; i++) {
        if (!entries[i].is_dir) {
            upac_pkg_t pkg;
            if (upac_db_find(entries[i].name, &pkg) == 0) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%s", UPAC_INSTALLED, entries[i].name);
                
                u8 *info;
                u32 info_len;
                if (ufs_read(path, &info, &info_len) == 0) {
                    char *p = (char*)info;
                    p += strlen(p) + 1;
                    char *old_ver = p;
                    
                    if (strcmp(old_ver, pkg.version) != 0) {
                        upac_print("  Upgrading: ");
                        upac_print(pkg.name);
                        upac_print(" ");
                        upac_print(old_ver);
                        upac_print(" -> ");
                        upac_print(pkg.version);
                        upac_print("\n");
                        
                        upac_install(pkg.name);
                    }
                    
                    kfree(info);
                }
            }
        }
    }
    
    kfree(entries);
    
    upac_print(":: System is up to date\n");
    return 0;
}

int upac_main(int argc, char **argv) {
    if (argc < 2) {
        upac_print("usage: upac <command>\n");
        upac_print("\n");
        upac_print("  upac -Sy          sync database\n");
        upac_print("  upac -S <pkg>     install package\n");
        upac_print("  upac -R <pkg>     remove package\n");
        upac_print("  upac -Q            query installed\n");
        upac_print("  upac -Su           upgrade system\n");
        return 0;
    }
    
    if (strcmp(argv[1], "-Sy") == 0) {
        return upac_sync();
    }
    else if (strcmp(argv[1], "-S") == 0 && argc > 2) {
        return upac_install(argv[2]);
    }
    else if (strcmp(argv[1], "-R") == 0 && argc > 2) {
        return upac_remove(argv[2]);
    }
    else if (strcmp(argv[1], "-Q") == 0) {
        return upac_query();
    }
    else if (strcmp(argv[1], "-Su") == 0) {
        return upac_upgrade();
    }
    
    upac_print("unknown command\n");
    return -1;
}
