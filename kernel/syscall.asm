global syscall_entry

extern syscall_handler_c

section .text
bits 64

syscall_entry:
    swapgs
    
    mov [rsp-8], rcx
    mov [rsp-16], r11
    
    mov rcx, rsp
    mov rsp, [gs:8]
    
    push rcx
    push r11
    push rcx
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    mov r9, r8
    mov r8, rcx
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_handler_c
    
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
