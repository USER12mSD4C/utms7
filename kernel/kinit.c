#include "../drivers/drm.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "memory.h"
#include "../include/io.h"
#include "../include/module.h"

// Внешние функции диска
extern int disk_read(u32 lba, u8* buffer);
extern int disk_write(u32 lba, u8* buffer);
extern int disk_set_disk(int n);
extern u64 disk_get_sectors(u8 drive);

static loaded_module_t* module_list = NULL;

// Таблица символов ядра (экспортируемые функции)
typedef struct {
    const char* name;
    void* addr;
} kernel_sym_t;

// Ядерные символы, доступные модулям
static kernel_sym_t kernel_syms[] = {
    // Память
    {"kmalloc", kmalloc},
    {"kfree", kfree},
    {"memory_used", memory_used},
    {"memory_free", memory_free},

    // Видео
    {"print", print},
    {"printnum", printnum},
    {"printhex", printhex},
    {"print_clear", print_clear},
    {"print_setcolor", print_setcolor},

    // Диск
    {"disk_read", disk_read},
    {"disk_write", disk_write},
    {"disk_set_disk", disk_set_disk},
    {"disk_get_sectors", disk_get_sectors},

    // ФС
    {"ufs_read", ufs_read},
    {"ufs_write", ufs_write},
    {"ufs_mkdir", ufs_mkdir},
    {"ufs_exists", ufs_exists},
    {"ufs_isdir", ufs_isdir},
    {"ufs_readdir", ufs_readdir},
    {"ufs_delete", ufs_delete},
    {"ufs_rmdir", ufs_rmdir},
    {"ufs_rmdir_force", ufs_rmdir_force},

    // Строки
    {"strcpy", strcpy},
    {"strncpy", strncpy},
    {"strcmp", strcmp},
    {"strncmp", strncmp},
    {"strlen", strlen},
    {"strchr", strchr},
    {"strrchr", strrchr},
    {"strstr", strstr},
    {"strcat", strcat},
    {"memcpy", memcpy},
    {"memset", memset},
    {"memcmp", memcmp},
    {"sprintf", sprintf},
    {"snprintf", snprintf},

    {NULL, NULL}
};

// Поиск символа в ядре и загруженных модулях
static void* resolve_symbol(const char* name) {
    // Ищем в ядре
    for (kernel_sym_t* ks = kernel_syms; ks->name != NULL; ks++) {
        if (strcmp(ks->name, name) == 0) {
            return ks->addr;
        }
    }

    // Ищем в загруженных модулях
    loaded_module_t* mod = module_list;
    while (mod) {
        if (mod->symtab && mod->strtab) {
            for (u32 i = 0; i < mod->symtab_count; i++) {
                char* sym_name = mod->strtab + mod->symtab[i].name_offset;
                if (strcmp(sym_name, name) == 0) {
                    // Глобальные определенные символы
                    if (mod->symtab[i].type == 1) {
                        return mod->text_base + mod->symtab[i].value_offset;
                    }
                }
            }
        }
        mod = mod->next;
    }

    return NULL;
}

// Разрешение всех неопределенных символов в модуле
static int resolve_module_symbols(loaded_module_t* mod) {
    if (!mod->symtab || !mod->strtab) return 0;

    int resolved = 0;
    int unresolved = 0;

    for (u32 i = 0; i < mod->symtab_count; i++) {
        // UNDEF (type == 2) - символ должен быть определен где-то еще
        if (mod->symtab[i].type == 2) {
            char* sym_name = mod->strtab + mod->symtab[i].name_offset;
            void* addr = resolve_symbol(sym_name);

            if (addr) {
                // Записываем адрес в нужное место
                u64* target = (u64*)(mod->text_base + mod->symtab[i].value_offset);
                *target = (u64)addr;
                resolved++;
            } else {
                // Пропускаем специальные символы
                if (sym_name[0] != '_' && strcmp(sym_name, "_GLOBAL_OFFSET_TABLE_") != 0) {
                    print("  unresolved: ");
                    print(sym_name);
                    print("\n");
                    unresolved++;
                }
            }
        }
    }

    if (unresolved > 0) {
        return -1;
    }

    return 0;
}

// Загрузка модуля из файла
int module_load(const char* path) {
    u8* data;
    u32 size;

    if (ufs_read(path, &data, &size) != 0) {
        return -1;
    }

    module_header_t* hdr = (module_header_t*)data;
    if (hdr->magic != MODULE_MAGIC) {
        kfree(data);
        return -1;
    }

    // Проверяем размер
    if (size < sizeof(module_header_t)) {
        kfree(data);
        return -1;
    }

    // Проверяем, не загружен ли уже
    loaded_module_t* existing = module_list;
    while (existing) {
        if (strcmp(existing->name, hdr->name) == 0) {
            kfree(data);
            return -1;
        }
        existing = existing->next;
    }

    // Выделяем память под сегменты модуля
    u32 total = hdr->text_size + hdr->data_size + hdr->rodata_size + hdr->bss_size;
    u8* base = kmalloc(total);
    if (!base) {
        kfree(data);
        return -1;
    }

    // Копируем сегменты
    if (hdr->text_size > 0) {
        memcpy(base, data + hdr->text_offset, hdr->text_size);
    }
    if (hdr->data_size > 0) {
        memcpy(base + hdr->text_size, data + hdr->data_offset, hdr->data_size);
    }
    if (hdr->rodata_size > 0) {
        memcpy(base + hdr->text_size + hdr->data_size, data + hdr->rodata_offset, hdr->rodata_size);
    }
    if (hdr->bss_size > 0) {
        memset(base + hdr->text_size + hdr->data_size + hdr->rodata_size, 0, hdr->bss_size);
    }

    // Загружаем таблицу символов
    module_sym_t* symtab = NULL;
    char* strtab = NULL;

    if (hdr->symtab_count > 0 && hdr->symtab_offset > 0 && hdr->strtab_offset > 0) {
        symtab = kmalloc(hdr->symtab_count * sizeof(module_sym_t));
        if (symtab) {
            memcpy(symtab, data + hdr->symtab_offset, hdr->symtab_count * sizeof(module_sym_t));
        }

        strtab = kmalloc(hdr->strtab_size);
        if (strtab) {
            memcpy(strtab, data + hdr->strtab_offset, hdr->strtab_size);
        }
    }

    // Создаём запись о модуле
    loaded_module_t* mod = kmalloc(sizeof(loaded_module_t));
    if (!mod) {
        kfree(base);
        kfree(data);
        if (symtab) kfree(symtab);
        if (strtab) kfree(strtab);
        return -1;
    }

    strcpy(mod->name, hdr->name);
    mod->class = hdr->class;
    mod->version = hdr->version;
    mod->text_base = base;
    mod->data_base = base + hdr->text_size;
    mod->rodata_base = base + hdr->text_size + hdr->data_size;
    mod->bss_base = base + hdr->text_size + hdr->data_size + hdr->rodata_size;
    mod->text_size = hdr->text_size;
    mod->data_size = hdr->data_size;
    mod->rodata_size = hdr->rodata_size;
    mod->bss_size = hdr->bss_size;
    mod->entry = (int (*)(void))(base + hdr->entry_point);
    mod->symtab_count = hdr->symtab_count;
    mod->symtab = symtab;
    mod->strtab = strtab;
    mod->next = module_list;

    module_list = mod;

    kfree(data);

    // Разрешаем внешние символы
    if (resolve_module_symbols(mod) != 0) {
        return -1;
    }

    return 0;
}

// Поиск символа по имени во всех модулях
void* module_sym(const char* name, const char* module) {
    if (module) {
        // Ищем в конкретном модуле
        loaded_module_t* mod = module_list;
        while (mod) {
            if (strcmp(mod->name, module) == 0) {
                if (mod->symtab && mod->strtab) {
                    for (u32 i = 0; i < mod->symtab_count; i++) {
                        char* sym_name = mod->strtab + mod->symtab[i].name_offset;
                        if (strcmp(sym_name, name) == 0) {
                            return mod->text_base + mod->symtab[i].value_offset;
                        }
                    }
                }
                return NULL;
            }
            mod = mod->next;
        }
    } else {
        // Ищем везде
        return resolve_symbol(name);
    }
    return NULL;
}

// Выгрузка модуля
int module_unload(const char* name) {
    loaded_module_t* prev = NULL;
    loaded_module_t* mod = module_list;

    while (mod) {
        if (strcmp(mod->name, name) == 0) {
            if (prev) {
                prev->next = mod->next;
            } else {
                module_list = mod->next;
            }

            // Освобождаем память
            if (mod->text_base) kfree(mod->text_base);
            if (mod->symtab) kfree(mod->symtab);
            if (mod->strtab) kfree(mod->strtab);
            kfree(mod);

            return 0;
        }
        prev = mod;
        mod = mod->next;
    }
    return -1;
}

int kinit_run_all(void) {
    print("\nKinit: scanning /modules/\n");

    FSNode* entries;
    u32 count;

    if (ufs_readdir("/modules", &entries, &count) != 0) {
        print("  No /modules/ directory found\n");
    }

    // Сначала считаем сколько .ko файлов
    int module_count = 0;
    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;

        int len = strlen(entries[i].name);
        if (len > 3 && strcmp(entries[i].name + len - 3, ".ko") == 0) {
            module_count++;
        }
    }

    if (module_count == 0) {
        print("  No .ko modules found\n");
        kfree(entries);
    }

    print("  Found ");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", module_count);
    print(buf);
    print(" modules\n\n");

    // Загружаем все модули
    int loaded = 0;
    int failed = 0;

    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;

        int len = strlen(entries[i].name);
        if (len > 3 && strcmp(entries[i].name + len - 3, ".ko") == 0) {
            char path[256];
            snprintf(path, sizeof(path), "/modules/%s", entries[i].name);

            print("  Loading ");
            print(entries[i].name);
            print("... ");

            int result = module_load(path);

            if (result == 0) {
                print_setcolor(0x0A, 0x00);
                print("OK\n");
                loaded++;
            } else {
                print_setcolor(0x0C, 0x00);
                print("FAILED\n");
                failed++;
            }
            print_setcolor(0x07, 0x00);
        }
    }

    kfree(entries);

    print("\nKinit: ");
    snprintf(buf, sizeof(buf), "%d", loaded);
    print(buf);
    print(" loaded, ");
    snprintf(buf, sizeof(buf), "%d", failed);
    print(buf);
    print(" failed\n");

    // Вызываем entry() для всех успешно загруженных модулей
    print("\nKinit: initializing modules\n");

    loaded_module_t* mod = module_list;
    int init_ok = 0;
    int init_failed = 0;

    while (mod) {
        if (mod->entry) {
            print("  ");
            print(mod->name);
            int len = strlen(mod->name);
            for (int j = len; j < 16; j++) print_char(' ');

            int result = mod->entry();
            if (result == 0) {
                print_setcolor(0x0A, 0x00);
                print("OK\n");
                init_ok++;
            } else {
                print_setcolor(0x0C, 0x00);
                print("FAILED\n");
                init_failed++;
            }
            print_setcolor(0x07, 0x00);
        }
        mod = mod->next;
    }

    print("\nKinit: ");
    snprintf(buf, sizeof(buf), "%d", init_ok);
    print(buf);
    print(" initialized, ");
    snprintf(buf, sizeof(buf), "%d", init_failed);
    print(buf);
    print(" failed\n");
    return 0;
}
