; kernel/gdt_flush.asm
bits 64
section .text
global gdt_flush

gdt_flush:
    sub rsp, 16
    dec rsi                 ; limit = size - 1
    mov [rsp], si           ; limit (2 байта)
    mov [rsp+2], rdi        ; base (8 байт)
    lgdt [rsp]
    add rsp, 16

    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
