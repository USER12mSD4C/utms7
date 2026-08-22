#include "../drivers/drm.h"
#include "../include/string.h"
#include "../include/module.h"
#include "../include/print.h"
#include "memory.h"
#include "vfs.h"
#include "elf.h"

extern int disk_read(u32 lba, u8* buffer);
extern int disk_write(u32 lba, u8* buffer);
extern int disk_set_disk(int n);
extern u64 disk_get_sectors(u8 drive);

static loaded_module_t* module_list = NULL;

typedef struct {
    const char* name;
    void* addr;
} kernel_sym_t;

static kernel_sym_t kernel_syms[] = {
    {"kmalloc", kmalloc},
    {"kfree", kfree},
    {"memory_used", memory_used},
    {"memory_free", memory_free},
    {"print", print},
    {"printnum", printnum},
    {"printhex", printhex},
    {"print_clear", print_clear},
    {"print_setcolor", print_setcolor},
    {"print_char", print_char},
    {"disk_read", disk_read},
    {"disk_write", disk_write},
    {"disk_set_disk", disk_set_disk},
    {"disk_get_sectors", disk_get_sectors},
    {"vfs_read_entire", vfs_read_entire},
    {"vfs_write_entire", vfs_write_entire},
    {"vfs_mkdir", vfs_mkdir},
    {"vfs_exists", vfs_exists},
    {"vfs_isdir", vfs_isdir},
    {"vfs_readdir", vfs_readdir},
    {"vfs_unlink", vfs_unlink},
    {"vfs_rmdir", vfs_rmdir},
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

static void* resolve_symbol(const char* name) {
    for (kernel_sym_t* ks = kernel_syms; ks->name != NULL; ks++) {
        if (strcmp(ks->name, name) == 0) return ks->addr;
    }

    loaded_module_t* mod = module_list;
    while (mod) {
        if (mod->symtab && mod->strtab) {
            for (u32 i = 0; i < mod->symtab_count; i++) {
                if (mod->symtab[i].st_shndx == SHN_UNDEF) continue;

                char* sym_name = mod->strtab + mod->symtab[i].st_name;
                if (strcmp(sym_name, name) == 0) {
                    return mod->text_base + mod->symtab[i].st_value;
                }
            }
        }
        mod = mod->next;
    }

    return NULL;
}

int module_load(const char* path) {
    u8* data = NULL;
    u32 size = 0;

    if (vfs_read_entire(path, &data, &size) != 0) return -1;

    elf64_hdr_t* hdr = (elf64_hdr_t*)data;
    if (*(u32*)hdr->ident != 0x464C457F || hdr->type != 1) {
        kfree(data);
        return -1;
    }

    u8* text_base = NULL;
    u32 text_size = 0;
    u8* data_base = NULL;
    u32 data_size = 0;
    u8* bss_base = NULL;
    u32 bss_size = 0;

    elf64_shdr_t* shdrs = (elf64_shdr_t*)(data + hdr->shoff);
    char* shstrtab = (char*)(data + shdrs[hdr->shstrndx].sh_offset);

    for (int i = 0; i < hdr->shnum; i++) {
        char* name = shstrtab + shdrs[i].sh_name;

        if (strcmp(name, ".text") == 0) {
            text_size = shdrs[i].sh_size;
            text_base = kmalloc(text_size);
            memcpy(text_base, data + shdrs[i].sh_offset, text_size);
        } else if (strcmp(name, ".data") == 0) {
            data_size = shdrs[i].sh_size;
            data_base = kmalloc(data_size);
            memcpy(data_base, data + shdrs[i].sh_offset, data_size);
        } else if (strcmp(name, ".bss") == 0) {
            bss_size = shdrs[i].sh_size;
            bss_base = kmalloc(bss_size);
            memset(bss_base, 0, bss_size);
        }
    }

    if (!text_base) {
        kfree(data);
        return -1;
    }

    loaded_module_t* mod = kmalloc(sizeof(loaded_module_t));
    memset(mod, 0, sizeof(loaded_module_t));

    mod->text_base = text_base;
    mod->data_base = data_base;
    mod->bss_base = bss_base;
    mod->text_size = text_size;

    for (int i = 0; i < hdr->shnum; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            mod->symtab_count = shdrs[i].sh_size / sizeof(elf64_sym_t);
            mod->symtab = kmalloc(shdrs[i].sh_size);
            memcpy(mod->symtab, data + shdrs[i].sh_offset, shdrs[i].sh_size);
            mod->strtab = (char*)(data + shdrs[shdrs[i].sh_link].sh_offset);
            break;
        }
    }

    mod->next = module_list;
    module_list = mod;

    for (int i = 0; i < hdr->shnum; i++) {
        if (shdrs[i].sh_type == SHT_RELA) {
            elf64_rela_t* relas = (elf64_rela_t*)(data + shdrs[i].sh_offset);
            int count = shdrs[i].sh_size / sizeof(elf64_rela_t);

            for (int j = 0; j < count; j++) {
                int sym_idx = ELF64_R_SYM(relas[j].r_info);
                int type = ELF64_R_TYPE(relas[j].r_info);

                void* sym_addr = NULL;
                if (sym_idx != 0) {
                    char* sym_name = mod->strtab + mod->symtab[sym_idx].st_name;
                    sym_addr = resolve_symbol(sym_name);
                }

                if (type == R_X86_64_64) {
                    u64* target = (u64*)(text_base + relas[j].r_offset);
                    *target = (u64)sym_addr + relas[j].r_addend;
                } else if (type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
                    u32* target32 = (u32*)(text_base + relas[j].r_offset);
                    *target32 = (u32)((u64)sym_addr - (u64)target32 + relas[j].r_addend);
                }
            }
        }
    }

    if (mod->symtab && mod->strtab) {
        for (u32 i = 0; i < mod->symtab_count; i++) {
            char* sym_name = mod->strtab + mod->symtab[i].st_name;
            if (strcmp(sym_name, "module_init") == 0 || strcmp(sym_name, "init_module") == 0) {
                mod->entry = (int (*)(void))(text_base + mod->symtab[i].st_value);
                break;
            }
        }
    }

    kfree(data);
    return 0;
}

int kmod_load_all(void) {
    print("\nKmod: scanning /modules/\n");

    vfs_node_t* dir = vfs_resolve_path("/modules");
    if (!dir || dir->type != VFS_DIR) {
        print("  No /modules/ directory found\n");
        return -1;
    }

    vfs_dirent_t entries[64];
    u32 count = 64;

    if (vfs_readdir(dir, entries, &count) != 0) {
        print("  Cannot read /modules/\n");
        return -1;
    }

    int loaded = 0;

    for (u32 i = 0; i < count; i++) {
        if (entries[i].type == VFS_DIR) continue;

        int len = strlen(entries[i].name);
        if (len > 3 && strcmp(entries[i].name + len - 3, ".ko") == 0) {
            char path[256];
            snprintf(path, sizeof(path), "/modules/%s", entries[i].name);

            if (module_load(path) == 0) loaded++;
        }
    }

    loaded_module_t* mod = module_list;
    while (mod) {
        if (mod->entry) mod->entry();
        mod = mod->next;
    }

    return 0;
}
