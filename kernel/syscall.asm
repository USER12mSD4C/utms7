; kernel/syscall.asm
global syscall_entry

extern syscall_handler_c
extern kernel_stack_temp
extern user_stack_temp

section .text
bits 64

syscall_entry:
    swapgs
    mov [rel user_stack_temp], rsp
    mov rsp, [rel kernel_stack_temp]

    push qword [rel user_stack_temp]
    push r11
    push rcx
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi
    push rax

    mov rdi, rsp
    mov rsi, rax
    call syscall_handler_c

    mov rax, [rsp]

    add rsp, 8
    pop rdi
    pop rsi
    pop rdx
    pop r10
    pop r8
    pop r9
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    pop rcx
    pop r11
    pop rsp

    swapgs
    o64 sysret

section .note.GNU-stack noalloc noexec nowrite progbits
default rel
