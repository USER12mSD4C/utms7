#include "../include/string.h"
#include "../kernel/memory.h"
#include "../fs/ufs.h"
#include "../net/http.h"

#define GIT_OBJECT_DIR "/.git/objects"
#define GIT_REFS_DIR "/.git/refs"

typedef struct {
    char type[16]; // blob, tree, commit, tag
    u32 size;
    u8 *data;
} git_object_t;

// Вычисление SHA-1 (заглушка - нужна реальная реализация)
void sha1(const u8 *data, u32 len, u8 *out) {
    // TODO: настоящий SHA-1
    for (int i = 0; i < 20; i++) out[i] = i;
}

// Чтение объекта из .git/objects
int git_read_object(const char *hash, git_object_t *obj) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%c%c/%s",
             GIT_OBJECT_DIR, hash[0], hash[1], hash + 2);

    u8 *compressed;
    u32 comp_len;
    if (ufs_read(path, &compressed, &comp_len) != 0) return -1;

    // TODO: распаковка zlib
    // Пока просто копируем
    obj->data = compressed;
    obj->size = comp_len;
    strcpy(obj->type, "blob");

    return 0;
}

// Клонирование репозитория
int git_clone(const char *url, const char *dir) {
    // 1. Создаем .git директории
    char git_dir[256];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", dir);
    ufs_mkdir(git_dir);
    ufs_mkdir(git_dir + sizeof("/objects"));
    ufs_mkdir(git_dir + sizeof("/refs"));
    ufs_mkdir(git_dir + sizeof("/refs/heads"));

    // 2. Получаем info/refs
    char refs_url[512];
    snprintf(refs_url, sizeof(refs_url), "%s/info/refs?service=git-upload-pack", url);

    u8 *refs_data;
    u32 refs_len;
    if (http_get(refs_url, &refs_data, &refs_len) != 0) return -1;

    // 3. Парсим refs и сохраняем
    char *p = (char*)refs_data;
    while (p && *p) {
        if (strncmp(p, "refs/heads/", 11) == 0) {
            char hash[41];
            char ref[256];
            sscanf(p - 41, "%s %s", hash, ref);

            char ref_path[256];
            snprintf(ref_path, sizeof(ref_path), "%s/.git/%s", dir, ref);
            ufs_write(ref_path, (u8*)hash, 40);
        }
        p = strchr(p, '\n');
        if (p) p++;
    }

    kfree(refs_data);
    return 0;
}
