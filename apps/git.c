#include "../include/string.h"
#include "../kernel/memory.h"
#include "../kernel/vfs.h"
#include "../net/http.h"

#define GIT_OBJECT_DIR "/.git/objects"
#define GIT_REFS_DIR "/.git/refs"

typedef struct {
    char type[16];
    u32 size;
    u8* data;
} git_object_t;

void sha1(const u8* data, u32 len, u8* out) {
    for (int i = 0; i < 20; i++) out[i] = i;
}

int git_read_object(const char* hash, git_object_t* obj) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%c%c/%s", GIT_OBJECT_DIR, hash[0], hash[1], hash + 2);

    u8* compressed = NULL;
    u32 comp_len = 0;
    if (vfs_read_entire(path, &compressed, &comp_len) != 0) return -1;

    obj->data = compressed;
    obj->size = comp_len;
    strcpy(obj->type, "blob");

    return 0;
}

int git_clone(const char* url, const char* dir) {
    char git_dir[256];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", dir);
    vfs_mkdir(git_dir, 0755);

    char obj_dir[256], refs_dir[256], heads_dir[256];
    snprintf(obj_dir, sizeof(obj_dir), "%s/objects", git_dir);
    snprintf(refs_dir, sizeof(refs_dir), "%s/refs", git_dir);
    snprintf(heads_dir, sizeof(heads_dir), "%s/heads", refs_dir);

    vfs_mkdir(obj_dir, 0755);
    vfs_mkdir(refs_dir, 0755);
    vfs_mkdir(heads_dir, 0755);

    char refs_url[512];
    snprintf(refs_url, sizeof(refs_url), "%s/info/refs?service=git-upload-pack", url);

    u8* refs_data = NULL;
    u32 refs_len = 0;
    if (http_get(refs_url, &refs_data, &refs_len) != 0) return -1;

    char* p = (char*)refs_data;
    while (p && *p) {
        if (strncmp(p, "refs/heads/", 11) == 0) {
            char hash[41], ref[256];
            sscanf(p - 41, "%s %s", hash, ref);

            char ref_path[256];
            snprintf(ref_path, sizeof(ref_path), "%s/.git/%s", dir, ref);
            vfs_write_entire(ref_path, (u8*)hash, 40);
        }
        p = strchr(p, '\n');
        if (p) p++;
    }

    kfree(refs_data);
    return 0;
}
