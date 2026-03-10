global gdt_flush
global gdt_init

section .text
bits 64

; This is a wrapper for the C function
gdt_init:
    ; Just call the C function - the actual implementation is in gdt.c
    ret

gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    pop rdi
    push 0x08
    push .flush_ret
    retfq
.flush_ret:
    ret
