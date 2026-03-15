#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define MODULE_MAGIC 0x4B4F4D55

typedef struct {
    u32 magic;
    char name[32];
    u32 class;
    u32 version;
    u32 text_offset;
    u32 data_offset;
    u32 rodata_offset;
    u32 bss_size;
    u32 text_size;
    u32 data_size;
    u32 rodata_size;
    u32 entry_point;
    u32 dep_count;
    struct { char name[32]; u32 version_min; } deps[8];
    u32 crc32;
    u32 symtab_offset;
    u32 symtab_count;
    u32 strtab_offset;
    u32 strtab_size;
} module_header_t;

typedef struct {
    u32 name_offset;
    u32 value_offset;
    u8 type;        // 1 = global defined, 2 = global undefined
    u8 bind;
    u16 shndx;
} module_sym_t;

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: %s <input.o> <output.ko> <module_name>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        printf("Cannot open %s\n", argv[1]);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    u8* elf_data = malloc(file_size);
    fread(elf_data, 1, file_size, f);
    fclose(f);
    
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_data;
    Elf64_Shdr* shdr = (Elf64_Shdr*)(elf_data + ehdr->e_shoff);
    
    u32 text_offset = 0, data_offset = 0, rodata_offset = 0;
    u32 text_size = 0, data_size = 0, rodata_size = 0, bss_size = 0;
    u32 entry = 0;
    
    // Находим секции
    for (int i = 0; i < ehdr->e_shnum; i++) {
        char* name = (char*)(elf_data + shdr[ehdr->e_shstrndx].sh_offset + shdr[i].sh_name);
        
        if (strcmp(name, ".text") == 0) {
            text_offset = shdr[i].sh_offset;
            text_size = shdr[i].sh_size;
        } else if (strcmp(name, ".data") == 0) {
            data_offset = shdr[i].sh_offset;
            data_size = shdr[i].sh_size;
        } else if (strcmp(name, ".rodata") == 0) {
            rodata_offset = shdr[i].sh_offset;
            rodata_size = shdr[i].sh_size;
        } else if (strcmp(name, ".bss") == 0) {
            bss_size = shdr[i].sh_size;
        } else if (strcmp(name, ".module_entry") == 0) {
            entry = *(u32*)(elf_data + shdr[i].sh_offset);
        }
    }
    
    // Собираем символы
    module_sym_t syms[4096];
    char strtab[65536];
    u32 sym_count = 0;
    u32 strtab_pos = 0;
    
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            Elf64_Sym* sym = (Elf64_Sym*)(elf_data + shdr[i].sh_offset);
            char* strtab_elf = (char*)(elf_data + shdr[shdr[i].sh_link].sh_offset);
            int num_sym = shdr[i].sh_size / sizeof(Elf64_Sym);
            
            for (int j = 0; j < num_sym; j++) {
                if (sym[j].st_name == 0) continue;
                
                char* name = strtab_elf + sym[j].st_name;
                if (name[0] == 0) continue;
                
                u8 type = 0;
                if (ELF64_ST_BIND(sym[j].st_info) == STB_GLOBAL) {
                    if (sym[j].st_shndx != SHN_UNDEF) {
                        type = 1; // Global defined
                    } else {
                        type = 2; // Global undefined
                    }
                } else {
                    continue; // Пропускаем локальные символы
                }
                
                // Проверяем, не дубликат ли
                int found = 0;
                for (u32 k = 0; k < sym_count; k++) {
                    if (strcmp(strtab + syms[k].name_offset, name) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (found) continue;
                
                syms[sym_count].name_offset = strtab_pos;
                syms[sym_count].value_offset = sym[j].st_value;
                syms[sym_count].type = type;
                syms[sym_count].bind = ELF64_ST_BIND(sym[j].st_info);
                syms[sym_count].shndx = sym[j].st_shndx;
                
                strcpy(strtab + strtab_pos, name);
                strtab_pos += strlen(name) + 1;
                sym_count++;
                
                if (sym_count >= 4096 || strtab_pos >= 65536) break;
            }
        }
    }
    
    // Создаём заголовок модуля
    module_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MODULE_MAGIC;
    strncpy(hdr.name, argv[3], 31);
    hdr.class = 2; // MODULE_DRIVER
    hdr.version = 1;
    hdr.text_offset = sizeof(module_header_t);
    hdr.data_offset = hdr.text_offset + text_size;
    hdr.rodata_offset = hdr.data_offset + data_size;
    hdr.bss_size = bss_size;
    hdr.text_size = text_size;
    hdr.data_size = data_size;
    hdr.rodata_size = rodata_size;
    hdr.entry_point = entry;
    hdr.dep_count = 0;
    hdr.crc32 = 0;
    hdr.symtab_offset = hdr.rodata_offset + rodata_size;
    hdr.symtab_count = sym_count;
    hdr.strtab_offset = hdr.symtab_offset + sym_count * sizeof(module_sym_t);
    hdr.strtab_size = strtab_pos;
    
    FILE* out = fopen(argv[2], "wb");
    if (!out) {
        printf("Cannot create %s\n", argv[2]);
        free(elf_data);
        return 1;
    }
    
    fwrite(&hdr, sizeof(hdr), 1, out);
    fwrite(elf_data + text_offset, text_size, 1, out);
    fwrite(elf_data + data_offset, data_size, 1, out);
    fwrite(elf_data + rodata_offset, rodata_size, 1, out);
    fwrite(syms, sizeof(module_sym_t), sym_count, out);
    fwrite(strtab, strtab_pos, 1, out);
    
    fclose(out);
    
    printf("Module %s: %u symbols, %u bytes strtab\n", argv[3], sym_count, strtab_pos);
    free(elf_data);
    return 0;
}
