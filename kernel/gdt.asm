global gdt_flush
global gdt_init

section .text
bits 64

; Функция инициализации GDT (просто вызывает gdt_flush с правильным указателем)
gdt_init:
    ; Загружаем адрес gdt_ptr из .data секции
    lea rdi, [gdt_ptr]
    call gdt_flush
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

section .data
gdt:
    dq 0x0000000000000000  ; 0x00 NULL
    dq 0x0020980000000000  ; 0x08 CODE
    dq 0x0000920000000000  ; 0x10 DATA
gdt_ptr:
    dw $ - gdt - 1
    dq gdt
