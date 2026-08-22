// File: kernel/acpi.c
#include "../include/acpi.h"
#include "memory.h"
#include "../include/string.h"

static acpi_rsdp_t* rsdp = NULL;
static int use_xsdt = 0;
static u64 table_phys = 0;

static int check_signature(const char* mem, const char* sig, int len) {
    for (int i = 0; i < len; i++) if (mem[i] != sig[i]) return 0;
    return 1;
}

static acpi_rsdp_t* find_rsdp(void) {
    u8* mem;
    for (mem = (u8*)0xE0000; mem < (u8*)0x100000; mem += 16) {
        if (check_signature((char*)mem, "RSD PTR ", 8)) return (acpi_rsdp_t*)mem;
    }
    u64 ebda = (u64)(*(u16*)0x40E) << 4;
    for (mem = (u8*)ebda; mem < (u8*)(ebda + 1024); mem += 16) {
        if (check_signature((char*)mem, "RSD PTR ", 8)) return (acpi_rsdp_t*)mem;
    }
    return NULL;
}

u64 acpi_find_table(const char* signature) {
    if (!rsdp) return 0;
    u64 entries = 0;
    u64* ptrs = NULL;
    if (use_xsdt) {
        acpi_header_t* xsdt = (acpi_header_t*)(table_phys + 0xFFFF800000000000ULL);
        entries = (xsdt->length - sizeof(acpi_header_t)) / 8;
        ptrs = (u64*)((u8*)xsdt + sizeof(acpi_header_t));
    } else {
        acpi_header_t* rsdt = (acpi_header_t*)(table_phys + 0xFFFF800000000000ULL);
        entries = (rsdt->length - sizeof(acpi_header_t)) / 4;
        u32* p32 = (u32*)((u8*)rsdt + sizeof(acpi_header_t));
        for (u64 i = 0; i < entries; i++) {
            acpi_header_t* h = (acpi_header_t*)(0xFFFF800000000000ULL + p32[i]);
            if (check_signature(h->signature, signature, 4)) return p32[i];
        }
        return 0;
    }
    for (u64 i = 0; i < entries; i++) {
        acpi_header_t* h = (acpi_header_t*)(0xFFFF800000000000ULL + ptrs[i]);
        if (check_signature(h->signature, signature, 4)) return ptrs[i];
    }
    return 0;
}

int acpi_init(void) {
    rsdp = find_rsdp();
    if (!rsdp) return -1;
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        use_xsdt = 1;
        table_phys = rsdp->xsdt_address;
    } else {
        use_xsdt = 0;
        table_phys = rsdp->rsdt_address;
    }
    return 0;
}
