bits 64
section .text
global _start
extern main
extern _exit

_start:
    xor rbp, rbp
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    lea rdx, [rsp + 16 + rdi * 8]

    and rsp, -16

    call main

    mov rdi, rax
    call _exit

section .note.GNU-stack noalloc noexec nowrite progbits
