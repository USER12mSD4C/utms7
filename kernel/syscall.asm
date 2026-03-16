global syscall_handler

extern syscall_handler_c

section .text
bits 64

syscall_handler:
    swapgs
    mov [rsp-8], rcx      ; сохраняем RIP
    mov [rsp-16], r11     ; сохраняем RFLAGS
    
    ; Переключаемся на стек ядра
    mov rcx, rsp
    mov rsp, [gs:8]       ; kernel rsp
    
    push rcx              ; user rsp
    push r11              ; user rflags
    push rcx              ; user rip
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Вызываем C обработчик
    mov r9, r8
    mov r8, rcx
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_handler_c
    
    ; Восстанавливаем
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx               ; user rip
    pop r11               ; user rflags
    pop rsp               ; user rsp
    
    swapgs
    o64 sysret
