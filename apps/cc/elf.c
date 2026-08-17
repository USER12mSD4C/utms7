#include "elf.h"
#include "../../lib/libc.h"

typedef struct {
    u8 e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} Elf64_Phdr;

typedef struct {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
} Elf64_Shdr;

typedef struct {
    u32 st_name;
    u8 st_info;
    u8 st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
} Elf64_Sym;

int elf_write(const char *filename, CodeGen *cg) {
    int fd = open(filename, 0x41, 0755);
    if (fd < 0) return -1;

    u64 text_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    u64 shstrtab_offset = text_offset + cg->size;
    u64 symtab_offset = (shstrtab_offset + 64 + 7) & ~7;
    u64 strtab_offset = symtab_offset + sizeof(Elf64_Sym) * 2;
    u64 shdr_offset = (strtab_offset + 32 + 7) & ~7;

    const char shstrtab[] = "\0.text\0.shstrtab\0.symtab\0.strtab";
    const char strtab[] = "\0main";

    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7F;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2;
    ehdr.e_ident[5] = 1;
    ehdr.e_ident[6] = 1;
    ehdr.e_type = 2;
    ehdr.e_machine = 0x3E;
    ehdr.e_version = 1;
    ehdr.e_entry = 0x40000000;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = shdr_offset;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 1;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = 5;
    ehdr.e_shstrndx = 2;

    Elf64_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = 1;
    phdr.p_flags = 5;
    phdr.p_offset = text_offset;
    phdr.p_vaddr = 0x40000000;
    phdr.p_paddr = 0x40000000;
    phdr.p_filesz = cg->size;
    phdr.p_memsz = cg->size;
    phdr.p_align = 4096;

    Elf64_Shdr shdrs[5];
    memset(shdrs, 0, sizeof(shdrs));

    shdrs[1].sh_name = 1;
    shdrs[1].sh_type = 1;
    shdrs[1].sh_flags = 6;
    shdrs[1].sh_addr = 0x40000000;
    shdrs[1].sh_offset = text_offset;
    shdrs[1].sh_size = cg->size;
    shdrs[1].sh_addralign = 16;

    shdrs[2].sh_name = 7;
    shdrs[2].sh_type = 3;
    shdrs[2].sh_offset = shstrtab_offset;
    shdrs[2].sh_size = sizeof(shstrtab);
    shdrs[2].sh_addralign = 1;

    shdrs[3].sh_name = 17;
    shdrs[3].sh_type = 2;
    shdrs[3].sh_offset = symtab_offset;
    shdrs[3].sh_size = sizeof(Elf64_Sym) * 2;
    shdrs[3].sh_link = 4;
    shdrs[3].sh_info = 1;
    shdrs[3].sh_addralign = 8;
    shdrs[3].sh_entsize = sizeof(Elf64_Sym);

    shdrs[4].sh_name = 25;
    shdrs[4].sh_type = 3;
    shdrs[4].sh_offset = strtab_offset;
    shdrs[4].sh_size = sizeof(strtab);
    shdrs[4].sh_addralign = 1;

    Elf64_Sym syms[2];
    memset(syms, 0, sizeof(syms));
    syms[1].st_name = 1;
    syms[1].st_info = 0x12;
    syms[1].st_shndx = 1;
    syms[1].st_value = 0x40000000;
    syms[1].st_size = cg->size;

    write(fd, &ehdr, sizeof(ehdr));
    write(fd, &phdr, sizeof(phdr));
    write(fd, cg->code, cg->size);
    write(fd, shstrtab, sizeof(shstrtab));

    u8 zero_pad[8] = {0};
    int pad = symtab_offset - (shstrtab_offset + sizeof(shstrtab));
    for (int i = 0; i < pad; i++) write(fd, zero_pad, 1);

    write(fd, syms, sizeof(syms));
    write(fd, strtab, sizeof(strtab));

    pad = shdr_offset - (strtab_offset + sizeof(strtab));
    for (int i = 0; i < pad; i++) write(fd, zero_pad, 1);

    write(fd, shdrs, sizeof(shdrs));
    close(fd);
    return 0;
}
